#pragma once

// Read-only Bitcoin balance lookup via public block explorer APIs
// (Blockstream / mempool.space). GET-only, no keys ever leave the machine.

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include <curl/curl.h>

namespace RTX_BALANCE {

class BtcBalanceChecker {
private:
    std::vector<std::string> endpoints;

    static size_t writeCb(void* contents, size_t size, size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

    // Blockstream/mempool return: ..."chain_stats":{"funded_txo_sum":N,"spent_txo_sum":M,...}
    // confirmed balance (sats) = funded - spent.
    static long long extractSats(const std::string& body) {
        auto sumField = [&](const char* key) -> long long {
            size_t p = body.find(key);
            if (p == std::string::npos) return 0;
            p = body.find(':', p);
            if (p == std::string::npos) return 0;
            ++p;
            while (p < body.size() && (body[p] == ' ' || body[p] == '"')) ++p;
            long long v = 0;
            bool any = false;
            while (p < body.size() && body[p] >= '0' && body[p] <= '9') { v = v * 10 + (body[p]-'0'); ++p; any = true; }
            if (!any) throw std::runtime_error("btc balance: malformed response");
            return v;
        };
        return sumField("\"funded_txo_sum\"") - sumField("\"spent_txo_sum\"");
    }

    std::string get(const std::string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("btc balance: curl init failed");
        std::string buf;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 7000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 12000L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Ratrix-Wallet/1.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        if (const char* ca = std::getenv("RATRIX_CAINFO"); ca && *ca)
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
        else
            curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);

        CURLcode rc = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK) throw std::runtime_error(std::string("btc balance: ") + curl_easy_strerror(rc));
        if (http != 200) throw std::runtime_error("btc balance: HTTP " + std::to_string(http));
        return buf;
    }

    static std::string satsToBtc(long long sats) {
        bool neg = sats < 0;
        unsigned long long a = neg ? -sats : sats;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%s%llu.%08llu", neg ? "-" : "", a / 100000000ULL, a % 100000000ULL);
        return buf;
    }

public:
    BtcBalanceChecker()
        : endpoints({"https://blockstream.info/api/address/",
                     "https://mempool.space/api/address/"}) {}

    // Returns confirmed balance as a BTC decimal string, e.g. "0.00042000".
    std::string getBalance(const std::string& address) {
        std::string lastError = "no endpoint attempted";
        for (const std::string& base : endpoints) {
            try {
                return satsToBtc(extractSats(get(base + address)));
            } catch (const std::exception& e) {
                lastError = e.what();
            }
        }
        throw std::runtime_error(lastError);
    }
};

}  // namespace RTX_BALANCE
