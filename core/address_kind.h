#ifndef RTX_ADDRESS_KIND_H
#define RTX_ADDRESS_KIND_H

// Recognizes which network a public address belongs to (for watch-only
// wallets and balance lookups). Checksummed formats are verified, so a Solana
// address that happens to start with "1" or "3" isn't taken for Bitcoin.

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "bitcoin.h"
#include "solana.h"
#include "tron.h"

namespace rtxaddr {

enum class Kind { Unknown, Evm, BtcSegwit, BtcLegacy, Tron, Solana };

inline Kind classify(const std::string& a) {
    if (a.size() == 42 && (a.rfind("0x", 0) == 0 || a.rfind("0X", 0) == 0)) {
        bool hex = true;
        for (size_t i = 2; i < a.size(); ++i) hex = hex && std::isxdigit(static_cast<unsigned char>(a[i]));
        if (hex) return Kind::Evm;
    }
    if (a.rfind("bc1", 0) == 0) return Kind::BtcSegwit;
    if (tron::isAddress(a)) return Kind::Tron;
    try {
        const std::vector<uint8_t> p = btc::base58CheckDecode(a);
        if (p.size() == 21 && (p[0] == 0x00 || p[0] == 0x05)) return Kind::BtcLegacy;  // P2PKH "1…" / P2SH "3…"
    } catch (const std::exception&) {
    }
    if (sol::isAddress(a)) return Kind::Solana;
    return Kind::Unknown;
}

}  // namespace rtxaddr

#endif  // RTX_ADDRESS_KIND_H
