#ifndef KECCAK256_H
#define KECCAK256_H

#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>

namespace kecca256 {

class Keccak256 {
public:
    // Convert a hex string to a vector of bytes
    static std::vector<unsigned char> hexToBytes(const std::string& hex) {
        std::vector<unsigned char> bytes;
        
        // Ensure the hex string length is even
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

    // Convert bytes to hex string
    static std::string bytesToHex(const unsigned char* data, size_t length) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            ss << std::setw(2) << static_cast<int>(data[i]);
        }
        return ss.str();
    }

    // Compute Keccak-256 hash of a hex string
    static std::string getHex(const std::string& hexInput) {
        // Convert hex input to bytes
        std::vector<unsigned char> inputBytes = hexToBytes(hexInput);

        // Initialize OpenSSL library context
        OSSL_LIB_CTX* libctx = OSSL_LIB_CTX_new();
        if (!libctx) {
            throw std::runtime_error("Failed to create OpenSSL library context");
        }

        // Create message digest context
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to create message digest context");
        }

        // Fetch Keccak-256 algorithm 
        EVP_MD* md = EVP_MD_fetch(libctx, "KECCAK-256", NULL);
        if (!md) {
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to fetch Keccak-256 algorithm");
        }

        // Output buffer for hash
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLength = 0;

        // Initialize digest operation
        if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest initialization failed");
        }

        // Update digest with input
        if (EVP_DigestUpdate(mdctx, inputBytes.data(), inputBytes.size()) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest update failed");
        }

        // Finalize digest
        if (EVP_DigestFinal_ex(mdctx, hash, &hashLength) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest finalization failed");
        }

        // Clean up resources
        EVP_MD_free(md);
        EVP_MD_CTX_free(mdctx);
        OSSL_LIB_CTX_free(libctx);

        // Convert hash to hex string
        return bytesToHex(hash, hashLength);
    }

    // Compute Keccak-256 hash of raw bytes
    static std::string getHexFromBytes(const std::vector<unsigned char>& inputBytes) {
        // Initialize OpenSSL library context
        OSSL_LIB_CTX* libctx = OSSL_LIB_CTX_new();
        if (!libctx) {
            throw std::runtime_error("Failed to create OpenSSL library context");
        }

        // Create message digest context
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to create message digest context");
        }

        // Fetch Keccak-256 algorithm 
        EVP_MD* md = EVP_MD_fetch(libctx, "KECCAK-256", NULL);
        if (!md) {
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to fetch Keccak-256 algorithm");
        }

        // Output buffer for hash
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLength = 0;

        // Initialize digest operation
        if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest initialization failed");
        }

        // Update digest with input
        if (EVP_DigestUpdate(mdctx, inputBytes.data(), inputBytes.size()) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest update failed");
        }

        // Finalize digest
        if (EVP_DigestFinal_ex(mdctx, hash, &hashLength) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest finalization failed");
        }

        // Clean up resources
        EVP_MD_free(md);
        EVP_MD_CTX_free(mdctx);
        OSSL_LIB_CTX_free(libctx);

        // Convert hash to hex string
        return bytesToHex(hash, hashLength);
    }
};

// Convenience function to match the original interface
inline std::string getHex(const std::string& hexInput) {
    return Keccak256::getHex(hexInput);
}

} 

#endif // KECCAK256_H