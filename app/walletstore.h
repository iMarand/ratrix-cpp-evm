#pragma once

// Encrypted-at-rest wallet storage for the Ratrix CLI/GUI.
//
// Threat model fixed vs. the old bin/wallet.cpp (which wrote privateKey,
// seedPhrase and entropy in plaintext JSON): secrets are encrypted with a key
// derived from the user's passphrase. Public metadata (name, addresses, type)
// stays in cleartext so balances/addresses are viewable without unlocking.
//
//   KDF     : scrypt (N=2^15, r=8, p=1) -> 32-byte key
//   Cipher  : AES-256-GCM (12-byte IV, 16-byte tag), AAD = wallet name
//
// File: <walletDir>/<name>.rtxwallet  (JSON)

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/core_names.h>

#include "../third_party/nlohmann/json.hpp"

namespace rtxstore {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Secret {
    std::string privateKey;
    std::string seedPhrase;
    std::string entropy;
};

struct Wallet {
    std::string name;
    std::string type;       // HD | IMPORTED_PK | IMPORTED_SEED | WATCH
    std::string eth;
    std::string btcSegwit;
    std::string btcLegacy;
    std::string tron;        // "T..."; same key as `eth`
    std::string sol;         // from the seed phrase; empty for key-only imports
    bool encrypted = false;  // false for WATCH-only
};

// ----------------------------- base64 -----------------------------------
inline std::string b64encode(const std::vector<unsigned char>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) { out.push_back(t[(val >> bits) & 0x3F]); bits -= 6; }
    }
    if (bits > -6) out.push_back(t[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
inline std::vector<unsigned char> b64decode(const std::string& in) {
    static int T[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) T[i] = -1;
        const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(unsigned char)t[i]] = i;
        init = true;
    }
    std::vector<unsigned char> out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) { out.push_back((unsigned char)((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

inline std::vector<unsigned char> randBytes(size_t n) {
    std::vector<unsigned char> b(n);
    if (RAND_bytes(b.data(), (int)n) != 1) throw std::runtime_error("RAND_bytes failed");
    return b;
}

// ----------------------------- KDF --------------------------------------
inline std::vector<unsigned char> deriveKey(const std::string& pass, const std::vector<unsigned char>& salt,
                                            uint64_t N = 1u << 15, uint32_t r = 8, uint32_t p = 1) {
    std::vector<unsigned char> key(32);
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "SCRYPT", nullptr);
    if (!kdf) throw std::runtime_error("scrypt unavailable");
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) throw std::runtime_error("kdf ctx alloc failed");

    OSSL_PARAM params[6], *pp = params;
    *pp++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, (void*)pass.data(), pass.size());
    *pp++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void*)salt.data(), salt.size());
    *pp++ = OSSL_PARAM_construct_uint64(OSSL_KDF_PARAM_SCRYPT_N, &N);
    *pp++ = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SCRYPT_R, &r);
    *pp++ = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SCRYPT_P, &p);
    *pp = OSSL_PARAM_construct_end();

    int ok = EVP_KDF_derive(ctx, key.data(), key.size(), params);
    EVP_KDF_CTX_free(ctx);
    if (ok <= 0) throw std::runtime_error("scrypt derive failed");
    return key;
}

// ------------------------- AES-256-GCM -----------------------------------
inline void gcmEncrypt(const std::vector<unsigned char>& key, const std::vector<unsigned char>& iv,
                       const std::string& aad, const std::string& plain,
                       std::vector<unsigned char>& ct, std::vector<unsigned char>& tag) {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) throw std::runtime_error("cipher ctx alloc failed");
    ct.resize(plain.size());
    tag.resize(16);
    int len = 0, outLen = 0, dummy = 0;
    bool ok =
        EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr) == 1 &&
        EVP_EncryptInit_ex(c, nullptr, nullptr, key.data(), iv.data()) == 1 &&
        EVP_EncryptUpdate(c, nullptr, &dummy, (const unsigned char*)aad.data(), (int)aad.size()) == 1 &&
        EVP_EncryptUpdate(c, ct.data(), &len, (const unsigned char*)plain.data(), (int)plain.size()) == 1;
    outLen = len;
    ok = ok && EVP_EncryptFinal_ex(c, ct.data() + outLen, &len) == 1;
    outLen += len;
    ok = ok && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1;
    EVP_CIPHER_CTX_free(c);
    if (!ok) throw std::runtime_error("encryption failed");
    ct.resize(outLen);
}

inline std::string gcmDecrypt(const std::vector<unsigned char>& key, const std::vector<unsigned char>& iv,
                              const std::string& aad, const std::vector<unsigned char>& ct,
                              std::vector<unsigned char> tag) {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) throw std::runtime_error("cipher ctx alloc failed");
    std::string plain;
    plain.resize(ct.size());
    int len = 0, outLen = 0, dummy = 0;
    bool ok =
        EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr) == 1 &&
        EVP_DecryptInit_ex(c, nullptr, nullptr, key.data(), iv.data()) == 1 &&
        EVP_DecryptUpdate(c, nullptr, &dummy, (const unsigned char*)aad.data(), (int)aad.size()) == 1 &&
        EVP_DecryptUpdate(c, (unsigned char*)plain.data(), &len, ct.data(), (int)ct.size()) == 1;
    outLen = len;
    ok = ok && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1;
    int fin = EVP_DecryptFinal_ex(c, (unsigned char*)plain.data() + outLen, &len);
    EVP_CIPHER_CTX_free(c);
    if (!ok || fin != 1) throw std::runtime_error("decryption failed: wrong passphrase or corrupted wallet");
    plain.resize(outLen + len);
    return plain;
}

// ----------------------------- storage -----------------------------------
inline fs::path walletDir() {
    if (const char* d = std::getenv("RATRIX_WALLET_DIR"); d && *d) return fs::path(d);
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA"); a && *a) return fs::path(a) / "Ratrix" / "wallets";
#else
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".ratrix" / "wallets";
#endif
    return fs::path("wallets");
}

inline fs::path walletPath(const std::string& name) { return walletDir() / (name + ".rtxwallet"); }

inline bool exists(const std::string& name) { return fs::exists(walletPath(name)); }

inline std::vector<std::string> list() {
    std::vector<std::string> names;
    if (!fs::exists(walletDir())) return names;
    for (auto& e : fs::directory_iterator(walletDir()))
        if (e.path().extension() == ".rtxwallet") names.push_back(e.path().stem().string());
    return names;
}

inline json loadRaw(const std::string& name) {
    std::ifstream f(walletPath(name));
    if (!f) throw std::runtime_error("wallet not found: " + name);
    return json::parse(f);
}

// Writes JSON to `path` via a temp file + rename, so a crash never leaves a
// half-written file behind.
inline void writeJson(const fs::path& path, const json& j) {
    const fs::path tmp = path.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << j.dump(2) << "\n"; }
    fs::rename(tmp, path);
}

inline Wallet meta(const json& j) {
    Wallet w;
    w.name = j.value("name", "");
    w.type = j.value("type", "");
    w.eth = j.value("/addresses/eth"_json_pointer, std::string());
    w.btcSegwit = j.value("/addresses/btcSegwit"_json_pointer, std::string());
    w.btcLegacy = j.value("/addresses/btcLegacy"_json_pointer, std::string());
    w.tron = j.value("/addresses/tron"_json_pointer, std::string());
    w.sol = j.value("/addresses/sol"_json_pointer, std::string());
    w.encrypted = j.contains("crypto");
    return w;
}

// Writes an encrypted wallet. For WATCH-only wallets pass empty passphrase and
// a Secret with all-empty fields; nothing secret is then stored.
inline void save(const Wallet& w, const Secret& secret, const std::string& passphrase) {
    fs::create_directories(walletDir());
    json j;
    j["ratrixWallet"] = 1;
    j["name"] = w.name;
    j["type"] = w.type;
    j["addresses"] = {{"eth", w.eth}, {"btcSegwit", w.btcSegwit}, {"btcLegacy", w.btcLegacy},
                      {"tron", w.tron}, {"sol", w.sol}};

    if (w.encrypted) {
        if (passphrase.empty()) throw std::runtime_error("a passphrase is required to encrypt this wallet");
        auto salt = randBytes(16);
        auto iv = randBytes(12);
        auto key = deriveKey(passphrase, salt);
        json sec = {{"privateKey", secret.privateKey}, {"seedPhrase", secret.seedPhrase}, {"entropy", secret.entropy}};
        std::vector<unsigned char> ct, tag;
        gcmEncrypt(key, iv, w.name, sec.dump(), ct, tag);
        j["crypto"] = {{"kdf", "scrypt"}, {"n", 1u << 15}, {"r", 8}, {"p", 1},
                       {"salt", b64encode(salt)}, {"cipher", "aes-256-gcm"},
                       {"iv", b64encode(iv)}, {"tag", b64encode(tag)}, {"ct", b64encode(ct)}};
    }

    writeJson(walletPath(w.name), j);
}

// Sets one public address (e.g. "sol") in an existing wallet file, leaving its
// encrypted secret untouched. Used to fill in chains added after the wallet
// was created.
inline void setAddress(const std::string& name, const std::string& chain, const std::string& address) {
    json j = loadRaw(name);
    j["addresses"][chain] = address;
    writeJson(walletPath(name), j);
}

inline Secret unlock(const std::string& name, const std::string& passphrase) {
    json j = loadRaw(name);
    if (!j.contains("crypto")) throw std::runtime_error("wallet '" + name + "' is watch-only (no secret stored)");
    const auto& c = j["crypto"];
    auto salt = b64decode(c.at("salt").get<std::string>());
    auto iv = b64decode(c.at("iv").get<std::string>());
    auto tag = b64decode(c.at("tag").get<std::string>());
    auto ct = b64decode(c.at("ct").get<std::string>());
    uint64_t N = c.value("n", 1u << 15);
    uint32_t r = c.value("r", 8), p = c.value("p", 1);
    auto key = deriveKey(passphrase, salt, N, r, p);
    std::string plain = gcmDecrypt(key, iv, j.value("name", name), ct, tag);
    json sec = json::parse(plain);
    return {sec.value("privateKey", ""), sec.value("seedPhrase", ""), sec.value("entropy", "")};
}

inline void remove(const std::string& name) {
    if (!exists(name)) throw std::runtime_error("wallet not found: " + name);
    fs::remove(walletPath(name));
}

// --------------------------- app passphrase ------------------------------
// A single app-level passphrase protects every wallet. app.lock stores a scrypt
// salt plus an AES-GCM-encrypted known token, so a wrong passphrase is detected
// up front. Each wallet is still encrypted with its own salt, keyed by the same
// passphrase (held in memory for the session, never written out).
inline fs::path appLockPath() { return walletDir() / "app.lock"; }
inline bool appInitialized() { return fs::exists(appLockPath()); }

inline void setupApp(const std::string& pass) {
    if (pass.empty()) throw std::runtime_error("app passphrase cannot be empty");
    fs::create_directories(walletDir());
    auto salt = randBytes(16);
    auto iv = randBytes(12);
    auto key = deriveKey(pass, salt);
    std::vector<unsigned char> ct, tag;
    gcmEncrypt(key, iv, "ratrix-app", "RATRIX_OK", ct, tag);
    json j = {{"kdf", "scrypt"}, {"n", 1u << 15}, {"r", 8}, {"p", 1},
              {"salt", b64encode(salt)}, {"iv", b64encode(iv)},
              {"tag", b64encode(tag)}, {"check", b64encode(ct)}};
    writeJson(appLockPath(), j);
}

inline bool verifyApp(const std::string& pass) {
    std::ifstream f(appLockPath());
    if (!f) return false;
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded()) return false;
    try {
        auto salt = b64decode(j.at("salt").get<std::string>());
        auto iv = b64decode(j.at("iv").get<std::string>());
        auto tag = b64decode(j.at("tag").get<std::string>());
        auto ct = b64decode(j.at("check").get<std::string>());
        auto key = deriveKey(pass, salt, j.value("n", 1u << 15), j.value("r", 8), j.value("p", 1));
        return gcmDecrypt(key, iv, "ratrix-app", ct, tag) == "RATRIX_OK";
    } catch (const std::exception&) {
        return false;  // wrong passphrase or corrupt lock
    }
}

}  // namespace rtxstore
