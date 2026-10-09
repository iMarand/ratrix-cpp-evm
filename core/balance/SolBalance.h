#pragma once

// Read-only Solana balances: SOL via getBalance (lamports, 9 decimals) and SPL
// tokens (USDT) by reading the owner's associated token account with
// getAccountInfo. Free public nodes refuse the "indexed" token queries
// (getTokenAccountsByOwner, getTokenAccountBalance), which is why the token
// account address is derived locally. Tries each endpoint until one answers.

#include <cstdint>
#include <stdexcept>
#include <string>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/endpoints.h"
#include "../net/http.h"
#include "../solana.h"
#include "Balances.h"  // detail::formatUnits

namespace RTX_BALANCE {

class SolBalanceChecker {
public:
    // SOL balance as a decimal string, e.g. "1.25".
    std::string getBalance(const std::string& address) {
        if (!sol::isAddress(address)) throw std::runtime_error("Not a Solana address");
        const nlohmann::json value = call("getBalance", {address}).at("value");
        if (!value.is_number_unsigned()) throw std::runtime_error("Unexpected getBalance response");
        return detail::formatUnits(std::to_string(value.get<uint64_t>()), 9);
    }

    // SPL token balance (e.g. sol::kUsdtMint, 6 decimals) held in the owner's
    // associated token account; "0.00" if that account doesn't exist yet.
    std::string getTokenBalance(const std::string& mint, int decimals, const std::string& owner) {
        const sol::Key ata = sol::associatedTokenAccount(sol::decode(owner), sol::decode(mint));
        const nlohmann::json value = call("getAccountInfo", {sol::encode(ata), {{"encoding", "base64"}}}).at("value");
        if (value.is_null()) return detail::formatUnits("0", decimals);
        const sol::TokenAccount t = sol::parseTokenAccount(sol::base64Decode(value.at("data").at(0).get<std::string>()));
        if (t.mint != sol::decode(mint)) throw std::runtime_error("Token account holds a different token");
        // Its owner can be reassigned on chain; then none of it is this wallet's.
        if (t.owner != sol::decode(owner)) return detail::formatUnits("0", decimals);
        return detail::formatUnits(std::to_string(t.amount), decimals);
    }

private:
    // One JSON-RPC call; returns "result". Any failure moves on to the next node.
    static nlohmann::json call(const char* method, const nlohmann::json& params) {
        const std::string payload =
            nlohmann::json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}}.dump();
        std::string lastError = "no endpoint attempted";
        for (const std::string& ep : rtxnet::endpoints::solana()) {
            try {
                const rtxnet::Response r = rtxnet::post(ep, payload);
                const nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
                if (r.ok() && !j.is_discarded() && j.contains("result") && j["result"].is_object()) return j["result"];
                lastError = "HTTP " + std::to_string(r.status) + " from " + ep + ": " + detail::shortResponse(r.body);
            } catch (const std::exception& e) {
                lastError = e.what();
            }
        }
        throw std::runtime_error(lastError);
    }
};

}  // namespace RTX_BALANCE
