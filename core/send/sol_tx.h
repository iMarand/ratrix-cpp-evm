#ifndef RTX_SEND_SOL_TX_H
#define RTX_SEND_SOL_TX_H

// Building and signing Solana transactions locally (legacy message format):
// a SOL transfer (System program) or an SPL token transfer (Token program,
// creating the recipient's token account when needed), each with compute-
// budget instructions for a small priority fee. Signed with ed25519 via
// OpenSSL. A node only supplies a recent blockhash and relays the result.

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include "../solana.h"
#include "bignum.h"

namespace rtxsend::sol {

using Key = ::sol::Key;

struct AccountMeta {
    Key key;
    bool signer = false;
    bool writable = false;
};

struct Instruction {
    Key program;
    std::vector<AccountMeta> accounts;
    Bytes data;
};

namespace detail {
// Solana's compact-u16 length prefix: 7 bits per byte, low bits first.
inline void shortvec(Bytes& b, size_t n) {
    if (n > 0xffff) throw std::runtime_error("Solana: list too long");
    do {
        uint8_t byte = n & 0x7f;
        n >>= 7;
        if (n) byte |= 0x80;
        b.push_back(byte);
    } while (n);
}
inline void le(Bytes& b, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
}  // namespace detail

// ------------------------------ instructions -------------------------------

inline Instruction setComputeUnitLimit(uint32_t units) {
    Instruction ix{::sol::decode(::sol::kComputeBudgetProgram), {}, {2}};
    detail::le(ix.data, units, 4);
    return ix;
}

inline Instruction setComputeUnitPrice(uint64_t microLamports) {
    Instruction ix{::sol::decode(::sol::kComputeBudgetProgram), {}, {3}};
    detail::le(ix.data, microLamports, 8);
    return ix;
}

// System program Transfer (index 2): move lamports between system accounts.
inline Instruction transfer(const Key& from, const Key& to, uint64_t lamports) {
    Instruction ix{::sol::decode(::sol::kSystemProgram), {{from, true, true}, {to, false, true}}, {2, 0, 0, 0}};
    detail::le(ix.data, lamports, 8);
    return ix;
}

// Associated Token Account program CreateIdempotent (1): opens `owner`'s token
// account for `mint`, paid by `payer`; a no-op if it already exists.
inline Instruction createTokenAccount(const Key& payer, const Key& ata, const Key& owner, const Key& mint) {
    return {::sol::decode(::sol::kAssociatedTokenProgram),
            {{payer, true, true},
             {ata, false, true},
             {owner, false, false},
             {mint, false, false},
             {::sol::decode(::sol::kSystemProgram), false, false},
             {::sol::decode(::sol::kTokenProgram), false, false}},
            {1}};
}

// Token program TransferChecked (12): the mint and decimals are checked on
// chain, so a wrong token or scale fails instead of moving funds.
inline Instruction transferChecked(const Key& source, const Key& mint, const Key& dest, const Key& owner,
                                   uint64_t amount, uint8_t decimals) {
    Instruction ix{::sol::decode(::sol::kTokenProgram),
                   {{source, false, true}, {mint, false, false}, {dest, false, true}, {owner, true, false}},
                   {12}};
    detail::le(ix.data, amount, 8);
    ix.data.push_back(decimals);
    return ix;
}

// -------------------------------- message ----------------------------------

// Compiles a legacy message: header, account keys (fee payer first, then
// signer-writable, signer-readonly, writable, readonly; each key once with its
// flags merged), recent blockhash, and the instructions referring to keys by
// index.
inline Bytes compileMessage(const Key& feePayer, const std::vector<Instruction>& ixs, const Key& blockhash) {
    std::vector<AccountMeta> keys;
    auto add = [&](const Key& k, bool signer, bool writable) {
        for (auto& m : keys)
            if (m.key == k) {
                m.signer |= signer;
                m.writable |= writable;
                return;
            }
        keys.push_back({k, signer, writable});
    };
    add(feePayer, true, true);
    for (const auto& ix : ixs) {
        for (const auto& m : ix.accounts) add(m.key, m.signer, m.writable);
        add(ix.program, false, false);
    }
    auto rank = [](const AccountMeta& m) { return m.signer ? (m.writable ? 0 : 1) : (m.writable ? 2 : 3); };
    std::stable_sort(keys.begin(), keys.end(), [&](const AccountMeta& a, const AccountMeta& b) { return rank(a) < rank(b); });
    auto indexOf = [&](const Key& k) {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i].key == k) return static_cast<uint8_t>(i);
        throw std::runtime_error("Solana: account missing from message");
    };

    Bytes msg;
    msg.push_back(static_cast<uint8_t>(std::count_if(keys.begin(), keys.end(), [](const AccountMeta& m) { return m.signer; })));
    msg.push_back(static_cast<uint8_t>(std::count_if(keys.begin(), keys.end(), [](const AccountMeta& m) { return m.signer && !m.writable; })));
    msg.push_back(static_cast<uint8_t>(std::count_if(keys.begin(), keys.end(), [](const AccountMeta& m) { return !m.signer && !m.writable; })));
    detail::shortvec(msg, keys.size());
    for (const auto& m : keys) msg.insert(msg.end(), m.key.begin(), m.key.end());
    msg.insert(msg.end(), blockhash.begin(), blockhash.end());
    detail::shortvec(msg, ixs.size());
    for (const auto& ix : ixs) {
        msg.push_back(indexOf(ix.program));
        detail::shortvec(msg, ix.accounts.size());
        for (const auto& m : ix.accounts) msg.push_back(indexOf(m.key));
        detail::shortvec(msg, ix.data.size());
        msg.insert(msg.end(), ix.data.begin(), ix.data.end());
    }
    return msg;
}

// ------------------------------- signing -----------------------------------

using Signature = std::array<uint8_t, 64>;

// ed25519 signature of the message bytes with a 32-byte private key.
inline Signature signMessage(const Bytes& message, const std::array<uint8_t, 32>& key) {
    EVP_PKEY* pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, key.data(), key.size());
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    Signature sig{};
    size_t len = sig.size();
    const bool ok = pk && ctx && EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pk) == 1 &&
                    EVP_DigestSign(ctx, sig.data(), &len, message.data(), message.size()) == 1 && len == sig.size();
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pk);
    if (!ok) throw std::runtime_error("Solana signing failed");
    return sig;
}

// Wire transaction: signature count, signatures, message. A zero signature
// stands in for an unsigned transaction (simulation, size, fee lookups).
inline Bytes serialize(const Bytes& message, const Signature& sig = {}) {
    Bytes tx;
    detail::shortvec(tx, 1);
    tx.insert(tx.end(), sig.begin(), sig.end());
    tx.insert(tx.end(), message.begin(), message.end());
    return tx;
}

}  // namespace rtxsend::sol

#endif  // RTX_SEND_SOL_TX_H
