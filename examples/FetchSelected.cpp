#include <iostream>
#include <fstream>
#include "../RTX_LIBS.h"

std::string checkBalanceRandETH(size_t t) {
    std::ofstream file("addressesLinkedEthereum");

    for(int r = 0; r < t; r++) {
        try {
            std::string rEntropy = RTX::randEntropy(32);
            std::string seedPhrase = RTX::toSeedPhrase(rEntropy);
            std::string seed = RTX::toSeed(seedPhrase);
            std::string privateKey = RTX::toPrivateKey(seed);
            std::string address = RTX::toAddress(privateKey);
    
            RTX_BALANCE::EthereumBalanceChecker checker;
    
            std::string balance = checker.getBalance(address);
            float _balance = std::stof(balance);
    
            if(_balance > 0.0001) {
                file << rEntropy << "<--------->" << balance << "\n"; 
            } 
    
            std::cout << r << " Balance for " << address << ": " << balance << " ETH" << std::endl;

        } catch(const std::exception& err) {
            std::cerr << err.what();
            continue;
        }
    }

    file.close();
    return "Done";
}

std::string checkBalanceRandBNB(size_t t) {
    std::ofstream file("addressesLinkedBNB");

    for(int r = 0; r < t; r++) {
        try {
            std::string rEntropy = RTX::randEntropy(32);
            std::string seedPhrase = RTX::toSeedPhrase(rEntropy);
            std::string seed = RTX::toSeed(seedPhrase);
            std::string privateKey = RTX::toPrivateKey(seed);
            std::string address = RTX::toAddress(privateKey);
    
            RTX_BALANCE::BNBBalanceChecker checker;
    
            std::string balance = checker.getBalance(address);
            float _balance = std::stof(balance);
    
            std::cout << r << " Balance for " << address << ": " << balance << " BNB" << std::endl;

            if(_balance > 0.0001) {
                file << rEntropy << "<--------->" << balance << "\n"; 
            } 
    

        } catch(const std::exception& err) {
            std::cerr << err.what();
            continue;
        }
    }

    file.close();
    return "Done";
}

std::string checkBalanceFromFile() {
    std::ifstream file("addresses");

    if(!file.is_open()) {
        std::cerr << "Failed to open file" << std::endl;
        return "Terminate";
    }

    std::string line;
    int addressLine = 0;

    while(std::getline(file, line)) {
        
        try {
            addressLine += 1;

            std::string address = line;
            RTX_BALANCE::EthereumBalanceChecker checker;

            std::string balance = checker.getBalance(address);
            std::cout << addressLine << " Balance for " << address << ": " << balance << " ETH" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Fatal error: " << e.what() << std::endl;
            return "Terminate";
        }
    }

    file.close();
    return "Done";
}

int main() {
    int n;
    std::cout << "Enter BNB addresses number: >" << std::endl;

    std::cin >> n;
    checkBalanceRandBNB(n);
    // checkBalanceFromFile();

    return 0;
}