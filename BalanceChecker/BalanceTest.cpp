#include <iostream>
#include "../RTX_LIBS.h"


auto main() -> int {
    std::string address = "0x5009317fd4f6f8feea9dae41e5f0a4737bb7a7d5";
    RTX_BALANCE::BNBBalanceChecker checker;

    std::string balance = checker.getBalance(address);
    std::cout << "Balance for " << address << ": " << balance << " BNB" << std::endl;

    return 0;
}