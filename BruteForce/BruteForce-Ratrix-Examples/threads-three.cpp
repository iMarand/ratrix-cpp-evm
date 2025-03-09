#include <iostream>
#include <thread>
#include <chrono>

#include "../targetLists.h"

void runBruteForce(int c) {
    RTX_BTF::BruteForceFromList_(c);
    coutLn("Started Well");
}

auto main() -> int {
    int totalAddresses;

    coutLn("::Enter Number Of random address You Need::");
    std::cin >> totalAddresses;

    runBruteForce(totalAddresses);


    return 0;
}
