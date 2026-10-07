#ifndef RTX_SEND_SIGNER_H
#define RTX_SEND_SIGNER_H

// ECDSA signing for transactions, delegated to the vendored reference library
// libsecp256k1. It produces RFC 6979 deterministic, low-S signatures, and for
// Ethereum gives the recovery id directly — so no elliptic-curve math is
// implemented here, only the thin glue the two transaction formats need.

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_recovery.h>

namespace rtxsend {

using Hash32 = std::array<uint8_t, 32>;
using Seckey = std::array<uint8_t, 32>;

struct RecoverableSig {
    std::array<uint8_t, 64> compact{};  // r || s, 32 bytes each
    int recid = 0;                      // 0..3; Ethereum v = recid + 27 (+ chainId*2 + 8 for EIP-155)
};

namespace detail {

// One process-wide, randomized context (randomization hardens against some
// side-channel attacks; it is not required for correctness).
inline secp256k1_context* ctx() {
    static secp256k1_context* c = [] {
        secp256k1_context* x = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        std::array<uint8_t, 32> seed{};
        // Best-effort randomization; failure is non-fatal (signing still works).
        for (auto& b : seed) b = static_cast<uint8_t>(std::rand());
        if (!secp256k1_context_randomize(x, seed.data())) { /* ignore: not required for correctness */ }
        return x;
    }();
    return c;
}

inline Seckey seckeyFromHex(std::string h) {
    if (h.rfind("0x", 0) == 0 || h.rfind("0X", 0) == 0) h = h.substr(2);
    if (h.size() > 64) throw std::runtime_error("private key too long");
    h.insert(0, 64 - h.size(), '0');  // left-pad to 32 bytes
    Seckey k{};
    for (size_t i = 0; i < 32; ++i) {
        auto hx = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("private key is not hex");
        };
        k[i] = static_cast<uint8_t>((hx(h[i * 2]) << 4) | hx(h[i * 2 + 1]));
    }
    if (!secp256k1_ec_seckey_verify(ctx(), k.data()))
        throw std::runtime_error("private key is out of range");
    return k;
}

}  // namespace detail

// Ethereum-style recoverable signature over a 32-byte message hash.
inline RecoverableSig signRecoverable(const Hash32& msg, const std::string& privHex) {
    const Seckey key = detail::seckeyFromHex(privHex);
    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(detail::ctx(), &sig, msg.data(), key.data(), nullptr, nullptr))
        throw std::runtime_error("signing failed");
    RecoverableSig out;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(detail::ctx(), out.compact.data(), &out.recid, &sig);
    return out;
}

// Bitcoin-style DER signature (low-S) over a 32-byte sighash.
inline std::vector<uint8_t> signDer(const Hash32& msg, const std::string& privHex) {
    const Seckey key = detail::seckeyFromHex(privHex);
    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_sign(detail::ctx(), &sig, msg.data(), key.data(), nullptr, nullptr))
        throw std::runtime_error("signing failed");
    std::vector<uint8_t> der(72);
    size_t len = der.size();
    if (!secp256k1_ecdsa_signature_serialize_der(detail::ctx(), der.data(), &len, &sig))
        throw std::runtime_error("DER encoding failed");
    der.resize(len);
    return der;
}

// 33-byte compressed public key for the private key (Bitcoin scripts).
inline std::array<uint8_t, 33> pubkeyCompressed(const std::string& privHex) {
    const Seckey key = detail::seckeyFromHex(privHex);
    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_create(detail::ctx(), &pk, key.data()))
        throw std::runtime_error("public key derivation failed");
    std::array<uint8_t, 33> out{};
    size_t len = out.size();
    secp256k1_ec_pubkey_serialize(detail::ctx(), out.data(), &len, &pk, SECP256K1_EC_COMPRESSED);
    return out;
}

// 64-byte uncompressed public key (no 0x04 prefix) — Ethereum addresses are
// keccak256 of this, last 20 bytes.
inline std::array<uint8_t, 64> pubkeyUncompressedXY(const std::string& privHex) {
    const Seckey key = detail::seckeyFromHex(privHex);
    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_create(detail::ctx(), &pk, key.data()))
        throw std::runtime_error("public key derivation failed");
    std::array<uint8_t, 65> full{};
    size_t len = full.size();
    secp256k1_ec_pubkey_serialize(detail::ctx(), full.data(), &len, &pk, SECP256K1_EC_UNCOMPRESSED);
    std::array<uint8_t, 64> xy{};
    std::memcpy(xy.data(), full.data() + 1, 64);  // drop the 0x04 prefix
    return xy;
}

}  // namespace rtxsend

#endif  // RTX_SEND_SIGNER_H
