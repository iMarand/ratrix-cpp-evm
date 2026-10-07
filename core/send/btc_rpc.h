#ifndef RTX_SEND_BTC_RPC_H
#define RTX_SEND_BTC_RPC_H

// Bitcoin network access for sending: list spendable UTXOs, a current fee rate,
// and broadcast. Uses the same public Esplora-style APIs as the balance check.

#include <stdexcept>
#include <string>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/http.h"
#include "btc_tx.h"

namespace rtxsend::btc {

inline const std::vector<std::string>& apis() {
    static const std::vector<std::string> a = {"https://blockstream.info/api", "https://mempool.space/api"};
    return a;
}

// Confirmed UTXOs for `address`, newest API that answers wins.
inline std::vector<Utxo> fetchUtxos(const std::string& address) {
    std::string lastErr = "no endpoint reachable";
    for (const auto& base : apis()) {
        try {
            const rtxnet::Response r = rtxnet::get(base + "/address/" + address + "/utxo");
            if (!r.ok()) {
                lastErr = "HTTP " + std::to_string(r.status);
                continue;
            }
            nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
            if (j.is_discarded() || !j.is_array()) {
                lastErr = "bad response";
                continue;
            }
            std::vector<Utxo> out;
            for (const auto& u : j) {
                const bool confirmed = u.contains("status") && u["status"].value("confirmed", false);
                if (!confirmed) continue;  // only spend confirmed coins
                out.push_back({u.at("txid").get<std::string>(), u.at("vout").get<uint32_t>(),
                               u.at("value").get<uint64_t>()});
            }
            return out;
        } catch (...) {
            lastErr = "request failed";
        }
    }
    throw std::runtime_error(lastErr);
}

// Fee rate in satoshis per vByte for roughly half-hour confirmation.
inline double feeRate() {
    try {
        const rtxnet::Response r = rtxnet::get("https://mempool.space/api/v1/fees/recommended");
        if (r.ok()) {
            nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
            if (!j.is_discarded() && j.contains("halfHourFee")) return j["halfHourFee"].get<double>();
        }
    } catch (...) {
    }
    try {
        const rtxnet::Response r = rtxnet::get("https://blockstream.info/api/fee-estimates");
        if (r.ok()) {
            nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
            if (!j.is_discarded() && j.contains("3")) return j["3"].get<double>();
        }
    } catch (...) {
    }
    return 8.0;  // conservative fallback sat/vB
}

// Broadcasts a raw transaction hex; returns the txid.
inline std::string broadcast(const std::string& rawHex) {
    std::string lastErr = "no endpoint reachable";
    for (const auto& base : apis()) {
        try {
            const rtxnet::Response r = rtxnet::post(base + "/tx", rawHex, "text/plain");
            if (r.ok()) return r.body;  // Esplora returns the txid as plain text
            lastErr = r.body.empty() ? ("HTTP " + std::to_string(r.status)) : r.body;
        } catch (...) {
            lastErr = "request failed";
        }
    }
    throw std::runtime_error(lastErr);
}

}  // namespace rtxsend::btc

#endif  // RTX_SEND_BTC_RPC_H
