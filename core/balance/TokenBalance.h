#pragma once

// Read-only ERC-20 / BEP-20 token balances (USDT, USDC) via eth_call balanceOf.
// Exact formatting through OpenSSL BIGNUM (no float precision loss).

#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/bn.h>

#include "Balances.h"  // reuses RTX_BALANCE::CurlHandler + detail::extractResultHex

namespace RTX_BALANCE {

struct TokenInfo {
    std::string name;      // "USDT"
    std::string contract;  // 0x... token address
    int decimals;          // 6 on Ethereum, 18 on BSC
    std::vector<std::string> endpoints;
};

namespace tokens {
inline std::vector<std::string> ethRpc() {
    return {"https://eth.drpc.org/", "https://ethereum-rpc.publicnode.com", "https://rpc.ankr.com/eth"};
}
inline std::vector<std::string> bscRpc() {
    return {"https://bsc.drpc.org", "https://bsc-rpc.publicnode.com", "https://rpc.ankr.com/bsc"};
}
inline TokenInfo ethUSDT() { return {"USDT", "0xdAC17F958D2ee523a2206206994597C13D831ec7", 6, ethRpc()}; }
inline TokenInfo ethUSDC() { return {"USDC", "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48", 6, ethRpc()}; }
inline TokenInfo bscUSDT() { return {"USDT", "0x55d398326f99059fF775485246999027B3197955", 18, bscRpc()}; }
inline TokenInfo bscUSDC() { return {"USDC", "0x8AC76a51cc950d9822D68b83fE1Ad97B32Cd580d", 18, bscRpc()}; }
}  // namespace tokens

class TokenBalanceChecker {
private:
    TokenInfo token;

    static std::string lower(std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32;
        return s;
    }

    // Format a raw integer (hex, possibly 0x-prefixed) scaled by 10^decimals.
    static std::string formatUnits(const std::string& rawHex, int decimals) {
        std::string hex = (rawHex.rfind("0x", 0) == 0 || rawHex.rfind("0X", 0) == 0) ? rawHex.substr(2) : rawHex;
        if (hex.empty()) hex = "0";
        BIGNUM* bn = nullptr;
        if (!BN_hex2bn(&bn, hex.c_str())) { if (bn) BN_free(bn); throw std::runtime_error("token: bad hex result"); }
        char* dec = BN_bn2dec(bn);
        std::string digits = dec ? dec : "0";
        OPENSSL_free(dec);
        BN_free(bn);

        if (decimals <= 0) return digits;
        if ((int)digits.size() <= decimals) digits.insert(0, decimals - digits.size() + 1, '0');
        std::string whole = digits.substr(0, digits.size() - decimals);
        std::string frac = digits.substr(digits.size() - decimals);
        while (frac.size() > 2 && frac.back() == '0') frac.pop_back();  // trim, keep >=2 dp
        return whole + "." + frac;
    }

public:
    explicit TokenBalanceChecker(TokenInfo t) : token(std::move(t)) {}

    // Returns the token balance as a decimal string, e.g. "12.50".
    std::string getBalance(const std::string& address) {
        std::string addr = lower(address);
        if (addr.rfind("0x", 0) == 0) addr = addr.substr(2);
        if (addr.size() != 40) throw std::runtime_error("token: bad address");
        std::string data = "0x70a08231" + std::string(24, '0') + addr;  // balanceOf(address), 32-byte padded

        std::string body = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\",\"params\":[{\"to\":\"" +
                           token.contract + "\",\"data\":\"" + data + "\"},\"latest\"],\"id\":1}";

        CurlHandler curl;
        std::string lastError = "no endpoint attempted";
        for (const std::string& ep : token.endpoints) {
            try {
                std::string resp = curl.performRequest(ep, body, "");
                return formatUnits(detail::extractResultHex(resp), token.decimals);
            } catch (const std::exception& e) {
                lastError = e.what();
            }
        }
        throw std::runtime_error(lastError);
    }
};

}  // namespace RTX_BALANCE
