#include <iostream>
#include "../targetLists.h"

auto main() -> int {
    int measure = 500; // bruteforce for 5000 random addresses
    
    // start bruteForce Function
    RTX_BTF::BruteForceFromList(measure);

    return 0;
}