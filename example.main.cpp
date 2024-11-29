// Ratrix-Pro Version 1.0.0
// Developer: Marand 
// File: Main.cpp File, Compile And Execute The File

#include <iostream>
#include "RTX_LIBS.h"

auto main() -> int {

    // Enter Your Seed
    char phrase[] = "hip fossil catalog range orchard adult egg isolate keen marble jazz merry";

    // Generate Your Seed As Char *
    std::string seed = RTX::toSeed(phrase);

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