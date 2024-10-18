// Ratrix-Pro Version 1.0.0
// Developer: Marand 
// File: Main.cpp File, Compile And Execute The File

#include <iostream>
#include "callLibs.h"

auto main() -> int {
    // enter Your Seed
    char phrase[] = "hip fossil catalog range orchard adult egg isolate keen marble jazz merry";

    // Generate Your Seed
    std::string seed = RTX::toSeed(phrase);

    // Generate Private Key From The Seed
    std::string privateKey = RTX::toPrivateKey(seed);

    std:: cout << privateKey;

    return 0;
}