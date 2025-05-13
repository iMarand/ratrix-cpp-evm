#include <iostream>
#include "Balances.h"

int main() {
    std::cout << "Checking wallet etheruem balance" << std::endl;

    try {
        std::string address = "0x75bced4bc8f882facbdb56d458c35cc079d48255";
        RTX_BALANCE::EthereumBalanceChecker checker;

        std::string balance = checker.getBalance(address);
        std::cout << "Balance for " << address << ": " << balance << " ETH" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}