#ifndef KECCAK256_H
#define KECCAK256_H

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/evp.h>

namespace kecca256 {

class Keccak256 {
private:
    static constexpr size_t kHashSize = 32;
    static constexpr size_t kRate = 136; // Keccak-256 rate in bytes (1088 bits)

    static uint64_t rotl64(uint64_t value, int shift) {
        return (value << shift) | (value >> (64 - shift));
    }

    static void keccakf(uint64_t state[25]) {
        static const uint64_t kRoundConstants[24] = {
            0x0000000000000001ULL, 0x0000000000008082ULL,
            0x800000000000808aULL, 0x8000000080008000ULL,
            0x000000000000808bULL, 0x0000000080000001ULL,
            0x8000000080008081ULL, 0x8000000000008009ULL,
            0x000000000000008aULL, 0x0000000000000088ULL,
            0x0000000080008009ULL, 0x000000008000000aULL,
            0x000000008000808bULL, 0x800000000000008bULL,
            0x8000000000008089ULL, 0x8000000000008003ULL,
            0x8000000000008002ULL, 0x8000000000000080ULL,
            0x000000000000800aULL, 0x800000008000000aULL,
            0x8000000080008081ULL, 0x8000000000008080ULL,
            0x0000000080000001ULL, 0x8000000080008008ULL
        };

        static const int kRotationOffsets[24] = {
            1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
            27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
        };

        static const int kPermutationIndexes[24] = {
            10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
            15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
        };

        uint64_t temp[5];

        for (int round = 0; round < 24; ++round) {
            // Theta
            for (int i = 0; i < 5; ++i) {
                temp[i] = state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^ state[i + 20];
            }
            for (int i = 0; i < 5; ++i) {
                uint64_t mix = temp[(i + 4) % 5] ^ rotl64(temp[(i + 1) % 5], 1);
                for (int j = 0; j < 25; j += 5) {
                    state[j + i] ^= mix;
                }
            }

            // Rho and Pi
            uint64_t current = state[1];
            for (int i = 0; i < 24; ++i) {
                int index = kPermutationIndexes[i];
                uint64_t next = state[index];
                state[index] = rotl64(current, kRotationOffsets[i]);
                current = next;
            }

            // Chi
            for (int j = 0; j < 25; j += 5) {
                for (int i = 0; i < 5; ++i) {
                    temp[i] = state[j + i];
                }
                for (int i = 0; i < 5; ++i) {
                    state[j + i] ^= (~temp[(i + 1) % 5]) & temp[(i + 2) % 5];
                }
            }

            // Iota
            state[0] ^= kRoundConstants[round];
        }
    }

    static void keccak256Fallback(const unsigned char* input, size_t inputLen, unsigned char output[kHashSize]) {
        uint64_t state[25] = {};
        size_t offset = 0;

        // Absorb full blocks
        while (inputLen >= kRate) {
            for (size_t i = 0; i < kRate; ++i) {
                state[i / 8] ^= static_cast<uint64_t>(input[offset + i]) << (8 * (i % 8));
            }
            keccakf(state);
            offset += kRate;
            inputLen -= kRate;
        }

        // Absorb remainder and apply Keccak padding (0x01 ... 0x80)
        for (size_t i = 0; i < inputLen; ++i) {
            state[i / 8] ^= static_cast<uint64_t>(input[offset + i]) << (8 * (i % 8));
        }
        state[inputLen / 8] ^= static_cast<uint64_t>(0x01) << (8 * (inputLen % 8));
        state[(kRate - 1) / 8] ^= static_cast<uint64_t>(0x80) << (8 * ((kRate - 1) % 8));

        keccakf(state);

        for (size_t i = 0; i < kHashSize; ++i) {
            output[i] = static_cast<unsigned char>((state[i / 8] >> (8 * (i % 8))) & 0xFF);
        }
    }

    static bool keccak256OpenSsl(const unsigned char* input, size_t inputLen, unsigned char output[kHashSize]) {
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            return false;
        }

        EVP_MD* md = EVP_MD_fetch(NULL, "KECCAK-256", NULL);
        if (!md) {
            EVP_MD_CTX_free(mdctx);
            return false;
        }

        bool ok = EVP_DigestInit_ex(mdctx, md, NULL) == 1
            && EVP_DigestUpdate(mdctx, input, inputLen) == 1;

        unsigned int outputLen = 0;
        if (ok) {
            ok = EVP_DigestFinal_ex(mdctx, output, &outputLen) == 1 && outputLen == kHashSize;
        }

        EVP_MD_free(md);
        EVP_MD_CTX_free(mdctx);
        return ok;
    }

public:
    static std::vector<unsigned char> hexToBytes(const std::string& hex) {
        std::vector<unsigned char> bytes;
        
        if (hex.length() % 2 != 0) {
            throw std::runtime_error("Invalid hex string length");
        }

        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            unsigned char byte = static_cast<unsigned char>(std::stoi(byteString, nullptr, 16));
            bytes.push_back(byte);
        }

        return bytes;
    }

    static std::string bytesToHex(const unsigned char* data, size_t length) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            ss << std::setw(2) << static_cast<int>(data[i]);
        }
        return ss.str();
    }

    static std::string getHex(const std::string& hexInput) {
        std::vector<unsigned char> inputBytes = hexToBytes(hexInput);
        std::array<unsigned char, kHashSize> hash{};

        if (!keccak256OpenSsl(inputBytes.data(), inputBytes.size(), hash.data())) {
            // Fallback keeps Ethereum Keccak-256 behavior when OpenSSL lacks KECCAK-256.
            keccak256Fallback(inputBytes.data(), inputBytes.size(), hash.data());
        }

        return bytesToHex(hash.data(), hash.size());
    }

    static std::string getHexFromBytes(const std::vector<unsigned char>& inputBytes) {
        std::array<unsigned char, kHashSize> hash{};

        if (!keccak256OpenSsl(inputBytes.data(), inputBytes.size(), hash.data())) {
            // Fallback keeps Ethereum Keccak-256 behavior when OpenSSL lacks KECCAK-256.
            keccak256Fallback(inputBytes.data(), inputBytes.size(), hash.data());
        }

        return bytesToHex(hash.data(), hash.size());
    }
};


inline std::string getHex(const std::string& hexInput) {
    return Keccak256::getHex(hexInput);
}

} 

#endif // KECCAK256_H
