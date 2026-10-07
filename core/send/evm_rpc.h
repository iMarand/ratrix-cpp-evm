#ifndef RTX_SEND_EVM_RPC_H
#define RTX_SEND_EVM_RPC_H

// JSON-RPC calls needed to send an EVM transaction: current nonce, gas price,
// gas estimate, and broadcast. Tries several public endpoints per chain.

#include <stdexcept>
#include <string>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/http.h"
#include "bignum.h"

namespace rtxsend::evm {

struct Chain {
    uint64_t chainId;
    std::vector<std::string> endpoints;
};

inline Chain ethChain() {
    return {1, {"https://eth.drpc.org", "https://ethereum-rpc.publicnode.com", "https://rpc.ankr.com/eth"}};
}
inline Chain bscChain() {
    return {56, {"https://bsc.drpc.org", "https://bsc-rpc.publicnode.com", "https://rpc.ankr.com/bsc"}};
}

// One JSON-RPC call, returning the "result" node. Throws with the node's error
// message on an RPC error, or after every endpoint fails.
inline nlohmann::json rpc(const Chain& chain, const std::string& method, const nlohmann::json& params) {
    nlohmann::json body = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
    const std::string payload = body.dump();
    std::string lastErr = "no endpoint reachable";
    for (const auto& ep : chain.endpoints) {
        try {
            const rtxnet::Response r = rtxnet::post(ep, payload);
            if (!r.ok()) {
                lastErr = "HTTP " + std::to_string(r.status);
                continue;
            }
            nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
            if (j.is_discarded()) {
                lastErr = "bad response";
                continue;
            }
            if (j.contains("error")) {
                const auto& e = j["error"];
                throw std::runtime_error(e.contains("message") ? e["message"].get<std::string>() : "RPC error");
            }
            if (j.contains("result")) return j["result"];
            lastErr = "no result";
        } catch (const std::runtime_error&) {
            throw;  // a real RPC error (e.g. "insufficient funds"): don't retry other nodes
        } catch (...) {
            lastErr = "request failed";
        }
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
