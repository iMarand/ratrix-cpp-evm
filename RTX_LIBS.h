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

#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

#include "Libs/generate_seed.h"
#include "Libs/getEthAddress.h"
#include "Libs/getPublicKey.h"
#include "Libs/seed_to_pk.h"

// Utility function to convert different types to string
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

// Overload for string concatenation-like behavior
template <typename T>
std::string operator+(const std::string& prefix, const T& value) {
    return prefix + to_string_impl(value);
}

namespace RTX {
    std::string toSeed(char phrase[]) {
        const unsigned char salt[] = "mnemonic";
        int salt_len = strlen((const char*)salt);

        unsigned char* seed = seed::generate_seed(phrase, salt, salt_len);
        
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
} 
