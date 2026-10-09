#ifndef RTX_SEND_TRON_TX_H
#define RTX_SEND_TRON_TX_H

// Building and signing Tron transactions locally: the protobuf `Transaction`
// for a TRX transfer (TransferContract) or a TRC-20 call
// (TriggerSmartContract). A node only supplies a recent block for the TaPoS
// reference and relays the signed bytes; it never builds what we sign. The
// encoding was checked byte-for-byte against java-tron's own
// createtransaction / triggersmartcontract output.

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/sha.h>

#include "../keccak256.h"
#include "../tron.h"
#include "bignum.h"
#include "signer.h"

namespace rtxsend::tron {

// Minimal protobuf (proto3) writer: just the wire types a Transaction uses.
namespace pb {
inline void varint(Bytes& b, uint64_t v) {
    while (v >= 0x80) {
        b.push_back(static_cast<uint8_t>(v) | 0x80);
        v >>= 7;
    }
    b.push_back(static_cast<uint8_t>(v));
}
inline void key(Bytes& b, int field, int wireType) { varint(b, (static_cast<uint64_t>(field) << 3) | wireType); }
inline void uint(Bytes& b, int field, uint64_t v) {  // proto3 leaves zero values out
    if (v == 0) return;
    key(b, field, 0);
    varint(b, v);
}
inline void bytes(Bytes& b, int field, const Bytes& v) {
    key(b, field, 2);
    varint(b, v.size());
    b.insert(b.end(), v.begin(), v.end());
}
inline void text(Bytes& b, int field, const std::string& s) { bytes(b, field, Bytes(s.begin(), s.end())); }
}  // namespace pb

// A recent block, referenced by the transaction (TaPoS) so it can only be
// included on this chain and only for a limited time.
struct BlockRef {
    uint64_t number = 0;
    Bytes id;                // 32-byte block id
    uint64_t timestampMs = 0;
};

constexpr uint64_t kExpirationMs = 60 * 1000;  // the window java-tron's own builder uses

// 21-byte on-chain form (0x41 + account id) of a "T..." address.
inline Bytes address(const std::string& t) {
    const std::vector<uint8_t> id = ::tron::accountId(t);
    Bytes a{::tron::kPrefix};
    a.insert(a.end(), id.begin(), id.end());
    return a;
}

// The Tron address a private key controls (keccak of the public key, like an
// EVM address), to check the key matches the wallet before signing.
inline std::string addressOf(const std::string& privHex) {
    const std::array<uint8_t, 64> xy = pubkeyUncompressedXY(privHex);
    const std::string h = kecca256::Keccak256::getHexFromBytes(std::vector<unsigned char>(xy.begin(), xy.end()));
    return ::tron::fromEvmAddress(h.substr(h.size() - 40));
}

namespace detail {
inline Bytes contract(uint64_t type, const char* name, const Bytes& value) {
    Bytes any;  // google.protobuf.Any
    pb::text(any, 1, std::string("type.googleapis.com/protocol.") + name);
    pb::bytes(any, 2, value);
    Bytes c;
    pb::uint(c, 1, type);
    pb::bytes(c, 2, any);
    return c;
}
}  // namespace detail

// TransferContract (type 1): send `amountSun` TRX.
inline Bytes transferContract(const std::string& from, const std::string& to, uint64_t amountSun) {
    Bytes v;
    pb::bytes(v, 1, address(from));
    pb::bytes(v, 2, address(to));
    pb::uint(v, 3, amountSun);
    return detail::contract(1, "TransferContract", v);
}

// TriggerSmartContract (type 31): call `contractAddr` with ABI `data`.
inline Bytes triggerContract(const std::string& from, const std::string& contractAddr, const Bytes& data) {
    Bytes v;
    pb::bytes(v, 1, address(from));
    pb::bytes(v, 2, address(contractAddr));
    pb::bytes(v, 4, data);
    return detail::contract(31, "TriggerSmartContract", v);
}

struct Unsigned {
    Bytes raw;    // serialized Transaction.raw
    Hash32 txid;  // sha256(raw): the transaction id, and what gets signed
};

// Transaction.raw: ref_block_bytes(1), ref_block_hash(4), expiration(8),
// contract(11), timestamp(14), fee_limit(18), in field order as java-tron
// writes them.
inline Unsigned build(const BlockRef& ref, const Bytes& contract, uint64_t nowMs, uint64_t feeLimitSun = 0) {
    if (ref.id.size() != 32) throw std::runtime_error("Tron: bad block id");
    Unsigned u;
    pb::bytes(u.raw, 1, Bytes{static_cast<uint8_t>(ref.number >> 8), static_cast<uint8_t>(ref.number)});
    pb::bytes(u.raw, 4, Bytes(ref.id.begin() + 8, ref.id.begin() + 16));
    pb::uint(u.raw, 8, ref.timestampMs + kExpirationMs);
    pb::bytes(u.raw, 11, contract);
    pb::uint(u.raw, 14, nowMs);
    pb::uint(u.raw, 18, feeLimitSun);
    SHA256(u.raw.data(), u.raw.size(), u.txid.data());
    return u;
}

// Serialized Transaction{raw_data(1), signature(2)}. The signature is r || s
// || v with v = recovery id + 27, as TronWeb produces.
inline Bytes withSignature(const Unsigned& u, const std::array<uint8_t, 65>& sig) {
    Bytes tx;
    pb::bytes(tx, 1, u.raw);
    pb::bytes(tx, 2, Bytes(sig.begin(), sig.end()));
    return tx;
}

inline Bytes sign(const Unsigned& u, const std::string& privHex) {
    const RecoverableSig s = signRecoverable(u.txid, privHex);
    std::array<uint8_t, 65> sig{};
    std::copy(s.compact.begin(), s.compact.end(), sig.begin());
    sig[64] = static_cast<uint8_t>(s.recid + 27);
    return withSignature(u, sig);
}

// Bandwidth the network charges: the signed transaction's size plus 64 bytes
// it reserves for the execution result. Signatures have a fixed length, so a
// zero placeholder measures an unsigned transaction exactly.
inline uint64_t bandwidth(const Unsigned& u) { return withSignature(u, {}).size() + 64; }

inline std::string hex(const uint8_t* p, size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(hx[p[i] >> 4]);
        out.push_back(hx[p[i] & 0x0f]);
    }
    return out;
}

}  // namespace rtxsend::tron

#endif  // RTX_SEND_TRON_TX_H
