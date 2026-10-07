#ifndef RTX_SEND_BIGNUM_H
#define RTX_SEND_BIGNUM_H

// Exact integer math for token amounts (wei values exceed 64 bits) on top of
// OpenSSL BIGNUM, plus decimal <-> base-unit conversion. No floating point is
// ever used for amounts that end up in a transaction.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <openssl/bn.h>
#include <openssl/crypto.h>

namespace rtxsend {

using Bytes = std::vector<uint8_t>;

class Big {
public:
    Big() : bn_(BN_new()) { check(); }
    explicit Big(uint64_t v) : Big() {
        if (!BN_set_word(bn_, static_cast<BN_ULONG>(v))) throw std::runtime_error("bignum: set failed");
    }
    Big(const Big& o) : bn_(BN_dup(o.bn_)) { check(); }
    Big(Big&& o) noexcept : bn_(o.bn_) { o.bn_ = nullptr; }
    Big& operator=(Big o) noexcept {
        std::swap(bn_, o.bn_);
        return *this;
    }
    ~Big() { BN_free(bn_); }

    // Parses hex with optional 0x; "" / "0x" is zero. Rejects any non-hex character.
    static Big fromHex(std::string h) {
        if (h.rfind("0x", 0) == 0 || h.rfind("0X", 0) == 0) h = h.substr(2);
        Big b;
        if (h.empty()) return b;
        for (char c : h)
            if (!std::isxdigit(static_cast<unsigned char>(c))) throw std::runtime_error("bignum: bad hex '" + h + "'");
        BIGNUM* raw = b.bn_;
        if (BN_hex2bn(&raw, h.c_str()) != static_cast<int>(h.size())) throw std::runtime_error("bignum: bad hex");
        return b;
    }
    static Big fromDec(const std::string& d) {
        Big b;
        if (d.empty()) throw std::runtime_error("bignum: empty number");
        for (char c : d)
            if (c < '0' || c > '9') throw std::runtime_error("bignum: bad decimal '" + d + "'");
        BIGNUM* raw = b.bn_;
        if (BN_dec2bn(&raw, d.c_str()) != static_cast<int>(d.size())) throw std::runtime_error("bignum: bad decimal");
        return b;
    }
    static Big fromBytes(const uint8_t* p, size_t n) {
        Big b;
        if (!BN_bin2bn(p, static_cast<int>(n), b.bn_)) throw std::runtime_error("bignum: bin2bn failed");
        return b;
    }

    std::string toDec() const {
        char* s = BN_bn2dec(bn_);
        std::string out = s ? s : "0";
        OPENSSL_free(s);
        return out;
    }
    std::string toHex() const {  // lowercase, no leading zeros, "0" for zero
        if (BN_is_zero(bn_)) return "0";
        char* s = BN_bn2hex(bn_);
        std::string out = s ? s : "0";
        OPENSSL_free(s);
        for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const size_t nz = out.find_first_not_of('0');
        return nz == std::string::npos ? "0" : out.substr(nz);
    }
    std::string toQuantity() const { return "0x" + toHex(); }  // JSON-RPC quantity
    Bytes toBytes() const {  // minimal big-endian; empty for zero (RLP integer form)
        Bytes out(BN_num_bytes(bn_));
        if (!out.empty()) BN_bn2bin(bn_, out.data());
        return out;
    }
    Bytes toBytesPadded(size_t n) const {
        if (static_cast<size_t>(BN_num_bytes(bn_)) > n) throw std::runtime_error("bignum: value too large");
        Bytes out(n);
        BN_bn2binpad(bn_, out.data(), static_cast<int>(n));
        return out;
    }
    uint64_t toU64() const {
        if (BN_num_bits(bn_) > 64) throw std::runtime_error("bignum: value exceeds 64 bits");
        const Bytes b = toBytesPadded(8);
        uint64_t v = 0;
        for (uint8_t x : b) v = (v << 8) | x;
        return v;
    }

    bool isZero() const { return BN_is_zero(bn_); }
    int cmp(const Big& o) const { return BN_cmp(bn_, o.bn_); }
    bool operator<(const Big& o) const { return cmp(o) < 0; }
    bool operator>(const Big& o) const { return cmp(o) > 0; }
    bool operator<=(const Big& o) const { return cmp(o) <= 0; }
    bool operator>=(const Big& o) const { return cmp(o) >= 0; }
    bool operator==(const Big& o) const { return cmp(o) == 0; }

    Big operator+(const Big& o) const {
        Big r;
        if (!BN_add(r.bn_, bn_, o.bn_)) throw std::runtime_error("bignum: add failed");
        return r;
    }
    Big operator-(const Big& o) const {  // never negative: amounts only
        if (*this < o) throw std::runtime_error("bignum: negative result");
        Big r;
        if (!BN_sub(r.bn_, bn_, o.bn_)) throw std::runtime_error("bignum: sub failed");
        return r;
    }
    Big operator*(const Big& o) const {
        Big r;
        BN_CTX* ctx = BN_CTX_new();
        const int ok = ctx && BN_mul(r.bn_, bn_, o.bn_, ctx);
        BN_CTX_free(ctx);
        if (!ok) throw std::runtime_error("bignum: mul failed");
        return r;
    }
    // Integer division rounding up (fee buffers).
    Big divCeil(uint64_t d) const {
        Big q, rem;
        Big div(d);
        BN_CTX* ctx = BN_CTX_new();
        const int ok = ctx && BN_div(q.bn_, rem.bn_, bn_, div.bn_, ctx);
        BN_CTX_free(ctx);
        if (!ok) throw std::runtime_error("bignum: div failed");
        return rem.isZero() ? q : q + Big(1);
    }

    const BIGNUM* raw() const { return bn_; }

private:
    void check() const {
        if (!bn_) throw std::runtime_error("bignum: allocation failed");
    }
    BIGNUM* bn_;
};

// "1.25" with 18 decimals -> 1250000000000000000. Rejects signs, exponents,
// empty input and more fractional digits than the asset supports.
inline Big parseUnits(const std::string& text, int decimals) {
    std::string s;
    for (char c : text)
        if (c != ',' && c != ' ') s.push_back(c);  // tolerate grouping
    if (s.empty()) throw std::runtime_error("Enter an amount");
    const size_t dot = s.find('.');
    std::string whole = dot == std::string::npos ? s : s.substr(0, dot);
    std::string frac = dot == std::string::npos ? std::string() : s.substr(dot + 1);
    if (whole.empty()) whole = "0";
    if (dot != std::string::npos && frac.empty()) throw std::runtime_error("Amount looks incomplete");
    for (char c : whole + frac)
        if (c < '0' || c > '9') throw std::runtime_error("Amount must be a plain number");
    if (static_cast<int>(frac.size()) > decimals)
        throw std::runtime_error("Too many decimal places (max " + std::to_string(decimals) + ")");
    frac.append(static_cast<size_t>(decimals) - frac.size(), '0');
    std::string digits = whole + frac;
    const size_t nz = digits.find_first_not_of('0');
    digits = nz == std::string::npos ? "0" : digits.substr(nz);
    return Big::fromDec(digits);
}

// 1250000000000000000 with 18 decimals -> "1.25" (trailing zeros trimmed, at
// least `minDp` decimals, truncated to `maxDp`).
inline std::string formatUnits(const Big& v, int decimals, int maxDp = 18, int minDp = 2) {
    std::string d = v.toDec();
    if (decimals == 0) return d;
    if (static_cast<int>(d.size()) <= decimals) d.insert(0, static_cast<size_t>(decimals) - d.size() + 1, '0');
    std::string whole = d.substr(0, d.size() - decimals);
    std::string frac = d.substr(d.size() - decimals);
    if (static_cast<int>(frac.size()) > maxDp) frac.resize(static_cast<size_t>(maxDp));
    while (static_cast<int>(frac.size()) > minDp && frac.back() == '0') frac.pop_back();
    return frac.empty() ? whole : whole + "." + frac;
}

}  // namespace rtxsend

#endif  // RTX_SEND_BIGNUM_H
