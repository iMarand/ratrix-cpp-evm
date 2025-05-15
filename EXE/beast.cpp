#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <openssl/ssl.h>
#include <sstream>
#include <iomanip>
#include <queue>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace pt = boost::property_tree;
using tcp = boost::asio::ip::tcp;

// Configuration
const std::string WSS_HOST = "bsc.drpc.org";
const std::string WSS_PORT = "443";
const std::string WSS_TARGET = "/";
const int TIMEOUT_SECONDS = 15;
const int CONNECTION_TIMEOUT_SECONDS = 10;
const int MAX_RETRY_ATTEMPTS = 3;
const int BATCH_SIZE = 100; // Number of addresses to process in parallel
const double MIN_BALANCE_TO_SAVE = 0.0001;

// Structure to hold balance information
struct BalanceInfo {
    std::string hex;
    std::string wei;
    double eth;
};

// Structure to represent a pending request
struct PendingRequest {
    int id;
    std::string address;
    std::chrono::steady_clock::time_point timestamp;
};

// Structure for results
using ResultMap = std::unordered_map<std::string, BalanceInfo>;

// Forward declaration
class WebSocketClient;

// Thread-safe queue for message processing
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;

public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        cv_.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        
        if (shutdown_ && queue_.empty()) {
            return false;
        }
        
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        cv_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

// Custom exception for WebSocket operations
class WebSocketException : public std::runtime_error {
public:
    WebSocketException(const std::string& message) : std::runtime_error(message) {}
};

// Class to handle WebSocket connections with SSL
class WebSocketClient : public std::enable_shared_from_this<WebSocketClient> {
private:
    net::io_context& ioc_;
    ssl::context& ctx_;
    std::string host_;
    std::string port_;
    std::string target_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_;
    beast::flat_buffer buffer_;
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> is_closing_{false};
    std::atomic<int> retry_count_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
    
    // Message processing
    ThreadSafeQueue<std::string> send_queue_;
    std::thread message_thread_;
    
    // Request tracking
    std::unordered_map<int, PendingRequest> pending_requests_;
    std::mutex requests_mutex_;
    
    // Results
    ResultMap results_;
    std::mutex results_mutex_;
    
    // Callback when all requests are processed
    std::function<void(const ResultMap&)> completion_callback_;
    
    // Statistics
    std::atomic<int> total_requests_{0};
    std::atomic<int> completed_requests_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    // Constructor
    WebSocketClient(net::io_context& ioc, ssl::context& ctx, 
                    const std::string& host, const std::string& port, 
                    const std::string& target)
        : ioc_(ioc), 
          ctx_(ctx),
          host_(host),
          port_(port),
          target_(target) {
        createWebsocket();
    }
    
    // Create/recreate the websocket
    void createWebsocket() {
        ws_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(ioc_, ctx_);
    }
    
    // Destructor
    ~WebSocketClient() {
        close();
        if (message_thread_.joinable()) {
            send_queue_.shutdown();
            message_thread_.join();
        }
    }
    
    // Start the WebSocket client
    void start(std::function<void(const ResultMap&)> callback) {
        completion_callback_ = std::move(callback);
        start_time_ = std::chrono::steady_clock::now();
        
        // Start message processing thread
        message_thread_ = std::thread([this]() {
            processMessages();
        });
        
        // Connect to the WebSocket server
        connect();
    }
    
    // Connect to WebSocket server with SSL
    void connect() {
        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
            throw beast::system_error(
                beast::error_code(static_cast<int>(::ERR_get_error()),
                net::error::get_ssl_category()),
                "Failed to set SNI Hostname");
        }
        
        // Look up the domain name
        tcp::resolver resolver(ioc_);
        auto const results = resolver.resolve(host_, port_);
        
        // Create a weak pointer to prevent the object from being kept alive by the handlers
        auto self = weak_from_this();
        
        // Connect to the IP address we get from a lookup
        auto& socket = ws_->next_layer().next_layer();
        socket.async_connect(
            results->endpoint(),
            [this, self, results](beast::error_code ec) {
                if (ec) {
                    std::cerr << "Connect error: " << ec.message() << std::endl;
                    if (auto shared_this = self.lock()) {
                        handleConnectionFailure();
                    }
                    return;
                }
                
                // Perform SSL handshake
                ws_->next_layer().async_handshake(
                    ssl::stream_base::client,
                    [this, self](beast::error_code ec) {
                        if (ec) {
                            std::cerr << "SSL Handshake error: " << ec.message() << std::endl;
                            if (auto shared_this = self.lock()) {
                                handleConnectionFailure();
                            }
                            return;
                        }
                        
                        // Set suggested timeout settings for the websocket
                        ws_->set_option(
                            websocket::stream_base::timeout::suggested(
                                beast::role_type::client));
                        
                        // Set a decorator to change the User-Agent of the handshake
                        ws_->set_option(websocket::stream_base::decorator(
                            [](websocket::request_type& req) {
                                req.set(http::field::user_agent,
                                    std::string(BOOST_BEAST_VERSION_STRING) +
                                    " websocket-client-coro");
                            }));
                        
                        // Perform the websocket handshake
                        ws_->async_handshake(host_, target_,
                            [this, self](beast::error_code ec) {
                                if (ec) {
                                    std::cerr << "WebSocket Handshake error: " << ec.message() << std::endl;
                                    if (auto shared_this = self.lock()) {
                                        handleConnectionFailure();
                                    }
                                    return;
                                }
                                
                                std::cout << "WebSocket connection established" << std::endl;
                                is_connected_ = true;
                                
                                if (auto shared_this = self.lock()) {
                                    startReading();
                                }
                                
                                // Notify any waiting threads
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    cv_.notify_all();
                                }
                            });
                    });
            });
    }
    
    // Start asynchronous reading
    void startReading() {
        auto self = weak_from_this();
        ws_->async_read(
            buffer_,
            [this, self](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) {
                    if (ec != websocket::error::closed) {
                        std::cerr << "Read error: " << ec.message() << std::endl;
                    }
                    
                    if (!is_closing_ && auto shared_this = self.lock()) {
                        handleConnectionFailure();
                    }
                    return;
                }
                
                // Process the received message
                std::string msg = beast::buffers_to_string(buffer_.data());
                buffer_.consume(buffer_.size());
                handleMessage(msg);
                
                // Continue reading if not closing
                if (!is_closing_ && auto shared_this = self.lock()) {
                    startReading();
                }
            });
    }
    
    // Process messages from the queue
    void processMessages() {
        std::string message;
        while (send_queue_.pop(message)) {
            if (is_closing_) {
                break;
            }
            
            // Wait until connected
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return is_connected_ || is_closing_; });
                
                if (is_closing_) {
                    break;
                }
            }
            
            try {
                // Send the message synchronously to avoid complexity
                net::const_buffer buffer = net::buffer(message);
                ws_->write(buffer);
            } catch (const std::exception& e) {
                std::cerr << "Error sending message: " << e.what() << std::endl;
                
                if (!is_closing_) {
                    handleConnectionFailure();
                }
                break;
            }
        }
    }
    
    // Queue a message to be sent
    void send(const std::string& message) {
        if (!is_closing_) {
            send_queue_.push(message);
        }
    }
    
    // Add a batch of requests
    void addRequests(const std::vector<std::string>& addresses, std::atomic<int>& next_id) {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        
        for (const auto& address : addresses) {
            int id = next_id++;
            
            // Create JSON-RPC request
            pt::ptree request;
            request.put("jsonrpc", "2.0");
            request.put("method", "eth_getBalance");
            pt::ptree params;
            params.push_back(std::make_pair("", pt::ptree(address)));
            params.push_back(std::make_pair("", pt::ptree("latest")));
            request.add_child("params", params);
            request.put("id", id);
            
            // Convert to JSON string
            std::ostringstream oss;
            pt::write_json(oss, request, false);
            std::string request_str = oss.str();
            
            // Add to pending requests
            PendingRequest req{id, address, std::chrono::steady_clock::now()};
            pending_requests_[id] = req;
            total_requests_++;
            
            // Queue the message for sending
            send(request_str);
        }
    }
    
    // Handle received messages
    void handleMessage(const std::string& message) {
        try {
            // Parse JSON response
            std::istringstream iss(message);
            pt::ptree response;
            pt::read_json(iss, response);
            
            int id = response.get<int>("id");
            
            std::lock_guard<std::mutex> lock(requests_mutex_);
            auto it = pending_requests_.find(id);
            if (it != pending_requests_.end()) {
                PendingRequest& req = it->second;
                std::string address = req.address;
                
                if (response.count("result") > 0) {
                    std::string result = response.get<std::string>("result");
                    
                    // Calculate balance in ETH
                    double balance_eth = 0.0;
                    std::string cleaned_result = result;
                    boost::algorithm::trim(cleaned_result);
                    
                    if (cleaned_result.substr(0, 2) == "0x") {
                        // Convert hex to decimal
                        std::stringstream ss;
                        ss << std::hex << cleaned_result.substr(2);
                        uint64_t wei_value = 0;
                        ss >> wei_value;
                        balance_eth = static_cast<double>(wei_value) / 1e18;
                    }
                    
                    // Store the result
                    {
                        std::lock_guard<std::mutex> result_lock(results_mutex_);
                        results_[address] = {result, cleaned_result, balance_eth};
                    }
                }
                
                // Remove from pending requests
                pending_requests_.erase(it);
                completed_requests_++;
                
                // Check if all requests are completed
                checkCompletion();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing message: " << e.what() << std::endl;
        }
    }
    
    // Check for request timeouts
    void checkTimeouts() {
        auto now = std::chrono::steady_clock::now();
        std::vector<int> timed_out_ids;
        
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            for (auto& pair : pending_requests_) {
                PendingRequest& req = pair.second;
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - req.timestamp).count();
                
                if (elapsed > TIMEOUT_SECONDS) {
                    timed_out_ids.push_back(req.id);
                }
            }
            
            // Remove timed out requests
            for (int id : timed_out_ids) {
                completed_requests_++;
                pending_requests_.erase(id);
            }
        }
        
        if (!timed_out_ids.empty()) {
            std::cerr << "Warning: " << timed_out_ids.size() 
                      << " requests timed out" << std::endl;
            
            // Check if all requests are completed
            checkCompletion();
        }
    }
    
    // Check if all requests have been completed
    void checkCompletion() {
        if (completed_requests_ >= total_requests_ && total_requests_ > 0) {
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time_).count();
            
            std::cout << "All requests completed in " << duration << "ms" << std::endl;
            
            // Call the completion callback with the results
            if (completion_callback_) {
                ResultMap result_copy;
                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    result_copy = results_;
                }
                completion_callback_(result_copy);
            }
            
            // Close the connection
            close();
        }
    }
    
    // Handle connection failures
    void handleConnectionFailure() {
        if (is_closing_) {
            return;
        }
        
        retry_count_++;
        
        if (retry_count_ < MAX_RETRY_ATTEMPTS) {
            std::cerr << "Connection lost. Retrying (" << retry_count_ << "/" 
                      << MAX_RETRY_ATTEMPTS << ")..." << std::endl;
            
            // Reset connection state
            is_connected_ = false;
            
            // Create a new WebSocket stream
            createWebsocket();
            
            // Reconnect after a short delay
            std::this_thread::sleep_for(std::chrono::seconds(1));
            connect();
        } else {
            std::cerr << "Max retry attempts reached. Closing connection." << std::endl;
            close();
            
            // Call the completion callback with partial results
            if (completion_callback_) {
                ResultMap result_copy;
                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    result_copy = results_;
                }
                completion_callback_(result_copy);
            }
        }
    }
    
    // Close the WebSocket connection
    void close() {
        if (is_closing_) {
            return;
        }
        
        is_closing_ = true;
        
        try {
            if (is_connected_) {
                is_connected_ = false;
                
                // Close the WebSocket connection
                beast::error_code ec;
                ws_->close(websocket::close_code::normal, ec);
                
                if (ec) {
                    std::cerr << "Error closing WebSocket: " << ec.message() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception during close: " << e.what() << std::endl;
        }
        
        // Notify any waiting threads
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_all();
        }
    }
    
    // Public access to connection status for monitoring
    bool isClosing() const { return is_closing_; }
    int getCompletedRequests() const { return completed_requests_; }
    int getTotalRequests() const { return total_requests_; }
};

// Function to read addresses from a file
std::vector<std::string> readAddressesFromFile(const std::string& filename) {
    std::vector<std::string> addresses;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        boost::algorithm::trim(line);
        if (!line.empty()) {
            addresses.push_back(line);
        }
    }
    
    return addresses;
}

// Function to save results to a file
void saveResultsToFile(const ResultMap& results, const std::string& filename) {
    std::ofstream file(filename, std::ios::app);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }
    
    // Count of addresses with significant balance
    int count = 0;
    for (const auto& pair : results) {
        if (pair.second.eth > MIN_BALANCE_TO_SAVE) {
            file << "-->>> " << pair.first << " :: " << count << " :: " 
                 << std::fixed << std::setprecision(18) << pair.second.eth << " ->> " << std::endl;
            count++;
        }
    }
    
    std::cout << "Saved " << count << " addresses with balance > " 
              << MIN_BALANCE_TO_SAVE << " to " << filename << std::endl;
}

// Function to print progress
void printProgress(int completed, int total) {
    if (total == 0) return;
    
    int percentage = (completed * 100) / total;
    std::cout << "\rProgress: " << completed << "/" << total 
              << " (" << percentage << "%)" << std::flush;
}

// The main balance checking function using WebSockets
void getMultipleBalancesWSS(const std::vector<std::string>& addresses, 
                            const std::string& output_file) {
    try {
        // Set up the IO context and SSL context
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        
        // Load SSL certificates
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);
        
        // Create a timer for checking timeouts
        net::steady_timer timeout_timer(ioc);
        
        // Create the WebSocket client
        auto client = std::make_shared<WebSocketClient>(ioc, ctx, WSS_HOST, WSS_PORT, WSS_TARGET);
        
        // Counter for request IDs
        std::atomic<int> next_id{1};
        
        // Completion flag
        std::atomic<bool> completed{false};
        
        // Start the client with a completion callback
        client->start([&](const ResultMap& results) {
            // Print the results
            std::cout << "\nWebSocket Results:" << std::endl;
            int index = 0;
            for (const auto& pair : results) {
                std::cout << index << "::" << pair.first << ": " 
                          << std::fixed << std::setprecision(18) << pair.second.eth << " ETH" << std::endl;
                index++;
            }
            
            // Save the results
            saveResultsToFile(results, output_file);
            
            // Set completed flag
            completed = true;
        });
        
        // Define the timeout checking function
        std::function<void(const boost::system::error_code&)> check_timeouts;
        check_timeouts = [&](const boost::system::error_code& ec) {
            if (ec) return;
            
            if (!completed) {
                client->checkTimeouts();
                
                // Schedule the next check
                timeout_timer.expires_after(std::chrono::seconds(1));
                timeout_timer.async_wait(check_timeouts);
            }
        };
        
        // Start checking timeouts
        timeout_timer.expires_after(std::chrono::seconds(1));
        timeout_timer.async_wait(check_timeouts);
        
        // Start the progress reporting thread
        std::thread progress_thread([client]() {
            while (!client->isClosing()) {
                printProgress(client->getCompletedRequests(), client->getTotalRequests());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << std::endl;
        });
        
        // Process addresses in batches
        std::cout << "Starting to process " << addresses.size() << " addresses" << std::endl;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        
        for (size_t i = 0; i < addresses.size(); i += BATCH_SIZE) {
            size_t batch_end = std::min(i + BATCH_SIZE, addresses.size());
            std::vector<std::string> batch(addresses.begin() + i, addresses.begin() + batch_end);
            
            // Add the batch of requests
            client->addRequests(batch, next_id);
            
            // Small delay between batches to avoid overloading
            if (batch_end < addresses.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        // Run the IO context
        std::vector<std::thread> threads;
        int num_threads = std::thread::hardware_concurrency();
        threads.reserve(num_threads);
        
        // Launch worker threads
        for (int i = 1; i < num_threads; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
        }
        
        // Run the main thread until completion
        ioc.run();
        
        // Join the worker threads
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        
        // Join the progress thread
        if (progress_thread.joinable()) {
            progress_thread.join();
        }
        
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "Operation completed in " << duration << "ms" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    try {
        std::cout << "\n=== NATIVE WEBSOCKET METHOD (C++ BEAST) ===" << std::endl;
        
        // Read addresses from file
        std::vector<std::string> addresses = readAddressesFromFile("addresses");
        std::cout << "Loaded " << addresses.size() << " addresses" << std::endl;
        
        // Start the balance check operation
        auto start = std::chrono::steady_clock::now();
        getMultipleBalancesWSS(addresses, "found.txt");
        auto end = std::chrono::steady_clock::now();
        
        // Print timing information
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "Total operation time: " << duration << "ms" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}