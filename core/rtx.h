#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <array>
#include <random>
#include <memory>
#include <cassert>
#include <bitset>

#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/obj_mac.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#include "generate_seed.h"
#include "getEthAddress.h"
#include "getPublicKey.h"
#include "seed_to_pk.h"
#include "toSeedPhrase.h"
#include "bitcoin.h"
#include "tron.h"
#include "solana.h"
#include "balance/Balances.h"

template <typename T>
std::string to_string_impl(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        return std::to_string(value);
    } else {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
}

// Variadic template function for flexible logging
template <typename... Args>
void coutLn(Args&&... args) {
    (std::cout << ... << to_string_impl(std::forward<Args>(args))) << "\n";
}

// Concatenate a string with a number, e.g. std::string("n=") + 42.
// Constrained to arithmetic types so it never shadows the standard
// std::string + const char* / std::string + std::string operators
// (leaving it unconstrained made those ambiguous).
template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
std::string operator+(const std::string& prefix, const T& value) {
    return prefix + std::to_string(value);
}

namespace RTX {
    std::string toSeed(const std::string& phrase) {
        const unsigned char salt[] = "mnemonic";
        int salt_len = strlen((const char*)salt);

        const char* phrase_cstr = phrase.c_str();
        int phrase_len = static_cast<int>(phrase.size());
        unsigned char* seed = seed::generate_seed(phrase_cstr, phrase_len, salt, salt_len);
        
        std::stringstream sd;
        if (seed) {
            for (int i = 0; i < 64; i++) {
                sd << std::hex << std::setw(2) << std::setfill('0') << (int)seed[i];
            }
    
            OPENSSL_free(seed);
        } else {
            std::cout << "Failed to generate seed." << std::endl;
        }
    
        return sd.str();
    }

    // Cryptographically secure random hex of `nibbles` hex characters.
    // Uses OpenSSL's CSPRNG (RAND_bytes). The old implementation used
    // std::mt19937_64, which is NOT cryptographically secure: its 64-bit state
    // is recoverable from output, so keys/entropy it produced were predictable.
    inline std::string secureRandHex(int nibbles) {
        if (nibbles <= 0) throw std::runtime_error("secureRandHex: length must be positive");
        const int nbytes = (nibbles + 1) / 2;
        std::vector<unsigned char> buf(nbytes);
        if (RAND_bytes(buf.data(), nbytes) != 1)
            throw std::runtime_error("secureRandHex: RAND_bytes failed (no system entropy)");

        static const char* hx = "0123456789abcdef";
        std::string out;
        out.reserve(nbytes * 2);
        for (unsigned char b : buf) { out.push_back(hx[b >> 4]); out.push_back(hx[b & 0x0f]); }
        out.resize(nibbles);
        return out;
    }

    std::string randPrivateKey() {
        return secureRandHex(64);  // 256-bit key
    }

    std::string randEntropy(int length) {
        return secureRandHex(length);  // BIP39: 32/40/48/56/64 hex chars
    }

    std::string toSeedPhrase(std::string entropy) {
        try {
            return generateBip39Mnemonic(entropy);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating Seed Phrase";
        }
    }

    std::string toPrivateKey(std::string seed) {
        try {
            return pk::seedToPrivateKey(seed);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating Private Key";
        }
    }

    std::string toAddress(std::string privateKey) {
        try {
            return  eth::getAddress(privateKey);;
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating Address";
        }
    }

    std::string getPublicKey(std::string privateKey) {
        try {
            return pub::getPublicKey(privateKey);
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating Public Key";
        }
    }

    // ---- Bitcoin (same private key as the EVM path) ----

    // Native SegWit "bc1q..." address (default, lowest fees).
    std::string toBtcAddress(std::string privateKey) {
        try {
            return btc::getAddressP2WPKH(privateKey);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating BTC Address";
        }
    }

    // Legacy "1..." address.
    std::string toBtcLegacyAddress(std::string privateKey) {
        try {
            return btc::getAddressP2PKH(privateKey);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating BTC Address";
        }
    }

    // Wallet Import Format (compressed) for the private key.
    std::string toBtcWIF(std::string privateKey) {
        try {
            return btc::toWIF(privateKey);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "Error Generating WIF";
        }
    }

}
