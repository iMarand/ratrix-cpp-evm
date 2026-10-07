#ifndef RTX_NET_HTTP_H
#define RTX_NET_HTTP_H

// Minimal HTTPS GET/POST over libcurl with certificate verification, shared by
// the sending and price code. Uses the bundled CA file when RATRIX_CAINFO is
// set (as the balance checkers do), otherwise the OS trust store.

#include <cstdlib>
#include <stdexcept>
#include <string>

#include <curl/curl.h>

namespace rtxnet {

struct Response {
    long status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

namespace detail {
inline size_t write(char* p, size_t size, size_t n, void* user) {
    static_cast<std::string*>(user)->append(p, size * n);
    return size * n;
}
}  // namespace detail

inline Response request(const std::string& url, const std::string* body, const char* contentType,
                        long timeoutMs = 15000) {
    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("network: could not start a request");
    Response r;
    struct curl_slist* headers = curl_slist_append(nullptr, "Accept: application/json");
    if (body) {
        const std::string ct = std::string("Content-Type: ") + contentType;
        headers = curl_slist_append(headers, ct.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, detail::write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 7000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "R-Wallet/1.0");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    if (const char* ca = std::getenv("RATRIX_CAINFO"); ca && *ca) curl_easy_setopt(c, CURLOPT_CAINFO, ca);
    else curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));

    const CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) throw std::runtime_error(std::string("network: ") + curl_easy_strerror(rc));
    return r;
}

inline Response get(const std::string& url) { return request(url, nullptr, nullptr); }

inline Response post(const std::string& url, const std::string& body, const char* contentType = "application/json") {
    return request(url, &body, contentType);
}

}  // namespace rtxnet

#endif  // RTX_NET_HTTP_H
