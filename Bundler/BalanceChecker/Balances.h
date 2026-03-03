#include <cctype>
#include <cstdio>
#include <curl/curl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

    std::string performRequest(const std::string& url, const std::string& postFields, const std::string& apiKey) {
        std::string responseBuffer;
        (void)apiKey;

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

        // Current behavior intentionally disables TLS verification.
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            throw std::runtime_error("CURL request failed: " + std::string(curl_easy_strerror(res)));
        }

        return responseBuffer;
    }
};

namespace RTX_BALANCE {
namespace detail {

inline std::string shortResponse(const std::string& response, size_t limit = 220) {
    std::string sanitized;
    sanitized.reserve(response.size());

    for (char c : response) {
        if (c == '\n' || c == '\r' || c == '\t') {
            sanitized.push_back(' ');
        } else {
            sanitized.push_back(c);
        }
    }

    if (sanitized.size() > limit) {
        sanitized.resize(limit);
        sanitized += "...";
    }
    return sanitized;
}

inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

inline long double hexToWei(const std::string& hexBalance) {
    const std::string hexValue = (hexBalance.rfind("0x", 0) == 0 || hexBalance.rfind("0X", 0) == 0)
        ? hexBalance.substr(2)
        : hexBalance;

    if (hexValue.empty()) {
        return 0.0L;
    }

    long double wei = 0.0L;
    for (char c : hexValue) {
        int value = hexDigit(c);
        if (value < 0) {
            throw std::runtime_error("Invalid hex balance value: " + hexBalance);
        }
        wei = (wei * 16.0L) + static_cast<long double>(value);
    }
    return wei;
}

inline std::string weiHexToEtherString(const std::string& hexBalance) {
    const long double wei = hexToWei(hexBalance);
    const long double ether = wei / 1'000'000'000'000'000'000.0L;

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << static_cast<double>(ether);
    return stream.str();
}

inline std::string extractResultHex(const std::string& response) {
    size_t resultPos = response.find("\"result\"");
    if (resultPos == std::string::npos) {
        if (response.find("\"error\"") != std::string::npos) {
            throw std::runtime_error("RPC error response: " + shortResponse(response));
        }
        throw std::runtime_error("Balance not found in response: " + shortResponse(response));
    }

    size_t colonPos = response.find(':', resultPos);
    if (colonPos == std::string::npos) {
        throw std::runtime_error("Malformed JSON-RPC response: " + shortResponse(response));
    }

    size_t valueStart = colonPos + 1;
    while (valueStart < response.size() && std::isspace(static_cast<unsigned char>(response[valueStart]))) {
        ++valueStart;
    }

    if (valueStart >= response.size() || response[valueStart] != '"') {
        throw std::runtime_error("Unexpected result format in response: " + shortResponse(response));
    }

    ++valueStart;
    size_t valueEnd = response.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        throw std::runtime_error("Unterminated result value in response: " + shortResponse(response));
    }

    return response.substr(valueStart, valueEnd - valueStart);
}

} // namespace detail

class EthereumBalanceChecker {
private:
    std::string apiKey;
    std::string endpoint;

    std::string extractBalanceFromResponse(const std::string& response) {
        return detail::weiHexToEtherString(detail::extractResultHex(response));
    }

public:
    EthereumBalanceChecker(const std::string& key = "")
        : apiKey(key), endpoint("https://eth.drpc.org/") {}

    std::string getBalance(const std::string& address) {
        char postfields[256];
        std::snprintf(
            postfields,
            sizeof(postfields),
            "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\",\"params\":[\"%s\",\"latest\"],\"id\":1}",
            address.c_str()
        );

        CurlHandler curl;

        try {
            std::string response = curl.performRequest(endpoint, postfields, apiKey);
            return extractBalanceFromResponse(response);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error fetching balance";
        }
    }
};

class BNBBalanceChecker {
private:
    std::string apiKey;
    std::string endpoint;

    std::string extractBalanceFromResponse(const std::string& response) {
        return detail::weiHexToEtherString(detail::extractResultHex(response));
    }

public:
    BNBBalanceChecker(const std::string& key = "")
        : apiKey(key), endpoint("https://bsc.drpc.org") {}

    std::string getBalance(const std::string& address) {
        char postfields[256];
        std::snprintf(
            postfields,
            sizeof(postfields),
            "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\",\"params\":[\"%s\",\"latest\"],\"id\":1}",
            address.c_str()
        );

        CurlHandler curl;

        try {
            std::string response = curl.performRequest(endpoint, postfields, apiKey);
            return extractBalanceFromResponse(response);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error fetching balance";
        }
    }
};

} // namespace RTX_BALANCE
