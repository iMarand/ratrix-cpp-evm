// Ratrix-Pro Version 1.0.0
// Developer: Marand 
// File: Main.cpp File, Compile And Execute The File

#include <iostream>

#include ".env"
#include "RTX_LIBS.h"

auto main() -> int {
    // Generate Your Seed As Char *
    std::string seed = RTX::toSeed(_PHRASE_);

    // Generate Private Key From The Seed *
    std::string privateKey = RTX::toPrivateKey(seed);

    // Generate Address From Private Key *
    std::string address = RTX::toAddress(privateKey);

    // You Can Check Also Public Key (Optional)
    std::string publicKey = RTX::getPublicKey(privateKey);

    coutLn("Seed Phrase: ", seed);
    coutLn("Private Key: ", privateKey);
    coutLn("Address: ", address);
    coutLn("Public Key: ", publicKey);

    return 0;
}