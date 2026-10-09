#ifndef RTX_SEND_TRON_RPC_H
#define RTX_SEND_TRON_RPC_H

// Tron network access for sending, over the nodes' HTTP API: a recent block
// (TaPoS reference), account balance and free/staked resources, fee prices,
// a simulated contract call (energy estimate, TRC-20 balance), and broadcast.
// Addresses are passed in "T..." form ("visible": true).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/endpoints.h"
#include "../net/http.h"
#include "bignum.h"
#include "tron_tx.h"

namespace rtxsend::tron {

using json = nlohmann::json;

namespace detail {
inline uint64_t u64(const json& j, const char* key) {
    return j.contains(key) && j[key].is_number_integer() ? j[key].get<uint64_t>() : 0;
}
inline Bytes unhex(const std::string& h) {
    if (h.size() % 2) throw std::runtime_error("Tron: odd-length hex from node");
    Bytes out;
    for (size_t i = 0; i < h.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("Tron: bad hex from node");
        };
        out.push_back(static_cast<uint8_t>(nib(h[i]) << 4 | nib(h[i + 1])));
    }
    return out;
}
// Node error messages are sometimes hex-encoded text; decode those.
inline std::string readable(const std::string& m) {
    if (m.empty() || m.size() % 2 || !std::all_of(m.begin(), m.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        return m;
    std::string out;
    for (uint8_t b : unhex(m)) {
        if (b < 0x20 || b > 0x7e) return m;  // not text after all
        out.push_back(static_cast<char>(b));
    }
    return out;
}
}  // namespace detail

// POSTs to each node in turn and returns the first JSON object answer. A node
// that can't be reached or returns an HTTP error is skipped.
inline json call(const std::string& path, const json& body) {
    const std::string payload = body.dump();
    std::string lastErr = "no Tron node reachable";
    for (const auto& base : rtxnet::endpoints::tronHttp()) {
        try {
            const rtxnet::Response r = rtxnet::post(base + path, payload);
            if (!r.ok()) {
                lastErr = "HTTP " + std::to_string(r.status) + " from " + base;
                continue;
            }
            json j = json::parse(r.body, nullptr, false);
            if (j.is_discarded() || !j.is_object()) {
                lastErr = "bad response from " + base;
                continue;
            }
            return j;
        } catch (const std::exception& e) {
            lastErr = e.what();
        }
    }
    throw std::runtime_error(lastErr);
}

inline BlockRef latestBlock() {
    const json j = call("/wallet/getblock", {{"detail", false}});
    BlockRef b;
    if (!j.contains("blockID") || !j["blockID"].is_string()) throw std::runtime_error("Tron: no block from node");
    b.id = detail::unhex(j["blockID"].get<std::string>());
    const json raw = j.value("/block_header/raw_data"_json_pointer, json::object());
    b.number = detail::u64(raw, "number");
    b.timestampMs = detail::u64(raw, "timestamp");
    if (b.id.size() != 32 || b.number == 0 || b.timestampMs == 0) throw std::runtime_error("Tron: incomplete block from node");
    return b;
}

struct Account {
    bool exists = false;   // false until the address first receives TRX ("activated")
    uint64_t balanceSun = 0;
};

inline Account account(const std::string& addr) {
    const json j = call("/wallet/getaccount", {{"address", addr}, {"visible", true}});
    return {j.contains("address"), detail::u64(j, "balance")};  // an unknown address comes back as {}
}

// Resources available right now (bandwidth in bytes, energy in units).
struct Resources {
    uint64_t freeBandwidth = 0;    // the daily free allowance every account gets
    uint64_t stakedBandwidth = 0;  // from TRX staked for bandwidth
    uint64_t energy = 0;           // from TRX staked for energy
};

inline Resources resources(const std::string& addr) {
    const json j = call("/wallet/getaccountresource", {{"address", addr}, {"visible", true}});
    auto left = [&](const char* limit, const char* used) {
        const uint64_t l = detail::u64(j, limit), u = detail::u64(j, used);
        return l > u ? l - u : 0;
    };
    return {left("freeNetLimit", "freeNetUsed"), left("NetLimit", "NetUsed"), left("EnergyLimit", "EnergyUsed")};
}

// Current network prices, in sun (1 TRX = 1,000,000 sun).
struct Prices {
    uint64_t perBandwidthByte = 1000;   // getTransactionFee
    uint64_t perEnergy = 100;           // getEnergyFee
    uint64_t createAccount = 100000;    // getCreateAccountFee: bandwidth for an activating transfer
    uint64_t activation = 1000000;      // getCreateNewAccountFeeInSystemContract
};

inline Prices prices() {
    const json j = call("/wallet/getchainparameters", json::object());
    Prices p;
    if (!j.contains("chainParameter") || !j["chainParameter"].is_array()) throw std::runtime_error("Tron: no fee parameters");
    for (const auto& kv : j["chainParameter"]) {
        const std::string k = kv.value("key", "");
        const uint64_t v = detail::u64(kv, "value");
        if (k == "getTransactionFee") p.perBandwidthByte = v;
        else if (k == "getEnergyFee") p.perEnergy = v;
        else if (k == "getCreateAccountFee") p.createAccount = v;
        else if (k == "getCreateNewAccountFeeInSystemContract") p.activation = v;
    }
    if (p.perEnergy == 0 || p.perBandwidthByte == 0) throw std::runtime_error("Tron: incomplete fee parameters");
    return p;
}

// Runs a contract call on the node without broadcasting it. Throws with the
// node's reason if the call would revert.
struct Simulation {
    uint64_t energy = 0;  // energy it would use (includes the contract's dynamic-energy penalty)
    Bytes result;
};

inline Simulation simulate(const std::string& from, const std::string& contractAddr, const std::string& selector,
                           const Bytes& args) {
    const json j = call("/wallet/triggerconstantcontract", {{"owner_address", from},
                                                            {"contract_address", contractAddr},
                                                            {"function_selector", selector},
                                                            {"parameter", hex(args.data(), args.size())},
                                                            {"visible", true}});
    const json res = j.value("result", json::object());
    const std::string msg = res.value("message", "");
    if (!res.value("result", false) || !msg.empty())
        throw std::runtime_error("The token contract would reject this transfer" +
                                 (msg.empty() ? std::string() : " (" + detail::readable(msg) + ")"));
    Simulation s;
    s.energy = detail::u64(j, "energy_used");
    if (j.contains("constant_result") && j["constant_result"].is_array() && !j["constant_result"].empty())
        s.result = detail::unhex(j["constant_result"][0].get<std::string>());
    return s;
}

inline Big trc20Balance(const std::string& contractAddr, const std::string& owner) {
    Bytes arg(12, 0);  // ABI address: 20-byte id left-padded to 32
    const std::vector<uint8_t> id = ::tron::accountId(owner);
    arg.insert(arg.end(), id.begin(), id.end());
    const Simulation s = simulate(owner, contractAddr, "balanceOf(address)", arg);
    if (s.result.size() != 32) throw std::runtime_error("Tron: bad token balance from node");
    return Big::fromBytes(s.result.data(), s.result.size());
}

// Broadcasts a signed transaction. Busy or unreachable nodes are skipped; a
// real rejection (bad signature, insufficient balance, ...) is thrown at once.
// "Already known" counts as success: an earlier attempt got through.
inline std::string broadcast(const Bytes& signedTx, const std::string& txid) {
    std::string lastErr = "no Tron node reachable";
    for (const auto& base : rtxnet::endpoints::tronHttp()) {
        json j;
        try {
            const rtxnet::Response r =
                rtxnet::post(base + "/wallet/broadcasthex", json{{"transaction", hex(signedTx.data(), signedTx.size())}}.dump());
            j = json::parse(r.body, nullptr, false);
            if (!r.ok() || j.is_discarded() || !j.is_object()) {
                lastErr = "HTTP " + std::to_string(r.status) + " from " + base;
                continue;
            }
        } catch (const std::exception& e) {
            lastErr = e.what();
            continue;
        }
        if (j.value("result", false)) return txid;
        const std::string code = j.value("code", "");
        const std::string msg = detail::readable(j.value("message", ""));
        if (code == "DUP_TRANSACTION_ERROR") return txid;
        if (code == "SERVER_BUSY" || code == "NOT_ENOUGH_EFFECTIVE_CONNECTION" || code == "NO_CONNECTION" ||
            code == "BLOCK_UNSOLIDIFIED" || code == "OTHER_ERROR") {
            lastErr = msg.empty() ? code : msg;
            continue;
        }
        throw std::runtime_error(msg.empty() ? (code.empty() ? "Transaction rejected" : code) : msg);
    }
    throw std::runtime_error(lastErr);
}

}  // namespace rtxsend::tron

#endif  // RTX_SEND_TRON_RPC_H
