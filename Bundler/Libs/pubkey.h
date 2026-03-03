
// Custom deleters for RAII
struct EVP_PKEY_CTX_Deleter {
    void operator()(EVP_PKEY_CTX* ctx) { EVP_PKEY_CTX_free(ctx); }
};

struct EVP_PKEY_Deleter {
    void operator()(EVP_PKEY* key) { EVP_PKEY_free(key); }
};

struct BIGNUM_Deleter {
    void operator()(BIGNUM* bn) { BN_free(bn); }
};

struct EC_POINT_Deleter {
    void operator()(EC_POINT* point) { EC_POINT_free(point); }
};

struct EC_GROUP_Deleter {
    void operator()(EC_GROUP* group) { EC_GROUP_free(group); }
};

// Convert hex string to bytes
std::vector<unsigned char> hexStringToBytes_pub(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = (unsigned char) strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

// Convert bytes to hex string
std::string bytesToHexString_pb(const std::vector<unsigned char>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::vector<unsigned char> getPublicKey(const std::string& privateKeyHex) {
    // Create a new EC group for secp256k1
    std::unique_ptr<EC_GROUP, EC_GROUP_Deleter> group(EC_GROUP_new_by_curve_name(NID_secp256k1));
    if (!group) {
        throw std::runtime_error("Failed to create EC group");
    }

    // Convert private key from hex
    BIGNUM* bn_raw = nullptr;
    if (!BN_hex2bn(&bn_raw, privateKeyHex.c_str())) {
        throw std::runtime_error("Failed to convert private key to BIGNUM");
    }
    std::unique_ptr<BIGNUM, BIGNUM_Deleter> priv_bn(bn_raw);

    // Create a new EC point for the public key
    std::unique_ptr<EC_POINT, EC_POINT_Deleter> pub_key(EC_POINT_new(group.get()));
    if (!pub_key) {
        throw std::runtime_error("Failed to create EC point");
    }

    // Calculate public key point
    if (!EC_POINT_mul(group.get(), pub_key.get(), priv_bn.get(), nullptr, nullptr, nullptr)) {
        throw std::runtime_error("Failed to calculate public key point");
    }

    // Get the required buffer size for compressed format
    size_t buf_len = EC_POINT_point2oct(group.get(), pub_key.get(), POINT_CONVERSION_COMPRESSED, nullptr, 0, nullptr);
    if (buf_len == 0) {
        throw std::runtime_error("Failed to get public key length");
    }

    // Allocate buffer and get public key in compressed format
    std::vector<unsigned char> pubkey(buf_len);
    if (EC_POINT_point2oct(group.get(), pub_key.get(), POINT_CONVERSION_COMPRESSED, pubkey.data(), buf_len, nullptr) != buf_len) {
        throw std::runtime_error("Failed to get public key");
    }

    return pubkey;
}

