#ifndef RTX_SEND_SOL_RPC_H
#define RTX_SEND_SOL_RPC_H

// Solana network access for sending, over JSON-RPC: recent blockhash,
// balances and account info, rent minimums, recent priority fees, the fee for
// a message, simulation, and broadcast. Only non-indexed methods, which free
// public nodes serve.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/endpoints.h"
#include "../net/http.h"
#include "sol_tx.h"

namespace rtxsend::sol {

using json = nlohmann::json;

// One read-only call, returning "result". Every failure (unreachable node,
// HTTP error, rate limit, method blocked) moves on to the next node.
inline json call(const std::string& method, const json& params) {
    const std::string payload = json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}}.dump();
    std::string lastErr = "no Solana node reachable";
    for (const auto& ep : rtxnet::endpoints::solana()) {
        try {
            const rtxnet::Response r = rtxnet::post(ep, payload);
            const json j = json::parse(r.body, nullptr, false);
            if (r.ok() && !j.is_discarded() && j.contains("result")) return j["result"];
            if (!j.is_discarded() && j.contains("error") && j["error"].contains("message"))
                lastErr = j["error"]["message"].get<std::string>();
            else
                lastErr = "HTTP " + std::to_string(r.status) + " from " + ep;
        } catch (const std::exception& e) {
            lastErr = e.what();
        }
    }
    throw std::runtime_error(lastErr);
}

inline Key latestBlockhash() {
    const json r = call("getLatestBlockhash", {{{"commitment", "confirmed"}}});
    return ::sol::decode(r.at("value").at("blockhash").get<std::string>());
}

struct AccountInfo {
    bool exists = false;
    uint64_t lamports = 0;
    std::string owner;  // owning program
    Bytes data;
};

inline AccountInfo accountInfo(const std::string& addr) {
    const json v = call("getAccountInfo", {addr, {{"encoding", "base64"}, {"commitment", "confirmed"}}}).at("value");
    AccountInfo a;
    if (v.is_null()) return a;
    a.exists = true;
    a.lamports = v.at("lamports").get<uint64_t>();
    a.owner = v.at("owner").get<std::string>();
    a.data = ::sol::base64Decode(v.at("data").at(0).get<std::string>());
    return a;
}

// Lamports an account of `size` bytes must hold to exist.
inline uint64_t rentMinimum(size_t size) {
    return call("getMinimumBalanceForRentExemption", {size}).get<uint64_t>();
}

// A priority price (micro-lamports per compute unit) that recent transactions
// touching these accounts paid: the 75th percentile, kept within sane bounds.
inline uint64_t priorityPrice(const std::vector<std::string>& accounts) {
    std::vector<uint64_t> fees;
    try {
        for (const auto& e : call("getRecentPrioritizationFees", {accounts}))
            fees.push_back(e.value("prioritizationFee", uint64_t(0)));
    } catch (const std::exception&) {
        // Not essential: fall back to the floor below.
    }
    std::sort(fees.begin(), fees.end());
    const uint64_t p75 = fees.empty() ? 0 : fees[fees.size() * 3 / 4];
    return std::clamp<uint64_t>(p75, 10000, 2000000);
}

// The network fee (base + priority) in lamports for a compiled message.
inline uint64_t feeForMessage(const Bytes& message) {
    const json v = call("getFeeForMessage", {::sol::base64Encode(message), {{"commitment", "confirmed"}}}).at("value");
    if (!v.is_number_unsigned()) throw std::runtime_error("Solana: no fee quote (blockhash expired?)");
    return v.get<uint64_t>();
}

struct Simulation {
    std::string error;  // empty on success
    uint64_t units = 0;
    std::vector<std::string> logs;
};

// Runs a transaction on a node without committing it. With sigVerify the
// signature is checked too; otherwise the blockhash is refreshed for us.
inline Simulation simulate(const Bytes& tx, bool sigVerify) {
    json opts = {{"encoding", "base64"}, {"commitment", "confirmed"}, {"sigVerify", sigVerify}};
    if (!sigVerify) opts["replaceRecentBlockhash"] = true;
    const json v = call("simulateTransaction", {::sol::base64Encode(tx), opts}).at("value");
    Simulation s;
    if (v.contains("err") && !v["err"].is_null()) s.error = v["err"].dump();
    s.units = v.value("unitsConsumed", uint64_t(0));
    if (v.contains("logs") && v["logs"].is_array())
        for (const auto& l : v["logs"])
            if (l.is_string()) s.logs.push_back(l.get<std::string>());
    return s;
}

// A short human reason for a failed simulation, from the program logs.
inline std::string reason(const Simulation& s) {
    for (auto it = s.logs.rbegin(); it != s.logs.rend(); ++it) {
        const auto pos = it->find("Error: ");
        if (pos != std::string::npos) return it->substr(pos + 7);
        if (it->find("insufficient") != std::string::npos) return *it;
    }
    return s.error;
}

// Broadcasts a signed transaction; returns its id (the first signature in
// Base58). A node that is unreachable, rate-limited or refuses the method is
// skipped; a real rejection (e.g. the node's preflight simulation failing) is
// thrown at once. "Already processed" means an earlier attempt landed.
inline std::string send(const Bytes& tx, const std::string& txid) {
    const std::string payload =
        json{{"jsonrpc", "2.0"},
             {"id", 1},
             {"method", "sendTransaction"},
             {"params", {::sol::base64Encode(tx), {{"encoding", "base64"}, {"preflightCommitment", "confirmed"}, {"maxRetries", 5}}}}}
            .dump();
    std::string lastErr = "no Solana node reachable";
    for (const auto& ep : rtxnet::endpoints::solana()) {
        json j;
        try {
            const rtxnet::Response r = rtxnet::post(ep, payload);
            j = json::parse(r.body, nullptr, false);
            if (j.is_discarded() || !j.is_object()) {
                lastErr = "HTTP " + std::to_string(r.status) + " from " + ep;
                continue;
            }
            if (j.contains("result") && j["result"].is_string()) return j["result"].get<std::string>();
            if (!r.ok() && !j.contains("error")) {
                lastErr = "HTTP " + std::to_string(r.status) + " from " + ep;
                continue;
            }
        } catch (const std::exception& e) {
            lastErr = e.what();
            continue;
        }
        const json e = j.value("error", json::object());
        const int code = e.value("code", 0);
        std::string msg = e.value("message", std::string("Transaction rejected"));
        std::string lower = msg;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("already been processed") != std::string::npos) return txid;
        if (code == 429 || code == -32005 || lower.find("too many") != std::string::npos ||
            lower.find("blocked") != std::string::npos || lower.find("personal token") != std::string::npos ||
            lower.find("node is behind") != std::string::npos || lower.find("unhealthy") != std::string::npos) {
            lastErr = msg;
            continue;
        }
        // Preflight failures carry the program logs; surface the useful line.
        if (e.contains("data") && e["data"].contains("logs") && e["data"]["logs"].is_array()) {
            Simulation s;
            for (const auto& l : e["data"]["logs"])
                if (l.is_string()) s.logs.push_back(l.get<std::string>());
            const std::string why = reason(s);
            if (!why.empty() && why != s.error) msg += ": " + why;
        }
        throw std::runtime_error(msg);
    }
    throw std::runtime_error(lastErr);
}

}  // namespace rtxsend::sol

#endif  // RTX_SEND_SOL_RPC_H
