
namespace seed {
    unsigned char* generate_seed(const char* mnemonic, const unsigned char* salt, int salt_len) {
        unsigned char* seed = (unsigned char*)OPENSSL_malloc(64);  // 64 bytes = 512 bits
        if (!seed) return NULL;

        EVP_KDF *kdf;
        EVP_KDF_CTX *kctx;
        OSSL_PARAM params[6], *p = params;

        kdf = EVP_KDF_fetch(NULL, "PBKDF2", NULL);
        kctx = EVP_KDF_CTX_new(kdf);
        EVP_KDF_free(kdf);

        if (kctx == NULL) {
            OPENSSL_free(seed);
            return NULL;
        }

        unsigned int iter = 2048;
        unsigned int keylen = 64;

        *p++ = OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA512"), 0);
        *p++ = OSSL_PARAM_construct_octet_string("pass", (void*)mnemonic, strlen(mnemonic));
        *p++ = OSSL_PARAM_construct_octet_string("salt", (void*)salt, salt_len);
        *p++ = OSSL_PARAM_construct_uint("iter", &iter);
        *p++ = OSSL_PARAM_construct_uint("keylen", &keylen);
        *p = OSSL_PARAM_construct_end();

        if (EVP_KDF_derive(kctx, seed, 64, params) <= 0) {
            OPENSSL_free(seed);
            EVP_KDF_CTX_free(kctx);
            return NULL;
        }

        EVP_KDF_CTX_free(kctx);
        return seed;
    }
}
