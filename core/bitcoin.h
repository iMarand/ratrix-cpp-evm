#ifndef RTX_BITCOIN_H
#define RTX_BITCOIN_H

// Bitcoin address generation from a secp256k1 private key.
//
// Keeps the project's "any input derives a key" philosophy: a private key here
// is just a 32-byte secret (the same one Ratrix derives from a seed phrase), so
// an EVM wallet and a BTC wallet share one private key and both addresses come
// from the same secret. Supports:
//   - P2PKH  (legacy, "1..."),  Base58Check over 0x00 + HASH160(pubkey)
//   - P2WPKH (native segwit, "bc1q..."), Bech32 over witness v0 + HASH160
//   - WIF export of the private key (compressed)

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include "ripemd160.h"

namespace btc {

enum class AddressType { P2PKH, P2WPKH };

inline std::vector<uint8_t> hexToBytes(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::runtime_error("btc: odd-length hex");
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

// Compressed secp256k1 public key (33 bytes) for a 32-byte private key given as hex.
inline std::vector<uint8_t> compressedPubKey(const std::string& privHex) {
    BIGNUM* priv = nullptr;
    if (!BN_hex2bn(&priv, privHex.c_str())) throw std::runtime_error("btc: bad private key hex");

    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    EC_POINT* point = group ? EC_POINT_new(group) : nullptr;
    if (!group || !point) {
        if (point) EC_POINT_free(point);
        if (group) EC_GROUP_free(group);
        BN_free(priv);
        throw std::runtime_error("btc: EC init failed");
    }

    std::vector<uint8_t> pub(33);
    bool ok = EC_POINT_mul(group, point, priv, nullptr, nullptr, nullptr) == 1 &&
              EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, pub.data(), pub.size(), nullptr) == 33;

    EC_POINT_free(point);
    EC_GROUP_free(group);
    BN_free(priv);
    if (!ok) throw std::runtime_error("btc: public key derivation failed");
    return pub;
}

inline std::array<uint8_t, 32> sha256(const uint8_t* d, size_t n) {
    std::array<uint8_t, 32> out{};
    SHA256(d, n, out.data());
    return out;
}

// HASH160 = RIPEMD160(SHA256(data)) — the 20-byte key hash used by both address types.
inline std::array<uint8_t, 20> hash160(const std::vector<uint8_t>& data) {
    auto s = sha256(data.data(), data.size());
    return rmd160::hash(s.data(), s.size());
}

// ----------------------------- Base58Check ------------------------------
inline std::string base58(const std::vector<uint8_t>& input) {
    static const char* kAlphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    int zeros = 0;
    while (zeros < (int)input.size() && input[zeros] == 0) ++zeros;

    std::vector<uint8_t> b58((input.size() - zeros) * 138 / 100 + 1, 0);
    for (size_t i = zeros; i < input.size(); ++i) {
        int carry = input[i];
        for (int j = (int)b58.size() - 1; j >= 0; --j) {
            carry += 256 * b58[j];
            b58[j] = carry % 58;
            carry /= 58;
        }
    }
    size_t it = 0;
    while (it < b58.size() && b58[it] == 0) ++it;

    std::string out;
    out.assign(zeros, '1');
    for (; it < b58.size(); ++it) out += kAlphabet[b58[it]];
    return out;
}

inline std::string base58Check(const std::vector<uint8_t>& payload) {
    auto h1 = sha256(payload.data(), payload.size());
    auto h2 = sha256(h1.data(), h1.size());
    std::vector<uint8_t> full = payload;
    full.insert(full.end(), h2.begin(), h2.begin() + 4);
    return base58(full);
}

// ------------------------------- Bech32 (BIP173) -------------------------
namespace bech32 {
inline uint32_t polymod(const std::vector<uint8_t>& values) {
    static const uint32_t gen[5] = {0x3b6a57b2u, 0x26508e6du, 0x1ea119fau, 0x3d4233ddu, 0x2a1462b3u};
    uint32_t chk = 1;
    for (uint8_t v : values) {
        uint8_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ v;
        for (int i = 0; i < 5; ++i)
            if ((top >> i) & 1) chk ^= gen[i];
    }
    return chk;
}
inline std::vector<uint8_t> hrpExpand(const std::string& hrp) {
    std::vector<uint8_t> out;
    for (char c : hrp) out.push_back((uint8_t)c >> 5);
    out.push_back(0);
    for (char c : hrp) out.push_back((uint8_t)c & 31);
    return out;
}
inline std::string encode(const std::string& hrp, const std::vector<uint8_t>& data) {
    static const char* kCharset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    std::vector<uint8_t> values = hrpExpand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    values.insert(values.end(), {0, 0, 0, 0, 0, 0});
    uint32_t mod = polymod(values) ^ 1;
    std::string out = hrp + "1";
    for (uint8_t d : data) out += kCharset[d];
    for (int i = 0; i < 6; ++i) out += kCharset[(mod >> (5 * (5 - i))) & 31];
    return out;
}
// Regroup 8-bit bytes into 5-bit groups (for witness programs).
inline std::vector<uint8_t> convertTo5bit(const std::vector<uint8_t>& in) {
    int acc = 0, bits = 0;
    std::vector<uint8_t> out;
    for (uint8_t v : in) {
        acc = (acc << 8) | v;
        bits += 8;
        while (bits >= 5) { bits -= 5; out.push_back((acc >> bits) & 31); }
    }
    if (bits > 0) out.push_back((acc << (5 - bits)) & 31);
    return out;
}
}  // namespace bech32

// -------------------------------- Public API -----------------------------
// mainnet prefixes; override for testnet if ever needed.
inline std::string getAddressP2PKH(const std::string& privHex, uint8_t versionByte = 0x00) {
    auto h = hash160(compressedPubKey(privHex));
    std::vector<uint8_t> payload;
    payload.push_back(versionByte);
    payload.insert(payload.end(), h.begin(), h.end());
    return base58Check(payload);
}

inline std::string getAddressP2WPKH(const std::string& privHex, const std::string& hrp = "bc") {
    auto h = hash160(compressedPubKey(privHex));
    std::vector<uint8_t> prog(h.begin(), h.end());
    std::vector<uint8_t> data;
    data.push_back(0);  // witness version 0
    auto conv = bech32::convertTo5bit(prog);
    data.insert(data.end(), conv.begin(), conv.end());
    return bech32::encode(hrp, data);
}

inline std::string getAddress(const std::string& privHex, AddressType type = AddressType::P2WPKH) {
    return type == AddressType::P2PKH ? getAddressP2PKH(privHex) : getAddressP2WPKH(privHex);
}

// Wallet Import Format for the (compressed) private key, mainnet (0x80).
inline std::string toWIF(const std::string& privHex, uint8_t versionByte = 0x80) {
    auto key = hexToBytes(privHex);
    if (key.size() != 32) throw std::runtime_error("btc: WIF needs a 32-byte key");
    std::vector<uint8_t> payload;
    payload.push_back(versionByte);
    payload.insert(payload.end(), key.begin(), key.end());
    payload.push_back(0x01);  // compressed-pubkey flag
    return base58Check(payload);
}

}  // namespace btc

#endif  // RTX_BITCOIN_H
