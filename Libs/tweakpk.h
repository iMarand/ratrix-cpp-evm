

std::vector<unsigned char> hexStringToBytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

std::string bytesToHexString(const std::vector<unsigned char>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string privateKeyTweakAdd(const std::string& privateKeyHex, const std::string& ILHex) {
    std::vector<unsigned char> privateKey = hexStringToBytes(privateKeyHex);
    std::vector<unsigned char> IL = hexStringToBytes(ILHex);

    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) {
        throw std::runtime_error("Failed to create EC_GROUP");
    }

    BIGNUM* privKey = BN_bin2bn(privateKey.data(), privateKey.size(), nullptr);
    BIGNUM* tweak = BN_bin2bn(IL.data(), IL.size(), nullptr);
    BIGNUM* result = BN_new();
    BIGNUM* order = BN_new();

    if (!privKey || !tweak || !result || !order) {
        EC_GROUP_free(group);
        BN_free(privKey);
        BN_free(tweak);
        BN_free(result);
        BN_free(order);
        throw std::runtime_error("Failed to create BIGNUMs");
    }

    if (EC_GROUP_get_order(group, order, nullptr) != 1) {
        EC_GROUP_free(group);
        BN_free(privKey);
        BN_free(tweak);
        BN_free(result);
        BN_free(order);
        throw std::runtime_error("Failed to get group order");
    }

    BN_CTX* bn_ctx = BN_CTX_new();
    if (!bn_ctx) {
        EC_GROUP_free(group);
        BN_free(privKey);
        BN_free(tweak);
        BN_free(result);
        BN_free(order);
        throw std::runtime_error("Failed to create BN_CTX");
    }

    // Perform (privKey + tweak) mod order
    if (BN_mod_add(result, privKey, tweak, order, bn_ctx) != 1) {
        BN_CTX_free(bn_ctx);
        EC_GROUP_free(group);
        BN_free(privKey);
        BN_free(tweak);
        BN_free(result);
        BN_free(order);
        throw std::runtime_error("Failed to perform modular addition");
    }

    std::vector<unsigned char> output(32);
    BN_bn2bin(result, output.data());

    BN_CTX_free(bn_ctx);
    EC_GROUP_free(group);
    BN_free(privKey);
    BN_free(tweak);
    BN_free(result);
    BN_free(order);

    return bytesToHexString(output);
}
