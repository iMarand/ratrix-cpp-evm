#ifndef RTX_SEND_RLP_H
#define RTX_SEND_RLP_H

// Recursive Length Prefix encoding (Ethereum yellow paper, appendix B), just
// the encoder side needed to serialize transactions.

#include <cstdint>
#include <vector>

#include "bignum.h"

namespace rtxsend::rlp {

inline Bytes prefix(size_t len, uint8_t offset) {
    if (len < 56) return {static_cast<uint8_t>(offset + len)};
    Bytes lenBytes;
    for (size_t l = len; l; l >>= 8) lenBytes.insert(lenBytes.begin(), static_cast<uint8_t>(l & 0xff));
    Bytes out{static_cast<uint8_t>(offset + 55 + lenBytes.size())};
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
    return out;
}

inline Bytes bytes(const Bytes& b) {
    if (b.size() == 1 && b[0] < 0x80) return b;
    Bytes out = prefix(b.size(), 0x80);
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

// Integers are big-endian with no leading zeros; zero is the empty string.
inline Bytes integer(const Big& v) { return bytes(v.toBytes()); }
inline Bytes integer(uint64_t v) { return integer(Big(v)); }

inline Bytes list(const std::vector<Bytes>& encodedItems) {
    Bytes payload;
    for (const auto& item : encodedItems) payload.insert(payload.end(), item.begin(), item.end());
    Bytes out = prefix(payload.size(), 0xc0);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace rtxsend::rlp

#endif  // RTX_SEND_RLP_H
