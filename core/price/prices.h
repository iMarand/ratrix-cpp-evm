#ifndef RTX_PRICE_PRICES_H
#define RTX_PRICE_PRICES_H

// Live USD prices and recent change for the assets the wallet shows, from the
// CoinGecko public API (no key needed). One request returns every coin, so the
// UI makes a single call per refresh.

#include <map>
#include <stdexcept>
#include <string>

#include "../../third_party/nlohmann/json.hpp"
#include "../net/http.h"

namespace rtxprice {

struct Quote {
    bool valid = false;
    double usd = 0;        // price in USD
    double change24h = 0;  // percent change, last 24 hours
    double change7d = 0;   // percent change, last 7 days ("few days ago")
};

// CoinGecko coin ids for the assets we track.
inline const std::vector<std::pair<std::string, std::string>>& coins() {
    static const std::vector<std::pair<std::string, std::string>> c = {
        {"bitcoin", "BTC"},   {"ethereum", "ETH"}, {"binancecoin", "BNB"},
        {"tether", "USDT"},   {"usd-coin", "USDC"}};
    return c;
}

// Fetches every tracked coin, keyed by uppercase symbol (BTC, ETH, ...).
// Throws on a network/parse failure; individual coins missing from the reply
// simply stay absent from the map.
inline std::map<std::string, Quote> fetchAll() {
    std::string ids;
    for (const auto& [id, sym] : coins()) ids += (ids.empty() ? "" : ",") + id;
    const std::string url =
        "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=" + ids +
        "&price_change_percentage=24h%2C7d&precision=full";

    const rtxnet::Response r = rtxnet::get(url);
    if (!r.ok()) throw std::runtime_error("prices: HTTP " + std::to_string(r.status));

    nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) throw std::runtime_error("prices: unexpected response");

    std::map<std::string, std::string> idToSym;
    for (const auto& [id, sym] : coins()) idToSym[id] = sym;

    std::map<std::string, Quote> out;
    for (const auto& row : j) {
        if (!row.contains("id")) continue;
        const auto it = idToSym.find(row["id"].get<std::string>());
        if (it == idToSym.end()) continue;
        Quote q;
        auto num = [&](const char* key) -> double {
            return row.contains(key) && row[key].is_number() ? row[key].get<double>() : 0.0;
        };
        q.usd = num("current_price");
        q.change24h = num("price_change_percentage_24h_in_currency");
        q.change7d = num("price_change_percentage_7d_in_currency");
        q.valid = q.usd > 0;
        out[it->second] = q;
    }
    return out;
}

}  // namespace rtxprice

#endif  // RTX_PRICE_PRICES_H
