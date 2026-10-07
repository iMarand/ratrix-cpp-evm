#ifndef RTX_RIPEMD160_H
#define RTX_RIPEMD160_H

// Self-contained RIPEMD-160 (public-domain style implementation).
//
// Why bundled instead of OpenSSL: in OpenSSL 3.x RIPEMD-160 lives in the
// "legacy" provider, so EVP_MD_fetch("RIPEMD160") fails unless the host has the
// legacy provider loaded. Bitcoin's HASH160 = RIPEMD160(SHA256(x)) must always
// work, so we carry a small, dependency-free implementation here.

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>

namespace rmd160 {

inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline void compress(uint32_t h[5], const uint8_t block[64]) {
    auto f1 = [](uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; };
    auto f2 = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); };
    auto f3 = [](uint32_t x, uint32_t y, uint32_t z) { return (x | ~y) ^ z; };
    auto f4 = [](uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); };
    auto f5 = [](uint32_t x, uint32_t y, uint32_t z) { return x ^ (y | ~z); };

    static const int rL[80] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
        3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,
        1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
        4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13};
    static const int rR[80] = {
        5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,
        6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
        15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,
        8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
        12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11};
    static const int sL[80] = {
        11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,
        7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
        11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,
        11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
        9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6};
    static const int sR[80] = {
        8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,
        9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
        9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,
        15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
        8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11};
    static const uint32_t kL[5] = {0x00000000u,0x5a827999u,0x6ed9eba1u,0x8f1bbcdcu,0xa953fd4eu};
    static const uint32_t kR[5] = {0x50a28be6u,0x5c4dd124u,0x6d703ef3u,0x7a6d76e9u,0x00000000u};

    uint32_t X[16];
    for (int i = 0; i < 16; ++i)
        X[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);

    uint32_t aL=h[0],bL=h[1],cL=h[2],dL=h[3],eL=h[4];
    uint32_t aR=h[0],bR=h[1],cR=h[2],dR=h[3],eR=h[4];

    for (int j = 0; j < 80; ++j) {
        int round = j / 16;
        uint32_t t;
        uint32_t fL, fR;
        switch (round) {
            case 0: fL=f1(bL,cL,dL); fR=f5(bR,cR,dR); break;
            case 1: fL=f2(bL,cL,dL); fR=f4(bR,cR,dR); break;
            case 2: fL=f3(bL,cL,dL); fR=f3(bR,cR,dR); break;
            case 3: fL=f4(bL,cL,dL); fR=f2(bR,cR,dR); break;
            default:fL=f5(bL,cL,dL); fR=f1(bR,cR,dR); break;
        }
        t = rol(aL + fL + X[rL[j]] + kL[round], sL[j]) + eL;
        aL=eL; eL=dL; dL=rol(cL,10); cL=bL; bL=t;
        t = rol(aR + fR + X[rR[j]] + kR[round], sR[j]) + eR;
        aR=eR; eR=dR; dR=rol(cR,10); cR=bR; bR=t;
    }
    uint32_t tmp = h[1] + cL + dR;
    h[1] = h[2] + dL + eR;
    h[2] = h[3] + eL + aR;
    h[3] = h[4] + aL + bR;
    h[4] = h[0] + bL + cR;
    h[0] = tmp;
}

// Returns the 20-byte RIPEMD-160 digest of `data`.
inline std::array<uint8_t, 20> hash(const uint8_t* data, size_t len) {
    uint32_t h[5] = {0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
    size_t i = 0;
    uint8_t block[64];
    while (len - i >= 64) { compress(h, data + i); i += 64; }

    size_t rem = len - i;
    std::memset(block, 0, 64);
    std::memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        compress(h, block);
        std::memset(block, 0, 64);
    }
    uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; ++k) block[56 + k] = (uint8_t)(bits >> (8 * k));
    compress(h, block);

    std::array<uint8_t, 20> out{};
    for (int k = 0; k < 5; ++k) {
        out[k*4]   = (uint8_t)(h[k]);
        out[k*4+1] = (uint8_t)(h[k] >> 8);
        out[k*4+2] = (uint8_t)(h[k] >> 16);
        out[k*4+3] = (uint8_t)(h[k] >> 24);
    }
    return out;
}

inline std::array<uint8_t, 20> hash(const std::vector<uint8_t>& v) {
    return hash(v.data(), v.size());
}

}  // namespace rmd160

#endif  // RTX_RIPEMD160_H
