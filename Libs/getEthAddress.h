#include "keccak256.h"

namespace eth {

std::string toHex(const std::vector<unsigned char>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char b : data) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

std::string bytesToHex(const unsigned char* data, size_t length) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

std::string getAddress(const std::string& privateKeyHex) {
    // Convert private key from hex to BIGNUM
    BIGNUM* privateKey = BN_new();
    if (!BN_hex2bn(&privateKey, privateKeyHex.c_str())) {
        BN_free(privateKey);
        throw std::runtime_error("Failed to convert private key to BIGNUM");
    }

    // Get the EC group
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) {
        BN_free(privateKey);
        throw std::runtime_error("Failed to create EC group");
    }

    // Create public key point
    EC_POINT* publicKeyPoint = EC_POINT_new(group);
    if (!publicKeyPoint) {
        EC_GROUP_free(group);
        BN_free(privateKey);
        throw std::runtime_error("Failed to create EC point");
    }

    // Multiply generator point by private key to get public key point
    if (EC_POINT_mul(group, publicKeyPoint, privateKey, NULL, NULL, NULL) != 1) {
        EC_POINT_free(publicKeyPoint);
        EC_GROUP_free(group);
        BN_free(privateKey);
        throw std::runtime_error("Failed to compute public key point");
    }

    // Convert public key to bytes
    std::vector<unsigned char> publicKeyBytes(65);
    size_t publicKeyLen = EC_POINT_point2oct(group, publicKeyPoint, 
                                            POINT_CONVERSION_UNCOMPRESSED, 
                                            publicKeyBytes.data(), 
                                            publicKeyBytes.size(), 
                                            NULL);
    
    // Verify conversion worked and trim first byte
    if (publicKeyLen != 65) {
        EC_POINT_free(publicKeyPoint);
        EC_GROUP_free(group);
        BN_free(privateKey);
        throw std::runtime_error("Public key conversion failed");
    }

    // Remove the first byte (0x04) which indicates uncompressed public key
    std::vector<unsigned char> publicKeyBytesWithout04(publicKeyBytes.begin() + 1, publicKeyBytes.end());

    // Convert public key bytes to hex string for Keccak-256
    std::string publicKeyHex = toHex(publicKeyBytesWithout04);

    // Hash public key using Keccak-256
    std::string hash;
    try {
        hash = kecca256::getHex(publicKeyHex);
    } catch (const std::exception& e) {
        EC_POINT_free(publicKeyPoint);
        EC_GROUP_free(group);
        BN_free(privateKey);
        throw std::runtime_error(std::string("Keccak256 hashing failed: ") + e.what());
    }
    
    // Take the last 40 characters (20 bytes) of the hash as the Ethereum address
    std::string address = "0x" + hash.substr(hash.length() - 40);

    // Clean up resources
    EC_POINT_free(publicKeyPoint);
    EC_GROUP_free(group);
    BN_free(privateKey);

    return address;
}

} // namespace eth

// int main() {
//     try {
//         std::string privateKeyHex = "453f013a00adab972dacb96da29d89a8c2ef477c17ffedf70fbd5ec213c1ef0c";
//         std::string address = eth::getAddress(privateKeyHex);
        
//         std::cout << "Ethereum Address: " << address << std::endl;
//     }
//     catch (const std::exception& e) {
//         std::cerr << "Error: " << e.what() << std::endl;
//         return 1;
//     }
    
//     return 0;
// }