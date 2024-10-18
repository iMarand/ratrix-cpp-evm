#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <array>
#include <memory>

#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

#include "Libs/generate_seed.h"
#include "Libs/seed_to_pk.h"

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
            return "error";
        }
    }
} 
