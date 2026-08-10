#pragma once

/**
 * Fast batched Ethereum balance lookups.
 *
 * Why this exists: RTX_BALANCE::EthereumBalanceChecker::getBalance() builds a
 * fresh CurlHandler for every single address, which means a new TCP connection
 * and a new TLS handshake per address, one address per round trip. At ~150 ms
 * RTT that caps you near 3-6 addresses/second no matter how many threads run.
 *
 * The Node WSS script is not faster because it uses WebSockets - it is faster
 * because it opens ONE connection and pipelines every request over it without
 * waiting for replies. This class gets the same win over plain HTTPS:
 *
 *   1. One persistent CURL handle per checker, reused across calls, so the TLS
 *      handshake is paid once instead of once per address (keep-alive).
 *   2. JSON-RPC batching - N eth_getBalance calls in a single POST body, so N
 *      addresses cost ONE round trip instead of N.
 *   3. Several checkers run in parallel, each with its own connection.
 *
 * With batch=100 and 4 checkers, one round trip covers 400 addresses.
 */

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Balances.h" // reuses RTX_BALANCE::detail:: hex/wei helpers

namespace RTX_BALANCE {

namespace detail {

// Split a JSON array body into its top-level {...} objects.
// String-aware, so braces inside strings do not confuse the depth counter.
inline void splitTopLevelObjects(const std::string& body,
                                 std::vector<std::pair<size_t, size_t>>& spans) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t start = 0;

    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];

        if (inString) {
            if (escaped)        escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')  inString = false;
            continue;
        }

        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            if (depth > 0 && --depth == 0) {
                spans.emplace_back(start, i + 1);
            }
        }
    }
}

inline bool findFieldValueStart(const std::string& obj, const std::string& quotedKey, size_t& pos) {
    size_t keyPos = obj.find(quotedKey);
    if (keyPos == std::string::npos) return false;

    size_t colon = obj.find(':', keyPos + quotedKey.size());
    if (colon == std::string::npos) return false;

    ++colon;
    while (colon < obj.size() && std::isspace(static_cast<unsigned char>(obj[colon]))) {
        ++colon;
    }

    pos = colon;
    return pos < obj.size();
}

inline bool extractIntField(const std::string& obj, const std::string& quotedKey, long& out) {
    size_t pos = 0;
    if (!findFieldValueStart(obj, quotedKey, pos)) return false;
    if (!std::isdigit(static_cast<unsigned char>(obj[pos]))) return false;

    long value = 0;
    while (pos < obj.size() && std::isdigit(static_cast<unsigned char>(obj[pos]))) {
        value = value * 10 + (obj[pos] - '0');
        ++pos;
    }

    out = value;
    return true;
}

inline bool extractStringField(const std::string& obj, const std::string& quotedKey, std::string& out) {
    size_t pos = 0;
    if (!findFieldValueStart(obj, quotedKey, pos)) return false;
    if (obj[pos] != '"') return false;

    size_t end = obj.find('"', pos + 1);
    if (end == std::string::npos) return false;

    out = obj.substr(pos + 1, end - pos - 1);
    return true;
}

// True when a 0x-prefixed wei value is non-zero, without any float conversion.
inline bool weiHexIsPositive(const std::string& weiHex) {
    size_t i = (weiHex.rfind("0x", 0) == 0 || weiHex.rfind("0X", 0) == 0) ? 2 : 0;
    for (; i < weiHex.size(); ++i) {
        if (weiHex[i] != '0') return true;
    }
    return false;
}

} // namespace detail

class BatchBalanceChecker {
public:
    // Default list is keyless public nodes only. rpc.ankr.com is deliberately
    // absent: it now rejects keyless requests with -32000 Unauthorized.
    static std::vector<std::string> defaultEndpoints() {
        return {
            "https://ethereum-rpc.publicnode.com",
            "https://eth.llamarpc.com",
            "https://eth.drpc.org/",
            "https://rpc.mevblocker.io",
            "https://eth.meowrpc.com",
            "https://1rpc.io/eth",
        };
    }

    explicit BatchBalanceChecker(std::vector<std::string> rpcEndpoints = defaultEndpoints())
        : endpoints(std::move(rpcEndpoints)), endpointDead(endpoints.size(), false) {
        static const CURLcode initCode = curl_global_init(CURL_GLOBAL_DEFAULT);
        (void)initCode;

        curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }

        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Set once and never reset: curl_easy_reset() would throw away the
        // option set on every call, and we want this handle to stay warm.
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &BatchBalanceChecker::writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 7000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 25000L);
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Ratrix-EVM-CPP/1.0");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // Keep the connection alive between batches - this is the main win.
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 4L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // gzip/deflate

        // Matches the existing checker's behaviour (no CA bundle needed on MinGW).
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    ~BatchBalanceChecker() {
        if (headers) curl_slist_free_all(headers);
        if (curl)    curl_easy_cleanup(curl);
    }

    BatchBalanceChecker(const BatchBalanceChecker&) = delete;
    BatchBalanceChecker& operator=(const BatchBalanceChecker&) = delete;

    // Fetches balances for every address in one JSON-RPC batch request.
    // weiHexOut is resized to addresses.size(); entries a node did not answer
    // are left empty.
    //
    // A batch is only reported as failed after every live endpoint has been
    // tried maxRounds times with exponential backoff, so a transient 500/429
    // never silently drops candidates - the caller can re-queue on false.
    bool fetchBatch(const std::vector<std::string>& addresses, std::vector<std::string>& weiHexOut) {
        weiHexOut.assign(addresses.size(), std::string());
        if (addresses.empty()) return true;

        const std::string payload = buildBatchPayload(addresses);

        for (int round = 0; round < maxRounds; ++round) {
            bool anyLiveEndpoint = false;

            for (size_t attempt = 0; attempt < endpoints.size(); ++attempt) {
                size_t idx = (endpointIdx + attempt) % endpoints.size();
                if (endpointDead[idx]) continue;
                anyLiveEndpoint = true;

                const std::string& endpoint = endpoints[idx];

                responseBuffer.clear();
                curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));

                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    lastError = std::string("curl: ") + curl_easy_strerror(res) + " (" + endpoint + ")";
                    continue;
                }

                long status = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

                // Rate limited or server error: this endpoint is just busy, not
                // broken. Remember it and back off before the next round.
                if (status == 429 || (status >= 500 && status < 600)) {
                    lastError = "HTTP " + std::to_string(status) + " from " + endpoint;
                    continue;
                }

                if (status < 200 || status >= 300) {
                    lastError = "HTTP " + std::to_string(status) + " from " + endpoint;
                    continue;
                }

                // A keyless-auth rejection means this endpoint is useless for
                // the whole run - stop wasting round trips on it.
                if (looksLikeAuthError(responseBuffer)) {
                    endpointDead[idx] = true;
                    lastError = "endpoint needs an API key, disabling: " + endpoint;
                    continue;
                }

                size_t filled = parseBatchResponse(responseBuffer, weiHexOut);
                if (filled == 0) {
                    lastError = "unparseable response from " + endpoint + ": " +
                                detail::shortResponse(responseBuffer);
                    continue;
                }

                endpointIdx = idx; // stick with the winner next time
                return true;
            }

            if (!anyLiveEndpoint) {
                lastError = "all endpoints disabled (need API keys?)";
                return false;
            }

            // Back off before retrying: 250ms, 500ms, 1s, 2s... with jitter so
            // the checker threads do not all retry in lockstep.
            if (round + 1 < maxRounds) {
                backoffSleep(round);
                // Rotate so the next round leads with a different endpoint.
                endpointIdx = (endpointIdx + 1) % endpoints.size();
            }
        }

        return false;
    }

    const std::string& error() const { return lastError; }

    // How hard to retry a failing batch before giving up (caller re-queues).
    void setMaxRounds(int rounds) { maxRounds = rounds < 1 ? 1 : rounds; }

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        size_t total = size * nmemb;
        output->append(static_cast<char*>(contents), total);
        return total;
    }

    // A top-level {"error":...} (object, not a batch array) with an auth-style
    // message means the endpoint wants an API key.
    static bool looksLikeAuthError(const std::string& body) {
        size_t firstNonSpace = body.find_first_not_of(" \t\r\n");
        if (firstNonSpace == std::string::npos || body[firstNonSpace] != '{') {
            return false; // a real batch reply starts with '['
        }
        if (body.find("\"error\"") == std::string::npos) return false;
        return body.find("API key") != std::string::npos ||
               body.find("api key") != std::string::npos ||
               body.find("Unauthorized") != std::string::npos ||
               body.find("unauthorized") != std::string::npos;
    }

    static void backoffSleep(int round) {
        long baseMs = 250L << round;          // 250, 500, 1000, 2000...
        if (baseMs > 4000L) baseMs = 4000L;   // cap the wait
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<long> jitter(0, baseMs / 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(baseMs + jitter(rng)));
    }

    static std::string buildBatchPayload(const std::vector<std::string>& addresses) {
        std::string payload;
        payload.reserve(addresses.size() * 128 + 2);
        payload += '[';

        for (size_t i = 0; i < addresses.size(); ++i) {
            if (i) payload += ',';
            payload += R"({"jsonrpc":"2.0","method":"eth_getBalance","params":[")";
            payload += addresses[i];
            payload += R"(","latest"],"id":)";
            payload += std::to_string(i + 1); // id maps to index + 1
            payload += '}';
        }

        payload += ']';
        return payload;
    }

    // Returns how many slots were filled. Responses may come back in any order,
    // so each result is placed by its id rather than by position.
    static size_t parseBatchResponse(const std::string& body, std::vector<std::string>& weiHexOut) {
        std::vector<std::pair<size_t, size_t>> spans;
        detail::splitTopLevelObjects(body, spans);

        size_t filled = 0;
        for (const auto& span : spans) {
            std::string obj = body.substr(span.first, span.second - span.first);

            long id = 0;
            if (!detail::extractIntField(obj, "\"id\"", id)) continue;
            if (id < 1 || static_cast<size_t>(id) > weiHexOut.size()) continue;

            std::string result;
            if (!detail::extractStringField(obj, "\"result\"", result)) continue;

            weiHexOut[static_cast<size_t>(id) - 1] = result;
            ++filled;
        }

        return filled;
    }

    CURL* curl = nullptr;
    struct curl_slist* headers = nullptr;
    std::vector<std::string> endpoints;
    std::vector<char> endpointDead; // vector<bool> avoided for simple indexing
    size_t endpointIdx = 0;
    int maxRounds = 6;
    std::string responseBuffer;
    std::string lastError;
};

} // namespace RTX_BALANCE
