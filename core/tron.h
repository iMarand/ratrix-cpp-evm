#ifndef RTX_TRON_H
#define RTX_TRON_H

// Tron addresses. Tron uses the same secp256k1 keys and the same 20-byte
// Keccak account id as Ethereum; only the text encoding differs: Base58Check
// over 0x41 + those 20 bytes, which always starts with "T". So a wallet's EVM
// address and its Tron address belong to one private key, and that key
// imports into TronLink unchanged.

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "bitcoin.h"  // btc::base58Check, base58CheckDecode, hexToBytes

namespace tron {

constexpr uint8_t kPrefix = 0x41;

// "0x" + 40 hex characters -> "T..." address.
inline std::string fromEvmAddress(const std::string& evm) {
    std::string h = evm;
    if (h.rfind("0x", 0) == 0 || h.rfind("0X", 0) == 0) h = h.substr(2);
    if (h.size() != 40) throw std::runtime_error("tron: an EVM address has 40 hex characters");
    for (char c : h)
        if (!std::isxdigit(static_cast<unsigned char>(c))) throw std::runtime_error("tron: EVM address is not hex");
    std::vector<uint8_t> payload = btc::hexToBytes(h);
    payload.insert(payload.begin(), kPrefix);
    return btc::base58Check(payload);
}

// The 20-byte account id inside a "T..." address; throws if it isn't one.
inline std::vector<uint8_t> accountId(const std::string& addr) {
    const std::vector<uint8_t> payload = btc::base58CheckDecode(addr);
    if (payload.size() != 21 || payload[0] != kPrefix) throw std::runtime_error("Not a Tron address");
    return {payload.begin() + 1, payload.end()};
}

inline bool isAddress(const std::string& addr) {
    if (addr.size() != 34 || addr[0] != 'T') return false;
    try {
        (void)accountId(addr);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// "T..." -> "0x" + 40 hex, the form Tron's Ethereum-compatible JSON-RPC takes.
inline std::string toEvmHex(const std::string& addr) {
    static const char* hx = "0123456789abcdef";
    std::string out = "0x";
    for (uint8_t b : accountId(addr)) {
        out.push_back(hx[b >> 4]);
        out.push_back(hx[b & 0x0f]);
    }
    return out;
}

}  // namespace tron

#endif  // RTX_TRON_H
