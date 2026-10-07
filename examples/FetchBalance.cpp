#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <iomanip>
#include <sstream>
#include <curl/curl.h>

class CurlHandler {
private:
    CURL* curl;
    struct curl_slist* headers;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        size_t total_size = size * nmemb;
        output->append(static_cast<char*>(contents), total_size);
        return total_size;
    }

public:
    CurlHandler() : curl(curl_easy_init()), headers(nullptr) {
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
    }

    ~CurlHandler() {
        if (headers) {
            curl_slist_free_all(headers);
        }
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    CurlHandler(const CurlHandler&) = delete;
    CurlHandler& operator=(const CurlHandler&) = delete;

    std::string performRequest(const std::string& url, 
                               const std::string& postFields, 
                               const std::string& apiKey) {
        std::string responseBuffer;

        if (headers) {
            curl_slist_free_all(headers);
            headers = nullptr;
        }

        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        
        // Disable SSL certificate verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            throw std::runtime_error("CURL request failed: " + 
                std::string(curl_easy_strerror(res)));
        }

        return responseBuffer;
    }
};

class EthereumBalanceChecker {
    private:
        std::string apiKey;
        std::string endpoint;

        double hexToEther(const std::string& hexBalance) {
            std::string hexValue = (hexBalance.substr(0, 2) == "0x") ? hexBalance.substr(2) : hexBalance;
            unsigned long long weiBalance = std::stoull(hexValue, nullptr, 16);

            // Convert Wei to Ether (1 Ether = 10^18 Wei)
            return static_cast<double>(weiBalance) / 1'000'000'000'000'000'000.0;
        }

        std::string extractBalanceFromResponse(const std::string& response) {
            size_t resultPos = response.find("\"result\":\"");
            if (resultPos == std::string::npos) {
                throw std::runtime_error("Balance not found in response");
            }

            size_t startPos = resultPos + 10;
            size_t endPos = response.find("\"", startPos);
            std::string hexBalance = response.substr(startPos, endPos - startPos);

            double etherBalance = hexToEther(hexBalance);
            
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(6) << etherBalance;
            return stream.str();
        }

    public:
        EthereumBalanceChecker(const std::string& key = "") : 
            apiKey(key),
            endpoint("https://eth.drpc.org/") {}

        std::string getBalance(const std::string& address) {
            // Prepare JSON-RPC payload
            char postfields[256];
            snprintf(postfields, sizeof(postfields), 
                "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\",\"params\":[\"%s\",\"latest\"],\"id\":1}", 
                address.c_str());

            CurlHandler curl;
            
            try {
                std::string response = curl.performRequest(endpoint, postfields, apiKey);
                return extractBalanceFromResponse(response);
            }
            catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return "Error fetching balance";
            }
        }
};

int main() {
    std::cout << "Checking wallet etheruem balance" << std::endl;

    try {
        std::string address = "0x75bced4bc8f882facbdb56d458c35cc079d48255";
        EthereumBalanceChecker checker;

        std::string balance = checker.getBalance(address);
        std::cout << "Balance for " << address << ": " << balance << " ETH" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}