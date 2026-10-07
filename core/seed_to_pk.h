#include "hmac.h"
#include "tweakpk.h"
#include "pubkey.h"

namespace pk {

std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = (unsigned char) strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

std::vector<unsigned char> create_master_secret(const std::vector<unsigned char>& seed) {
    const std::string master_secret = "Bitcoin seed";
    std::vector<unsigned char> result(EVP_MAX_MD_SIZE);
    size_t result_len = 0;  // Changed to size_t to match EVP_MAC_final signature

    // Create new EVP_MAC context
    std::unique_ptr<EVP_MAC, void(*)(EVP_MAC*)> mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free);
    if (!mac) {
        throw std::runtime_error("Failed to create EVP_MAC");
    }

    // Create MAC context
    std::unique_ptr<EVP_MAC_CTX, void(*)(EVP_MAC_CTX*)> ctx(EVP_MAC_CTX_new(mac.get()), EVP_MAC_CTX_free);
    if (!ctx) {
        throw std::runtime_error("Failed to create MAC context");
    }

    // Create parameter builder for HMAC
    std::unique_ptr<OSSL_PARAM_BLD, void(*)(OSSL_PARAM_BLD*)> param_bld(
        OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    if (!param_bld) {
        throw std::runtime_error("Failed to create parameter builder");
    }

    // Add parameters
    if (!OSSL_PARAM_BLD_push_utf8_string(param_bld.get(), OSSL_MAC_PARAM_DIGEST, "SHA512", 0)) {
        throw std::runtime_error("Failed to add digest parameter");
    }

    // Create parameter array
    std::unique_ptr<OSSL_PARAM, void(*)(OSSL_PARAM*)> params(
        OSSL_PARAM_BLD_to_param(param_bld.get()), OSSL_PARAM_free);
    if (!params) {
        throw std::runtime_error("Failed to create parameter array");
    }

    // Initialize MAC
    if (!EVP_MAC_init(ctx.get(), 
                      reinterpret_cast<const unsigned char*>(master_secret.c_str()),
                      master_secret.length(), 
                      params.get())) {
        throw std::runtime_error("Failed to initialize MAC");
    }

    // Update MAC with seed
    if (!EVP_MAC_update(ctx.get(), seed.data(), seed.size())) {
        throw std::runtime_error("Failed to update MAC");
    }

    // Finalize MAC
    if (!EVP_MAC_final(ctx.get(), result.data(), &result_len, result.size())) {
        throw std::runtime_error("Failed to finalize MAC");
    }

    result.resize(result_len);
    return result;
}

std::string ispk(std::string hstr) {
    return hstr.substr(0, 64);
}

std::string isPK(std::string hstr, int ln) {
    std::string PK = hstr.substr(0, 64);
    switch(ln) {
        case 0:
            PK = "00" + PK + "8000002c";
            break;
        case 1:
            PK = "00" + PK + "8000003c";
            break;
        case 2:
            PK = "00" + PK + "80000000";
            break;
        case 3:
            PK = "00" + PK + "00000000";
            break;
        case 8:
            PK = hstr;
            break;
        default:
            throw std::runtime_error("Unknown Modifier");
    }
    return PK;
}

std::string isIL(std::string hstr) {
    return hstr.substr(64);
}

std::string PHtoString(const std::vector<unsigned char>& u) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char byte : u) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string lPrivateKey(std::string b, int y) {
    std::vector<std::string> pchain;
    
    std::string phkey = isPK(b, y);
    std::string opkey = ispk(b);
    std::string ilkey = isIL(b);
    
    std::vector<unsigned char> chainCode = hexToBytes_hm(ilkey);
    std::vector<unsigned char> data = hexToBytes_hm(phkey);
    
    std::string hmresult = hmacSha512(chainCode, data);
    
    pchain.push_back(hmresult.substr(64));
    std::string tkIL = ispk(hmresult);
    
    std::string result = privateKeyTweakAdd(opkey, tkIL);
    
    return result + pchain[0];
}

std::string lPublicKey(std::string b, int len, int y, std::string xg) {
    std::vector<std::string> pchain;
    
    std::string phkey = isPK(b, y);
    std::string opkey = b.substr(len);
    std::string ilkey = b.substr(len);
    
    if(y == 8) {
        phkey = phkey.substr(0, len) + "00000000";
    }
    
    std::vector<unsigned char> chainCode = hexToBytes_hm(ilkey);
    std::vector<unsigned char> data = hexToBytes_hm(phkey);
    
    std::string hmresult = hmacSha512(chainCode, data);
    
    pchain.push_back(hmresult.substr(64));
    std::string tkIL = ispk(hmresult);
    
    std::string result = privateKeyTweakAdd(xg, tkIL);
    
    return result + pchain[0];
}

std::string lPubKey(std::string g) {
    std::vector<unsigned char> publicKey = getPublicKey(g);
    return bytesToHexString_pb(publicKey);
}

std::string seedToPrivateKey(std::string hs) {
    std::vector<unsigned char> seed_buffer = hex_to_bytes(hs);
    std::vector<unsigned char> I = create_master_secret(seed_buffer);
    
    std::string bts = PHtoString(I);
    std::string fpk = lPrivateKey(bts, 0);
    std::string spk = lPrivateKey(fpk, 1);
    std::string tpk = lPrivateKey(spk, 2);
    
    std::string s_pk = tpk.substr(0, 64);
    int pub_len = lPubKey(s_pk).length();
    
    std::string fpb = lPubKey(s_pk) + tpk.substr(64);
    std::string z_pk = lPublicKey(fpb, pub_len, 8, tpk.substr(0,64));
    
    std::string zwpub = lPubKey(z_pk.substr(0,64)) + z_pk.substr(64);
    int publen = lPubKey(z_pk.substr(0, 64)).length();
    
    std::string ltpriv = lPublicKey(zwpub, publen, 8, z_pk.substr(0, 64));
    
    return ltpriv.substr(0,64);
}

} // namespace pk

