#ifndef RTX_SEND_EVM_H
#define RTX_SEND_EVM_H

// Building and signing EVM transactions (Ethereum, BNB Smart Chain). Legacy
// type-0 transactions with EIP-155 replay protection, which every EVM node
// accepts. The signing was verified against the canonical EIP-155 vector.

#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "../keccak256.h"
#include "bignum.h"
#include "rlp.h"
#include "signer.h"

namespace rtxsend::evm {

// Parses "0x"-prefixed hex of exactly `n` bytes (e.g. a 20-byte address).
inline Bytes parseAddress(const std::string& addr) {
    std::string h = addr;
    if (h.rfind("0x", 0) == 0 || h.rfind("0X", 0) == 0) h = h.substr(2);
    if (h.size() != 40) throw std::runtime_error("An EVM address must be 40 hex characters after 0x");
    Bytes out(20);
    for (size_t i = 0; i < 20; ++i) {
        auto hx = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("EVM address is not valid hex");
        };
        out[i] = static_cast<uint8_t>((hx(h[i * 2]) << 4) | hx(h[i * 2 + 1]));
    }
    return out;
}

// EIP-55 checksum: a mixed-case address is only valid if its capitalisation
// matches keccak256(lowercase-hex). All-lowercase / all-uppercase is accepted
// (no checksum applied). Returns true if acceptable.
inline bool checksumOk(const std::string& addr) {
    std::string h = addr;
    if (h.rfind("0x", 0) == 0) h = h.substr(2);
    if (h.size() != 40) return false;
    bool hasUpper = false, hasLower = false;
    for (char c : h) {
        if (c >= 'A' && c <= 'F') hasUpper = true;
        if (c >= 'a' && c <= 'f') hasLower = true;
    }
    if (!(hasUpper && hasLower)) return true;  // not checksummed; accept
    std::string lower = h;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string hash = kecca256::Keccak256::getHexFromBytes(
        std::vector<unsigned char>(lower.begin(), lower.end()));
    for (size_t i = 0; i < 40; ++i) {
        const int nibble = hash[i] >= 'a' ? hash[i] - 'a' + 10 : hash[i] - '0';
        const bool upper = nibble >= 8;
        if (std::isalpha(static_cast<unsigned char>(h[i])) && (std::isupper(static_cast<unsigned char>(h[i])) != upper))
            return false;
    }
    return true;
}

// ABI-encodes transfer(address,uint256) for an ERC-20 token.
inline Bytes erc20Transfer(const std::string& to, const Big& amount) {
    Bytes data = {0xa9, 0x05, 0x9c, 0xbb};  // keccak256("transfer(address,uint256)")[:4]
    const Bytes addr = parseAddress(to);
    data.insert(data.end(), 12, 0);  // left-pad address to 32 bytes
    data.insert(data.end(), addr.begin(), addr.end());
    const Bytes amt = amount.toBytesPadded(32);
    data.insert(data.end(), amt.begin(), amt.end());
    return data;
}

struct Tx {
    Big nonce, gasPrice, gasLimit, value;
    Bytes to;    // 20 bytes
    Bytes data;  // empty for a plain transfer; ERC-20 call for tokens
    uint64_t chainId = 1;
};

// Returns the "0x"-prefixed raw signed transaction for eth_sendRawTransaction.
inline std::string buildSigned(const Tx& tx, const std::string& privHex) {
    const std::vector<Bytes> base = {rlp::integer(tx.nonce),    rlp::integer(tx.gasPrice), rlp::integer(tx.gasLimit),
                                     rlp::bytes(tx.to),          rlp::integer(tx.value),    rlp::bytes(tx.data)};

    // EIP-155 signing payload: base + [chainId, 0, 0].
    std::vector<Bytes> toSign = base;
    toSign.push_back(rlp::integer(tx.chainId));
    toSign.push_back(rlp::integer(Big(0)));
    toSign.push_back(rlp::integer(Big(0)));
    const Bytes enc = rlp::list(toSign);

    Hash32 h{};
    const std::string hh =
        kecca256::Keccak256::getHexFromBytes(std::vector<unsigned char>(enc.begin(), enc.end()));
    for (int i = 0; i < 32; ++i) h[i] = static_cast<uint8_t>(std::stoul(hh.substr(i * 2, 2), nullptr, 16));

    const RecoverableSig sig = signRecoverable(h, privHex);
    const uint64_t v = static_cast<uint64_t>(sig.recid) + tx.chainId * 2 + 35;  // EIP-155
    Bytes r(sig.compact.begin(), sig.compact.begin() + 32);
    Bytes s(sig.compact.begin() + 32, sig.compact.end());

    std::vector<Bytes> signed_ = base;
    signed_.push_back(rlp::integer(Big(v)));
    signed_.push_back(rlp::bytes(r));
    signed_.push_back(rlp::bytes(s));
    const Bytes out = rlp::list(signed_);

    static const char* hex = "0123456789abcdef";
    std::string hexOut = "0x";
    for (uint8_t b : out) {
        hexOut.push_back(hex[b >> 4]);
        hexOut.push_back(hex[b & 0x0f]);
    }
    return hexOut;
}

}  // namespace rtxsend::evm

#endif  // RTX_SEND_EVM_H
