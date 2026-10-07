#include "bip39.h" 

std::string byteToBinaryString(unsigned char byte) {
    std::bitset<8> bits(byte);
    return bits.to_string();
}

std::string generateBip39Mnemonic(const std::string& entropyHex) {
    size_t entropyBits = entropyHex.length() * 4;
    if (entropyBits != 128 && entropyBits != 160 && entropyBits != 192 && 
        entropyBits != 224 && entropyBits != 256) {
        throw std::invalid_argument("Entropy must be 32, 40, 48, 56, or 64 hex characters");
    }
    
    std::vector<unsigned char> entropyBytes = hexStringToBytes(entropyHex);
    
    // Calculate checksum length (entropy_length_in_bits / 32)
    size_t checksumLengthBits = entropyBits / 32;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(entropyBytes.data(), entropyBytes.size(), hash);
    
    std::string binaryString;
    for (unsigned char byte : entropyBytes) {
        binaryString += byteToBinaryString(byte);
    }

    std::string checksumBits = byteToBinaryString(hash[0]).substr(0, checksumLengthBits);
    binaryString += checksumBits;
   
    std::vector<int> indices;
    for (size_t i = 0; i < binaryString.length(); i += 11) {
        std::string chunk = binaryString.substr(i, 11);
        int index = std::stoi(chunk, nullptr, 2);
        indices.push_back(index);
    }
    
    std::stringstream mnemonic;
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) {
            mnemonic << " ";
        }
        if (indices[i] >= 0 && indices[i] < 2048) {
            mnemonic << bipWords[indices[i]];
        } else {
            throw std::runtime_error("Generated invalid word index: " + std::to_string(indices[i]));
        }
    }
    
    return mnemonic.str();
}
