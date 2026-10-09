#ifndef RTX_SOLANA_H
#define RTX_SOLANA_H

// Solana addresses. Solana signs with ed25519, not secp256k1, so it can't
// share the EVM/Bitcoin private key. Like Phantom, Solflare and Trust Wallet,
// the key comes from the BIP39 seed by SLIP-0010 at m/44'/501'/0'/0', so the
// same recovery phrase restored in those wallets shows the same address. The
// address is the Base58 ed25519 public key. A wallet imported from a private
// key alone has no seed, and therefore no Solana address.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include "bitcoin.h"  // btc::base58, base58Decode, hexToBytes

namespace sol {

namespace detail {
inline std::array<uint8_t, 64> hmacSha512(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len) {
    std::array<uint8_t, 64> out{};
    size_t n = 0;
    if (!EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA512", nullptr, key, keyLen, data, len, out.data(), out.size(), &n) ||
        n != out.size())
        throw std::runtime_error("solana: HMAC-SHA512 failed");
    return out;
}
}  // namespace detail

// SLIP-0010 ed25519 private key for `seed` along `path`. ed25519 only has
// hardened children, so every index is hardened.
inline std::array<uint8_t, 32> derivePrivateKey(const std::vector<uint8_t>& seed, const std::vector<uint32_t>& path) {
    static const char kCurve[] = "ed25519 seed";
    std::array<uint8_t, 64> I =
        detail::hmacSha512(reinterpret_cast<const uint8_t*>(kCurve), sizeof kCurve - 1, seed.data(), seed.size());
    for (uint32_t index : path) {
        const uint32_t i = index | 0x80000000u;
        uint8_t data[37];
        data[0] = 0x00;
        std::memcpy(data + 1, I.data(), 32);  // parent key
        data[33] = static_cast<uint8_t>(i >> 24);
        data[34] = static_cast<uint8_t>(i >> 16);
        data[35] = static_cast<uint8_t>(i >> 8);
        data[36] = static_cast<uint8_t>(i);
        I = detail::hmacSha512(I.data() + 32, 32, data, sizeof data);  // keyed by the parent chain code
        OPENSSL_cleanse(data, sizeof data);
    }
    std::array<uint8_t, 32> key{};
    std::memcpy(key.data(), I.data(), 32);
    OPENSSL_cleanse(I.data(), I.size());
    return key;
}

inline std::array<uint8_t, 32> publicKey(const std::array<uint8_t, 32>& priv) {
    EVP_PKEY* pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv.data(), priv.size());
    if (!pk) throw std::runtime_error("solana: ed25519 key setup failed");
    std::array<uint8_t, 32> pub{};
    size_t n = pub.size();
    const bool ok = EVP_PKEY_get_raw_public_key(pk, pub.data(), &n) == 1 && n == pub.size();
    EVP_PKEY_free(pk);
    if (!ok) throw std::runtime_error("solana: ed25519 public key failed");
    return pub;
}

// Private key of the first account (m/44'/501'/0'/0') for a BIP39 seed given
// as hex, as RTX::toSeed returns it. Callers wipe it after use.
inline std::array<uint8_t, 32> accountKey(const std::string& seedHex) {
    std::vector<uint8_t> seed = btc::hexToBytes(seedHex);
    std::array<uint8_t, 32> priv = derivePrivateKey(seed, {44, 501, 0, 0});
    OPENSSL_cleanse(seed.data(), seed.size());
    return priv;
}

inline std::string addressFromSeed(const std::string& seedHex) {
    std::array<uint8_t, 32> priv = accountKey(seedHex);
    const std::array<uint8_t, 32> pub = publicKey(priv);
    OPENSSL_cleanse(priv.data(), priv.size());
    return btc::base58(std::vector<uint8_t>(pub.begin(), pub.end()));
}

// A Solana address is any 32 bytes in Base58 (32-44 characters).
inline bool isAddress(const std::string& addr) {
    if (addr.size() < 32 || addr.size() > 44) return false;
    try {
        return btc::base58Decode(addr).size() == 32;
    } catch (const std::exception&) {
        return false;
    }
}

// ------------------------- accounts and programs ---------------------------

using Key = std::array<uint8_t, 32>;

inline Key decode(const std::string& addr) {
    const std::vector<uint8_t> v = btc::base58Decode(addr);
    if (v.size() != 32) throw std::runtime_error("Not a Solana address: " + addr);
    Key k{};
    std::copy(v.begin(), v.end(), k.begin());
    return k;
}

inline std::string encode(const Key& k) { return btc::base58(std::vector<uint8_t>(k.begin(), k.end())); }

constexpr const char* kSystemProgram = "11111111111111111111111111111111";
constexpr const char* kTokenProgram = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";
constexpr const char* kAssociatedTokenProgram = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";
constexpr const char* kComputeBudgetProgram = "ComputeBudget111111111111111111111111111111";
constexpr const char* kUsdtMint = "Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB";  // USDT (SPL), 6 decimals

// Whether 32 bytes decode to a point on the ed25519 curve, as Solana (curve25519-
// dalek) decides it: y is the little-endian value with the sign bit cleared,
// and the point exists when (y^2 - 1) / (d*y^2 + 1) has a square root mod p.
inline bool isOnCurve(const Key& k) {
    BN_CTX* ctx = BN_CTX_new();
    BIGNUM *p = BN_new(), *d = BN_new(), *y = BN_new(), *y2 = BN_new(), *u = BN_new(), *v = BN_new(),
           *e = BN_new(), *r = BN_new();
    bool on = false;
    if (ctx && p && d && y && y2 && u && v && e && r) {
        BN_set_bit(p, 255);
        BN_sub_word(p, 19);  // p = 2^255 - 19
        BN_dec2bn(&d, "37095705934669439343138083508754565189542113879843219016388785533085940283555");
        Key le = k;
        le[31] &= 0x7f;
        std::reverse(le.begin(), le.end());  // to big-endian for BIGNUM
        BN_bin2bn(le.data(), static_cast<int>(le.size()), y);
        BN_nnmod(y, y, p, ctx);
        BN_mod_sqr(y2, y, p, ctx);
        BN_mod_sub(u, y2, BN_value_one(), p, ctx);  // u = y^2 - 1
        BN_mod_mul(v, d, y2, p, ctx);
        BN_mod_add(v, v, BN_value_one(), p, ctx);   // v = d*y^2 + 1 (never 0: d is not a square)
        BN_mod_inverse(v, v, p, ctx);
        BN_mod_mul(u, u, v, p, ctx);                // x^2 = u / v
        BN_rshift1(e, p);                           // (p - 1) / 2, as p is odd
        BN_mod_exp(r, u, e, p, ctx);                // Euler's criterion
        on = BN_is_zero(u) || BN_is_one(r);
    }
    for (BIGNUM* b : {p, d, y, y2, u, v, e, r}) BN_free(b);
    BN_CTX_free(ctx);
    return on;
}

// Program-derived address: the first sha256(seeds || bump || program ||
// "ProgramDerivedAddress"), trying bump 255 down to 0, that is off the curve
// (so no private key can exist for it).
inline Key programAddress(const std::vector<Key>& seeds, const Key& program) {
    static const char kMarker[] = "ProgramDerivedAddress";
    for (int bump = 255; bump >= 0; --bump) {
        std::vector<uint8_t> buf;
        for (const Key& s : seeds) buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(static_cast<uint8_t>(bump));
        buf.insert(buf.end(), program.begin(), program.end());
        buf.insert(buf.end(), kMarker, kMarker + sizeof kMarker - 1);
        Key h{};
        SHA256(buf.data(), buf.size(), h.data());
        if (!isOnCurve(h)) return h;
    }
    throw std::runtime_error("solana: no program address found");
}

// The owner's associated token account for `mint`: where wallets keep (and
// expect to receive) that token.
inline Key associatedTokenAccount(const Key& owner, const Key& mint) {
    return programAddress({owner, decode(kTokenProgram), mint}, decode(kAssociatedTokenProgram));
}

// SPL token account layout: mint (32) | owner (32) | amount (u64 LE) | ...
struct TokenAccount {
    Key mint{}, owner{};
    uint64_t amount = 0;
};

inline TokenAccount parseTokenAccount(const std::vector<uint8_t>& data) {
    if (data.size() < 72) throw std::runtime_error("solana: not a token account");
    TokenAccount t;
    std::copy(data.begin(), data.begin() + 32, t.mint.begin());
    std::copy(data.begin() + 32, data.begin() + 64, t.owner.begin());
    for (int i = 7; i >= 0; --i) t.amount = (t.amount << 8) | data[64 + i];
    return t;
}

// Base64, as Solana RPC uses for account data and transactions.
inline std::string base64Encode(const std::vector<uint8_t>& in) {
    std::string out(4 * ((in.size() + 2) / 3), '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), in.data(), static_cast<int>(in.size()));
    out.resize(n < 0 ? 0 : static_cast<size_t>(n));
    return out;
}

inline std::vector<uint8_t> base64Decode(const std::string& in) {
    if (in.empty()) return {};
    if (in.size() % 4) throw std::runtime_error("solana: bad base64");
    std::vector<uint8_t> out(in.size() / 4 * 3);
    if (EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size())) < 0)
        throw std::runtime_error("solana: bad base64");
    size_t pad = 0;
    for (size_t i = in.size(); i > 0 && in[i - 1] == '='; --i) ++pad;
    out.resize(out.size() - pad);
    return out;
}

}  // namespace sol

#endif  // RTX_SOLANA_H
