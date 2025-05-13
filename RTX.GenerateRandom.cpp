
#include <iostream>
#include <fstream>

#include ".env"
#include "RTX_LIBS.h"

std::string AppendData(std::string addresses, std::string entropy) {
    std::ofstream fileAddress("bruteRandomAddress.data", std::ios::app);
    std::ofstream fileEntropy("bruteRandomEntropy.data", std::ios::app);

    if (!fileAddress && !fileEntropy) {
        std::cerr << "Error: Unable to append in files." << std::endl;
        return "Terminate";
    }

    fileAddress << addresses << "\n";
    fileEntropy << entropy << "\n";

    fileAddress.close();
    fileEntropy.close();

    return "Append Done";
}

auto main() -> int {
    int nEntropies;

    coutLn("Enter number of entropies: >>>");
    std::cin >> nEntropies;

    for(int f = 0; f <= nEntropies; f++) {
        std::string rEntropy = RTX::randEntropy(32);
        std::string seedPhrase = RTX::toSeedPhrase(rEntropy);
        std::string seed = RTX::toSeed(seedPhrase);
        std::string privateKey = RTX::toPrivateKey(seed);
        std::string address = RTX::toAddress(privateKey);
    
        AppendData(address, rEntropy);
    
        coutLn("Seed Phrase: ", seedPhrase);
        coutLn("Private Key: ", privateKey);
        coutLn("Address: ", address);
    }


    return 0;
}