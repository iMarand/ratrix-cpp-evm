

std::vector<unsigned char> hexToBytes_hm(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(std::stoul(byteString, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string bytesToHex(const unsigned char* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

std::string hmacSha512(const std::vector<unsigned char>& key, const std::vector<unsigned char>& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    size_t digestLen;

    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);

    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char *>("SHA512"), 0);
    params[1] = OSSL_PARAM_construct_end();

    if (EVP_MAC_init(ctx, key.data(), key.size(), params) <= 0) {
        std::cerr << "Error initializing HMAC" << std::endl;
        EVP_MAC_CTX_free(ctx);
        EVP_MAC_free(mac);
        return "";
    }

    if (EVP_MAC_update(ctx, data.data(), data.size()) <= 0) {
        std::cerr << "Error updating HMAC" << std::endl;
        EVP_MAC_CTX_free(ctx);
        EVP_MAC_free(mac);
        return "";
    }

    if (EVP_MAC_final(ctx, digest, &digestLen, sizeof(digest)) <= 0) {
        std::cerr << "Error finalizing HMAC" << std::endl;
        EVP_MAC_CTX_free(ctx);
        EVP_MAC_free(mac);
        return "";
    }

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    
    return bytesToHex(digest, digestLen);
}
