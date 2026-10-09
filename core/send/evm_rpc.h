#ifndef RTX_SEND_EVM_RPC_H
#define RTX_SEND_EVM_RPC_H

// JSON-RPC calls needed to send an EVM transaction: current nonce, gas price,
// gas estimate, and broadcast. Tries the shared public endpoints per chain.

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/endpoints.h"
#include "../net/http.h"
#include "bignum.h"

namespace rtxsend::evm {

struct Chain {
    uint64_t chainId;
    std::vector<std::string> endpoints;
};

inline Chain ethChain() { return {1, rtxnet::endpoints::eth()}; }
inline Chain bscChain() { return {56, rtxnet::endpoints::bsc()}; }

namespace detail {
// True when a JSON-RPC error is about the node itself (rate limit, API key or
// paid plan required, method unsupported, internal fault) rather than an
// answer about the request, so another node may well succeed.
inline bool nodeSideError(const nlohmann::json& err, long httpStatus) {
    if (httpStatus < 200 || httpStatus >= 300) return true;
    const int code = err.is_object() && err.contains("code") && err["code"].is_number_integer() ? err["code"].get<int>() : 0;
    if (code == -32005 || code == -32029 || code == -32052 || code == -32601 || code == -32603 || code == 429 ||
        code == 35)
        return true;
    std::string msg = err.is_object() && err.contains("message") && err["message"].is_string()
                          ? err["message"].get<std::string>()
                          : std::string();
    for (char& c : msg) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* k : {"rate limit", "too many", "limit exceeded", "api key", "unauthorized", "authenticate",
                          "forbidden", "paid plan", "upgrade", "not available", "not supported", "timeout",
                          "timed out", "temporarily", "capacity", "internal error"})
        if (msg.find(k) != std::string::npos) return true;
    return false;
}
}  // namespace detail

// One JSON-RPC call, returning the "result" node. A node that can't be
// reached or refuses the call (see nodeSideError) is skipped for the next one;
// a real answer about the request (e.g. "insufficient funds") is thrown at
// once without trying other nodes. Throws the last error if every node fails.
inline nlohmann::json rpc(const Chain& chain, const std::string& method, const nlohmann::json& params) {
    nlohmann::json body = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
    const std::string payload = body.dump();
    std::string lastErr = "no endpoint reachable";
    for (const auto& ep : chain.endpoints) {
        rtxnet::Response r;
        try {
            r = rtxnet::post(ep, payload);
        } catch (const std::exception& e) {
            lastErr = e.what();  // no connection: try the next node
            continue;
        }
        const nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            lastErr = r.ok() ? "bad response" : "HTTP " + std::to_string(r.status);
            continue;
        }
        if (j.contains("error")) {
            const auto& e = j["error"];
            const std::string msg = e.is_object() && e.contains("message") && e["message"].is_string()
                                        ? e["message"].get<std::string>()
                                        : "RPC error";
            if (detail::nodeSideError(e, r.status)) {
                lastErr = msg;
                continue;
            }
            throw std::runtime_error(msg);
        }
        if (!r.ok()) {
            lastErr = "HTTP " + std::to_string(r.status);
            continue;
        }
        if (j.contains("result")) return j["result"];
        lastErr = "no result";
    }
    throw std::runtime_error(lastErr);
}

inline Big getNonce(const Chain& c, const std::string& address) {
    return Big::fromHex(rpc(c, "eth_getTransactionCount", {address, "pending"}).get<std::string>());
}
inline Big getGasPrice(const Chain& c) { return Big::fromHex(rpc(c, "eth_gasPrice", nlohmann::json::array()).get<std::string>()); }

inline Big estimateGas(const Chain& c, const std::string& from, const std::string& to, const Big& value,
                       const Bytes& data) {
    static const char* hex = "0123456789abcdef";
    std::string d = "0x";
    for (uint8_t b : data) {
        d.push_back(hex[b >> 4]);
        d.push_back(hex[b & 0x0f]);
    }
    nlohmann::json call = {{"from", from}, {"to", to}, {"value", value.toQuantity()}, {"data", d}};
    return Big::fromHex(rpc(c, "eth_estimateGas", {call}).get<std::string>());
}

inline Big getBalance(const Chain& c, const std::string& address) {
    return Big::fromHex(rpc(c, "eth_getBalance", {address, "latest"}).get<std::string>());
}

// ERC-20 balanceOf(address) via eth_call.
inline Big tokenBalance(const Chain& c, const std::string& token, const std::string& owner) {
    std::string h = owner;
    if (h.rfind("0x", 0) == 0) h = h.substr(2);
    const std::string data = "0x70a08231" + std::string(24, '0') + h;  // selector + left-padded address
    const std::string res = rpc(c, "eth_call", {{{"to", token}, {"data", data}}, "latest"}).get<std::string>();
    return Big::fromHex(res);
}

// Broadcasts a signed "0x..." transaction; returns the transaction hash.
inline std::string sendRaw(const Chain& c, const std::string& rawTxHex) {
    return rpc(c, "eth_sendRawTransaction", {rawTxHex}).get<std::string>();
}

}  // namespace rtxsend::evm

#endif  // RTX_SEND_EVM_RPC_H
