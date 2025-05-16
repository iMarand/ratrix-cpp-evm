#include <iostream>
#include <fstream>
#include <filesystem>
#include <format>
#include <functional>
#include <unordered_map>

#include "jsonLib/json.hpp"
#include "../RTX_LIBS.h"
#include "jsonLib/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

struct COMMAND {
    char** args;

    COMMAND(char** arg) {
        args = arg;
    };
    
    void CreateWallet() {
        try {
            if(!args[2]) throw std::runtime_error("Missing 3rd argument");

            fs::create_directory("wallets");

            std::string fileName = args[2];
            std::string fileURL = "wallets/WALLET_" + fileName + "_DATA.json";

            if(fs::exists(fileURL)) {
                throw std::runtime_error("Wallet " + fileName + " Already Exists");
            }

            std::ofstream walletFile(fileURL);

            std::string randomEntropy = RTX::randEntropy(32);
            std::string seedPhrase = RTX::toSeedPhrase(randomEntropy);
            std::string seed = RTX::toSeed(seedPhrase);

            std::string privateKey = RTX::toPrivateKey(seed);
            std::string address = RTX::toAddress(privateKey);

            json walletData = {};
            walletData["walletName"] = fileName;
            walletData["address"] = address; 
            walletData["entropy"] = randomEntropy;
            walletData["seedPhrase"] = seedPhrase;
            walletData["privateKey"] = privateKey; 

            walletFile << walletData.dump(4) << std::endl;
            coutLn(GREEN, "Wallet ", fileName ," Created Successfully", RESET);

        } catch(std::exception& err) {
            std::cerr << err.what() << std::endl;
        }
    };

    void View() {
        try {
            if(!args[2]) throw std::runtime_error("Missing some argument");
            
            std::string which = args[2];
            std::unordered_map<std::string, std::function<void()>> viewAction = {
                {"wallets", []() {
                    for(const auto& entry : fs::directory_iterator("wallets")) {
                        static int index = 0;

                        fs::path file = entry.path().filename();
                        std::string fileName = file.string();

                        fileName = fileName.substr(fileName.find("_") + 1, fileName.rfind("_DATA.json") - fileName.find("_") - 1);
                        coutLn(YELLOW, index, " -> ", fileName, RESET);

                        index++;
                    }
                }}
            };

            viewAction[which]();
    
        } catch(std::exception& err) {
            std::cerr << err.what() << std::endl;
        }
    };

    void Import() {
        try {
            coutLn(args[1], "<-->", "View Command");
    
        } catch(std::exception& err) {
            std::cerr << err.what() << std::endl;
        }
    };

    void DropWallet() {
        try {
             if(!args[2]) throw std::runtime_error("Missing some argument");
    
        } catch(std::exception& err) {
            std::cerr << err.what() << std::endl;
        }
    };

    void Get() {
        try {
            if(!args[2] || !args[3]) throw std::runtime_error("Missing some arguments, must have 4 args");

            std::string walletName = args[2];
            std::string walletTargetKey = args[3];
            std::string walletURL = "wallets/WALLET_" + walletName + "_DATA.json";

            if(!fs::exists(walletURL)) throw std::runtime_error("Wallet entered doesn't exist");

            std::ifstream wallet(walletURL);
            std::string data((std::istreambuf_iterator<char>(wallet)), std::istreambuf_iterator<char>());

            json tWallet = json::parse(data);

            (walletTargetKey == "__") ?
                coutLn(tWallet.dump(4)) :
                coutLn(YELLOW, tWallet[walletTargetKey].get<std::string>(), RESET);

        } catch(std::exception& err) {
            std::cerr << err.what() << std::endl;
        }
    }
};

void walletHelpCommands() {
    std::cout << "\n";
    std::cout << BOLD << CYAN << "=================================================================\n";
    std::cout << "                             RATRIX CLI                  \n";
    std::cout << "=================================================================\n" << RESET;
    
    std::cout << YELLOW << "================= WELCOME TO RATRIX EVM SHELL  ==================\n";
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << " " << GREEN << "RTX HELP COMMANDS TO USE" << RESET << "        \n";
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << " " << YELLOW << "1. rtx create-wallet <wallet name>" << RESET << " \n";
    std::cout << " " << GREEN << "2. rtx view wallets" << RESET << "            \n";
    std::cout << " " << YELLOW << "3. rtx drop <wallet name>" << RESET << "         \n";
    std::cout << " " << GREEN << "4. rtx get-<wallet name> <wallet_key> eg: rtx get-wallet main-wallet address" << RESET << "        \n";
    std::cout << " " << YELLOW << "5. rtx <wallet> export-data <file>" << RESET << "\n";
    std::cout << " " << GREEN << "6. rtx <wallet> <crypto> receive" << RESET << " \n";
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "               Visit: github.com/iMarand/ratrix                 \n";
    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "\n";
}

struct Person {
    Person(int a, std::string name) {
        std::cout << a << " - " << name; 
    }
};


auto main(int argc, char** argv) -> int {
   if (argc < 2) {
        walletHelpCommands();
        std::cerr << "Write Your Own Command" << std::endl;
        return 1;
    }

    if(argv[1]) {
        std::string fArg = argv[1];
        std::unordered_map<std::string, std::function<void()>> commandMap = {
            {"get-wallet", [&argv]() { COMMAND(argv).Get(); }},
            {"view", [&argv]() { COMMAND(argv).View(); }},
            {"drop-wallet", [&argv]() { COMMAND(argv).DropWallet(); }},
            {"import", [&argv]() { COMMAND(argv).Import(); }},
            {"create-wallet", [&argv]() { COMMAND(argv).CreateWallet(); }},
        };

        commandMap[fArg]();
    }


    std::string arg1 = argv[1];
    std::cout << "Welcome To Ratrix" << __cplusplus << "\n";

    return 0;
}