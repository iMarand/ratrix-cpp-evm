#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/bn.h>

#include "../net/endpoints.h"

class CurlRequestError : public std::runtime_error {
public:
    explicit CurlRequestError(CURLcode errorCode, const std::string& message)
        : std::runtime_error(message), code(errorCode) {}

    CURLcode code;
};

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
    CurlHandler() : curl(nullptr), headers(nullptr) {
        static const CURLcode initCode = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (initCode != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed: " + std::string(curl_easy_strerror(initCode)));
        }

        curl = curl_easy_init();
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
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 7000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 12000L);
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Ratrix-EVM-CPP/1.0");

        // TLS verification is ON. These talk to public RPC endpoints over HTTPS;
        // disabling verification (the old behavior) let anyone on the path MITM
        // the balance responses. CA resolution order:
        //   1. $RATRIX_CAINFO  (the CLI/GUI set this to the bundled bundle),
        //   2. the OS native CA store (Schannel-backed curl builds),
        //   3. curl's compiled-in default bundle.
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        if (const char* ca = std::getenv("RATRIX_CAINFO"); ca && *ca) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
        } else {
            curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            throw CurlRequestError(
                res,
                "CURL request failed (" + std::to_string(static_cast<int>(res)) + ") for " + url +
                    ": " + std::string(curl_easy_strerror(res))
            );
        }

        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (status < 200 || status >= 300) {
            throw std::runtime_error("HTTP " + std::to_string(status) + " from " + url);
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

// Scales a base-10 integer by 10^decimals into a decimal string, exactly (no
// floating point): formatUnits("1500000", 6) == "1.50". Keeps >= 2 decimals.
inline std::string formatUnits(std::string digits, int decimals) {
    if (digits.empty()) digits = "0";
    if (decimals <= 0) return digits;
    if ((int)digits.size() <= decimals) digits.insert(0, decimals - digits.size() + 1, '0');
    std::string whole = digits.substr(0, digits.size() - decimals);
    std::string frac = digits.substr(digits.size() - decimals);
    while (frac.size() > 2 && frac.back() == '0') frac.pop_back();
    return whole + "." + frac;
}

// Same for a JSON-RPC hex quantity, e.g. "0x1bc16d674ec80000".
inline std::string formatHexUnits(const std::string& rawHex, int decimals) {
    std::string hex = (rawHex.rfind("0x", 0) == 0 || rawHex.rfind("0X", 0) == 0) ? rawHex.substr(2) : rawHex;
    if (hex.empty()) hex = "0";
    BIGNUM* bn = nullptr;
    if (BN_hex2bn(&bn, hex.c_str()) != (int)hex.size()) {
        if (bn) BN_free(bn);
        throw std::runtime_error("Invalid hex quantity: " + shortResponse(rawHex));
    }
    char* dec = BN_bn2dec(bn);
    std::string digits = dec ? dec : "0";
    OPENSSL_free(dec);
    BN_free(bn);
    return formatUnits(digits, decimals);
}

// eth_getBalance against each endpoint in turn until one gives a real answer.
// Any failure (no connection, an HTTP error page, or a JSON-RPC error such as
// a rate limit or "API key required") moves on to the next endpoint. Throws
// the last error if every endpoint fails.
inline std::string rpcBalance(const std::vector<std::string>& endpoints, const std::string& address, int decimals) {
    const std::string body =
        "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\",\"params\":[\"" + address + "\",\"latest\"],\"id\":1}";
    CurlHandler curl;
    std::string lastError = "no endpoint attempted";
    for (const std::string& endpoint : endpoints) {
        try {
            return formatHexUnits(extractResultHex(curl.performRequest(endpoint, body, "")), decimals);
        } catch (const std::exception& e) {
            lastError = e.what();
        }
    }
    throw std::runtime_error(lastError);
}

} // namespace detail

// Native balances as decimal strings, e.g. "0.0421". Both throw if every
// endpoint fails.
class EthereumBalanceChecker {
public:
    explicit EthereumBalanceChecker(const std::string& /*apiKey*/ = "") {}

    std::string getBalance(const std::string& address) {
        return detail::rpcBalance(rtxnet::endpoints::eth(), address, 18);
    }
};

class BNBBalanceChecker {
public:
    explicit BNBBalanceChecker(const std::string& /*apiKey*/ = "") {}

    std::string getBalance(const std::string& address) {
        return detail::rpcBalance(rtxnet::endpoints::bsc(), address, 18);
    }
};

} // namespace RTX_BALANCE
