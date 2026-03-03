#include <iostream>
#include <string>
#include <iomanip>
#include <fstream>
#include <vector>
#include ".env"
#include "RTX_LIBS.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define RESET   "\033[0m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"

void displayHelp() {
    std::cout << BOLD << CYAN << "Usage:\n" << RESET;
    std::cout << "  main.exe show -s \"<seed phrase>\"  - Show info from seed phrase\n";
    std::cout << "  main.exe show -p \"<private key>\"  - Show info from private key\n";
    std::cout << "  main.exe show -f \"<file path>\"    - Show info from seed phrase in file\n";
}

void showFromSeedPhrase(const std::string& seedPhrase) {
    try {
        // Debug: Show actual bytes received
        std::cout << "\n" << CYAN << "[DEBUG] Received bytes: " << RESET;
        for (unsigned char c : seedPhrase) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)c << " ";
        }
        std::cout << std::dec << "\n";
        std::cout << CYAN << "[DEBUG] Byte length: " << RESET << seedPhrase.length() << "\n";
        std::cout << CYAN << "[DEBUG] Seed phrase: " << RESET << seedPhrase << "\n\n";
        
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
        std::cerr << RED << "Error: " << e.what() << RESET << std::endl;
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
        std::cerr << RED << "Error: " << e.what() << RESET << std::endl;
    }
}

void showFromFile(const std::string& filePath) {
    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filePath);
        }
        
        std::string seedPhrase;
        std::getline(file, seedPhrase);
        file.close();
        
        if (seedPhrase.empty()) {
            throw std::runtime_error("File is empty");
        }
        
        // Remove potential BOM (Byte Order Mark) from UTF-8 files
        if (seedPhrase.length() >= 3 && 
            (unsigned char)seedPhrase[0] == 0xEF && 
            (unsigned char)seedPhrase[1] == 0xBB && 
            (unsigned char)seedPhrase[2] == 0xBF) {
            seedPhrase = seedPhrase.substr(3);
        }
        
        // Remove trailing newline/carriage return
        while (!seedPhrase.empty() && 
               (seedPhrase.back() == '\n' || seedPhrase.back() == '\r')) {
            seedPhrase.pop_back();
        }
        
        showFromSeedPhrase(seedPhrase);
        
    } catch (const std::exception& e) {
        std::cerr << RED << "Error: " << e.what() << RESET << std::endl;
    }
}

#ifdef _WIN32
// Get UTF-8 command line arguments on Windows
std::vector<std::string> getUtf8Args() {
    std::vector<std::string> args;
    
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argv == nullptr) {
        return args;
    }
    
    for (int i = 0; i < argc; i++) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, 
                                              NULL, 0, NULL, NULL);
        if (size_needed > 0) {
            std::string utf8_arg(size_needed - 1, 0);  // -1 to exclude null terminator
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, 
                               &utf8_arg[0], size_needed, NULL, NULL);
            args.push_back(utf8_arg);
        }
    }
    
    LocalFree(argv);
    return args;
}
#endif

auto main(int argc, char** argv) -> int {
#ifdef _WIN32
    // Enable UTF-8 support on Windows
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Enable ANSI escape sequences for colors
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    // Get UTF-8 arguments
    std::vector<std::string> utf8_args = getUtf8Args();
    
    if (utf8_args.size() < 4) {
        displayHelp();
        return 1;
    }
    
    std::string command = utf8_args[1];
    std::string flag = utf8_args[2];
    std::string input = utf8_args[3];
    
#else
    // Unix/Linux - arguments are already UTF-8
    if (argc < 4) {
        displayHelp();
        return 1;
    }
    
    std::string command = argv[1];
    std::string flag = argv[2];
    std::string input = argv[3];
#endif
    
    if (command != "show") {
        std::cerr << RED << "Unknown command: " << command << RESET << std::endl;
        displayHelp();
        return 1;
    }
    
    if (flag == "-s") {
        showFromSeedPhrase(input);
    } else if (flag == "-p") {
        showFromPrivateKey(input);
    } else if (flag == "-f") {
        showFromFile(input);
    } else {
        std::cerr << RED << "Unknown flag: " << flag << RESET << std::endl;
        displayHelp();
        return 1;
    }
    
    return 0;
}