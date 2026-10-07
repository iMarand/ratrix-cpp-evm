#include <iostream>
#include <string>
#include ".env"
#include "RTX_LIBS.h"

#define RESET   "\033[0m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

void displayHelp() {
    std::cout << BOLD << CYAN << "Usage:\n" << RESET;
    std::cout << "  main.exe show -s \"<seed phrase>\"  - Show info from seed phrase\n";
    std::cout << "  main.exe show -p \"<private key>\"  - Show info from private key\n";
}

void showFromSeedPhrase(const std::string& seedPhrase) {
    try {
        std::string seed = RTX::toSeed(seedPhrase);
        std::string privateKey = RTX::toPrivateKey(seed);
        std::string address = RTX::toAddress(privateKey);
        
        std::string publicKey = RTX::getPublicKey(privateKey);
        RTX_BALANCE::EthereumBalanceChecker ethChecker;
        std::string ethBalance = ethChecker.getBalance(address);
        
        RTX_BALANCE::BNBBalanceChecker bnbChecker;
        std::string bnbBalance = bnbChecker.getBalance(address);
        
        std::cout << "\n" << BOLD << GREEN << "=== Wallet Information ===" << RESET << "\n";
        std::cout << YELLOW << "Seed Phrase: " << RESET << seedPhrase << "\n";
        std::cout << YELLOW << "Private Key: " << RESET << privateKey << "\n";
        std::cout << YELLOW << "Public Key:  " << RESET << publicKey << "\n";
        std::cout << YELLOW << "Address:     " << RESET << address << "\n";
        std::cout << CYAN << "ETH Balance: " << RESET << ethBalance << " ETH\n";
        std::cout << CYAN << "BNB Balance: " << RESET << bnbBalance << " BNB\n";
        std::cout << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void showFromPrivateKey(const std::string& privateKey) {
    try {
        std::string address = RTX::toAddress(privateKey);
        std::string publicKey = RTX::getPublicKey(privateKey);
     
        RTX_BALANCE::EthereumBalanceChecker ethChecker;
        std::string ethBalance = ethChecker.getBalance(address);
        
        RTX_BALANCE::BNBBalanceChecker bnbChecker;
        std::string bnbBalance = bnbChecker.getBalance(address);
        
        std::cout << "\n" << BOLD << GREEN << "=== Wallet Information ===" << RESET << "\n";
        std::cout << YELLOW << "Private Key: " << RESET << privateKey << "\n";
        std::cout << YELLOW << "Public Key:  " << RESET << publicKey << "\n";
        std::cout << YELLOW << "Address:     " << RESET << address << "\n";
        std::cout << CYAN << "ETH Balance: " << RESET << ethBalance << " ETH\n";
        std::cout << CYAN << "BNB Balance: " << RESET << bnbBalance << " BNB\n";
        std::cout << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

auto main(int argc, char** argv) -> int {
    if (argc < 4) {
        displayHelp();
        return 1;
    }
    
    std::string command = argv[1];
    std::string flag = argv[2];
    std::string input = argv[3];
    
    if (command != "show") {
        std::cerr << "Unknown command: " << command << std::endl;
        displayHelp();
        return 1;
    }
    
    if (flag == "-s") {
        showFromSeedPhrase(input);
    } else if (flag == "-p") {
        showFromPrivateKey(input);
    } else {
        std::cerr << "Unknown flag: " << flag << std::endl;
        displayHelp();
        return 1;
    }
    
    return 0;
}