#ifndef KECCAK256_H
#define KECCAK256_H

namespace kecca256 {

class Keccak256 {
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

        OSSL_LIB_CTX* libctx = OSSL_LIB_CTX_new();
        if (!libctx) {
            throw std::runtime_error("Failed to create OpenSSL library context");
        }

        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to create message digest context");
        }

        EVP_MD* md = EVP_MD_fetch(libctx, "KECCAK-256", NULL);
        if (!md) {
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to fetch Keccak-256 algorithm");
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLength = 0;

        if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest initialization failed");
        }

        if (EVP_DigestUpdate(mdctx, inputBytes.data(), inputBytes.size()) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest update failed");
        }

        if (EVP_DigestFinal_ex(mdctx, hash, &hashLength) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest finalization failed");
        }

        EVP_MD_free(md);
        EVP_MD_CTX_free(mdctx);
        OSSL_LIB_CTX_free(libctx);

        return bytesToHex(hash, hashLength);
    }

    static std::string getHexFromBytes(const std::vector<unsigned char>& inputBytes) {
        OSSL_LIB_CTX* libctx = OSSL_LIB_CTX_new();
        if (!libctx) {
            throw std::runtime_error("Failed to create OpenSSL library context");
        }

        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to create message digest context");
        }

        EVP_MD* md = EVP_MD_fetch(libctx, "KECCAK-256", NULL);
        if (!md) {
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Failed to fetch Keccak-256 algorithm");
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLength = 0;

        if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest initialization failed");
        }

        if (EVP_DigestUpdate(mdctx, inputBytes.data(), inputBytes.size()) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest update failed");
        }

        if (EVP_DigestFinal_ex(mdctx, hash, &hashLength) != 1) {
            EVP_MD_free(md);
            EVP_MD_CTX_free(mdctx);
            OSSL_LIB_CTX_free(libctx);
            throw std::runtime_error("Digest finalization failed");
        }

        EVP_MD_free(md);
        EVP_MD_CTX_free(mdctx);
        OSSL_LIB_CTX_free(libctx);

        return bytesToHex(hash, hashLength);
    }
};


inline std::string getHex(const std::string& hexInput) {
    return Keccak256::getHex(hexInput);
}

} 

#endif // KECCAK256_H