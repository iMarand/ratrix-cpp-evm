#include <iostream>
#include <fstream>

#include "../RTX_LIBS.h"
#include "jsonLib/json.hpp"

void walletHelpCommands() {

    coutLn("|----------WELCOME TO RATRIX EVM SHELL COMMANDS -------------|");
    coutLn("| - - - - - - - RTX HELP-COMMANDS TO USE - -  -  - - - - - - |");
    // coutLn("|", );
    coutLn("| 1. rtx create wallet <wallet name>                         |"); // Create New Wallet
    coutLn("| 2. rtx view wallets                                        |"); // View All Wallets Available
    coutLn("| 3. rtx drop <wallet name> --confirm-delete                 |"); // Delete Wallet
    coutLn("| 4. rtx get <wallet name>                                   |"); // Get wallet Info
    coutLn("| 5. rtx <wallet name> export-data <file.ext>                |"); // Export Wallet Data To Specified File
    coutLn("| 6. rtx <wallet name> <crypto name> receive-address         |"); // Get Recieve Address
    coutLn("| Visit https://github.com/iMarand/ratrix-cpp-evm for more   |"); // Info Link
}

auto main() -> int {
    std::string str = "Hello";
    coutLn(str);

    return 0;
}