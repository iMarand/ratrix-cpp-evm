// Ratrix-Pro Version 1.0.0
// Developer: Marand 
// File: Main.cpp File, Compile And Execute The File

#include <iostream>

#include ".env"
#include "RTX_LIBS.h"

auto main() -> int {
    // Generate Random Entropy
    std::string rEntropy = RTX::randEntropy(32);

    // Generate Seed Phrase From Entropy
    std::string seedPhrase = RTX::toSeedPhrase(rEntropy);

    // Generate Seed From Seed Phrase
    std::string seed = RTX::toSeed(seedPhrase);

    // Generate Private Key From The Seed *
    std::string privateKey = RTX::toPrivateKey(seed);

    // Generate Address From Private Key *
    std::string address = RTX::toAddress(privateKey);

    // You Can Check Also Public Key (Optional)
    std::string publicKey = RTX::getPublicKey(privateKey);

    coutLn("Entropy: ", rEntropy);
    coutLn("Seed Phrase: ", seedPhrase);
    coutLn("Seed: ", seed, "\n");
    coutLn("Private Key: ", privateKey);
    coutLn("Public Key: ", publicKey, "\n");
    coutLn("Address: ", address);

    return 0;
}