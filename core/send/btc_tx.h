#ifndef RTX_SEND_BTC_TX_H
#define RTX_SEND_BTC_TX_H

// Building and signing Bitcoin transactions that spend native SegWit (P2WPKH)
// inputs — the wallet's default "bc1q..." receive address. Pays to P2WPKH,
// legacy P2PKH ("1...") or P2SH ("3...") recipients. The BIP143 sighash is
// verified against the BIP143 test vector (see tests).

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/sha.h>

#include "../bitcoin.h"  // btc::sha256, hash160, base58 (encode/decode), bech32 (encode)
#include "signer.h"

namespace rtxsend::btc {

using ::btc::hash160;
using Bytes = std::vector<uint8_t>;

inline Bytes dsha256(const Bytes& v) {
    auto a = ::btc::sha256(v.data(), v.size());
    auto b = ::btc::sha256(a.data(), a.size());
    return Bytes(b.begin(), b.end());
}

inline void putU32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline void putU64(Bytes& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline void putVarint(Bytes& b, uint64_t n) {
    if (n < 0xfd) b.push_back(static_cast<uint8_t>(n));
    else if (n <= 0xffff) { b.push_back(0xfd); b.push_back(n & 0xff); b.push_back((n >> 8) & 0xff); }
    else if (n <= 0xffffffff) { b.push_back(0xfe); putU32(b, static_cast<uint32_t>(n)); }
    else { b.push_back(0xff); putU64(b, n); }
}
inline void putBytes(Bytes& b, const Bytes& x) { b.insert(b.end(), x.begin(), x.end()); }
inline void putVarBytes(Bytes& b, const Bytes& x) {
    putVarint(b, x.size());
    putBytes(b, x);
}

// ---- address -> scriptPubKey -------------------------------------------------

namespace detail {
// BIP173 bech32 decode -> (witness version, program bytes). Supports v0 only.
inline bool bech32Decode(const std::string& addr, const std::string& hrp, int& witver, Bytes& program) {
    static const char* CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    std::string s = addr;
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t pos = s.rfind('1');
    if (pos == std::string::npos || s.substr(0, pos) != hrp) return false;
    std::vector<uint8_t> data;
    for (size_t i = pos + 1; i < s.size(); ++i) {
        const char* p = std::char_traits<char>::find(CHARSET, 32, s[i]);
        if (!p) return false;
        data.push_back(static_cast<uint8_t>(p - CHARSET));
    }
    if (data.size() < 7) return false;
    // checksum (bech32, witness v0)
    std::vector<uint8_t> values = ::btc::bech32::hrpExpand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    if (::btc::bech32::polymod(values) != 1) return false;
    witver = data[0];
    if (witver != 0) return false;  // v1+ (taproot) not supported for sending
    std::vector<uint8_t> conv;
    int acc = 0, bits = 0;
    for (size_t i = 1; i + 6 < data.size(); ++i) {
        acc = (acc << 5) | data[i];
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            conv.push_back((acc >> bits) & 0xff);
        }
    }
    if (bits >= 5 || ((acc << (8 - bits)) & 0xff)) return false;
    program = conv;
    return program.size() == 20 || program.size() == 32;
}
}  // namespace detail

// Recipient address -> output scriptPubKey. `hrp`/`p2pkh`/`p2sh` are the
// network's prefixes (mainnet: "bc", 0x00, 0x05).
inline Bytes scriptForAddress(const std::string& addr, const std::string& hrp = "bc", uint8_t p2pkh = 0x00,
                              uint8_t p2sh = 0x05) {
    int witver = 0;
    Bytes program;
    if (addr.rfind(hrp + "1", 0) == 0 && detail::bech32Decode(addr, hrp, witver, program)) {
        Bytes spk = {0x00, static_cast<uint8_t>(program.size())};  // OP_0 <program>
        putBytes(spk, program);
        return spk;
    }
    const Bytes payload = ::btc::base58CheckDecode(addr);  // version byte + hash
    const uint8_t ver = payload[0];
    const Bytes hash(payload.begin() + 1, payload.end());
    if (hash.size() != 20) throw std::runtime_error("Unsupported address");
    if (ver == p2pkh) {  // OP_DUP OP_HASH160 <20> OP_EQUALVERIFY OP_CHECKSIG
        Bytes spk = {0x76, 0xa9, 0x14};
        putBytes(spk, hash);
        spk.push_back(0x88);
        spk.push_back(0xac);
        return spk;
    }
    if (ver == p2sh) {  // OP_HASH160 <20> OP_EQUAL
        Bytes spk = {0xa9, 0x14};
        putBytes(spk, hash);
        spk.push_back(0x87);
        return spk;
    }
    throw std::runtime_error("Unrecognized Bitcoin address");
}

// ---- transaction building ----------------------------------------------------

struct Utxo {
    std::string txid;  // big-endian display order (as the APIs return it)
    uint32_t vout = 0;
    uint64_t value = 0;  // satoshis
};

struct Output {
    Bytes script;
    uint64_t value = 0;
};

inline Bytes txidToLE(const std::string& txid) {
    if (txid.size() != 64) throw std::runtime_error("Bad txid");
    Bytes b(32);
    for (int i = 0; i < 32; ++i) b[31 - i] = static_cast<uint8_t>(std::stoul(txid.substr(i * 2, 2), nullptr, 16));
    return b;
}

// BIP143 sighash for signing P2WPKH input `index` (SIGHASH_ALL). `pubkeyHash`
// is HASH160 of the input's pubkey (the scriptCode is its P2PKH form).
// `sequences` is one value per input (the wallet uses 0xffffffff for all).
inline rtxsend::Hash32 bip143Sighash(const std::vector<Utxo>& ins, const std::vector<Output>& outs, size_t index,
                                     const std::array<uint8_t, 20>& pubkeyHash, const std::vector<uint32_t>& sequences,
                                     uint32_t version = 2, uint32_t locktime = 0) {
    Bytes prevouts, seqs, outputs;
    for (size_t i = 0; i < ins.size(); ++i) {
        putBytes(prevouts, txidToLE(ins[i].txid));
        putU32(prevouts, ins[i].vout);
        putU32(seqs, sequences[i]);
    }
    for (const auto& o : outs) {
        putU64(outputs, o.value);
        putVarBytes(outputs, o.script);
    }
    const Bytes hashPrevouts = dsha256(prevouts);
    const Bytes hashSequence = dsha256(seqs);
    const Bytes hashOutputs = dsha256(outputs);

    Bytes pre;
    putU32(pre, version);
    putBytes(pre, hashPrevouts);
    putBytes(pre, hashSequence);
    putBytes(pre, txidToLE(ins[index].txid));
    putU32(pre, ins[index].vout);
    Bytes scriptCode = {0x76, 0xa9, 0x14};  // P2PKH of the input pubkey
    scriptCode.insert(scriptCode.end(), pubkeyHash.begin(), pubkeyHash.end());
    scriptCode.push_back(0x88);
    scriptCode.push_back(0xac);
    putVarBytes(pre, scriptCode);
    putU64(pre, ins[index].value);
    putU32(pre, sequences[index]);
    putBytes(pre, hashOutputs);
    putU32(pre, locktime);
    putU32(pre, 1);  // SIGHASH_ALL

    const Bytes h = dsha256(pre);
    rtxsend::Hash32 out{};
    std::copy(h.begin(), h.end(), out.begin());
    return out;
}

struct Signed {
    std::string rawHex;
    std::string txid;  // display order
};

// Signs every (P2WPKH) input with `privHex` and serializes the SegWit tx.
inline Signed buildSigned(const std::vector<Utxo>& ins, const std::vector<Output>& outs, const std::string& privHex,
                          uint32_t version = 2, uint32_t locktime = 0) {
    const auto pub = pubkeyCompressed(privHex);
    const auto pkh = hash160(Bytes(pub.begin(), pub.end()));

    // Non-witness body (used for both the txid and the final serialization).
    auto serialize = [&](bool witness) {
        Bytes tx;
        putU32(tx, version);
        if (witness) {
            tx.push_back(0x00);  // SegWit marker
            tx.push_back(0x01);  // flag
        }
        putVarint(tx, ins.size());
        for (const auto& in : ins) {
            putBytes(tx, txidToLE(in.txid));
            putU32(tx, in.vout);
            putVarint(tx, 0);  // empty scriptSig for native SegWit
            putU32(tx, 0xffffffff);
        }
        putVarint(tx, outs.size());
        for (const auto& o : outs) {
            putU64(tx, o.value);
            putVarBytes(tx, o.script);
        }
        if (witness) {
            const std::vector<uint32_t> seqs(ins.size(), 0xffffffff);
            for (size_t i = 0; i < ins.size(); ++i) {
                const rtxsend::Hash32 h = bip143Sighash(ins, outs, i, pkh, seqs, version, locktime);
                Bytes sig = signDer(h, privHex);
                sig.push_back(0x01);  // SIGHASH_ALL
                putVarint(tx, 2);     // two witness items: signature, pubkey
                putVarBytes(tx, sig);
                putVarBytes(tx, Bytes(pub.begin(), pub.end()));
            }
        }
        putU32(tx, locktime);
        return tx;
    };

    const Bytes full = serialize(true);
    const Bytes stripped = serialize(false);  // witness excluded -> txid
    const Bytes txidLE = dsha256(stripped);

    static const char* hex = "0123456789abcdef";
    auto toHex = [](const Bytes& b) {
        std::string s;
        for (uint8_t x : b) {
            s.push_back(hex[x >> 4]);
            s.push_back(hex[x & 0x0f]);
        }
        return s;
    };
    Signed out;
    out.rawHex = toHex(full);
    Bytes disp(txidLE.rbegin(), txidLE.rend());
    out.txid = toHex(disp);
    return out;
}

// Greedy coin selection: smallest set of UTXOs covering amount + fee. Returns
// the chosen inputs and the change (which the caller may drop if dust).
struct Selection {
    std::vector<Utxo> inputs;
    uint64_t total = 0;
};
inline Selection selectCoins(std::vector<Utxo> utxos, uint64_t target) {
    std::sort(utxos.begin(), utxos.end(), [](const Utxo& a, const Utxo& b) { return a.value > b.value; });
    Selection s;
    for (const auto& u : utxos) {
        s.inputs.push_back(u);
        s.total += u.value;
        if (s.total >= target) break;
    }
    return s;
}

// Virtual size (vbytes) of a P2WPKH spend with `nIn` inputs and `nOut` outputs,
// for fee estimation (BIP141 weight / 4, rounded up).
inline uint64_t estimateVsize(size_t nIn, size_t nOut) {
    const uint64_t base = 10 + nIn * 41 + nOut * 31;      // version+locktime+counts + inputs + outputs
    const uint64_t witness = 2 + nIn * 107;               // marker+flag + (sig+pubkey) per input
    const uint64_t weight = base * 4 + witness;
    return (weight + 3) / 4;
}

}  // namespace rtxsend::btc

#endif  // RTX_SEND_BTC_TX_H
