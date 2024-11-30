#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include "../targetLists.h"

void runBruteForce(int start, int end) {
    int measure = end - start;
    RTX_BTF::BruteForceFromList(measure);
}

auto main() -> int {
    int totalAddresses;
    
    coutLn("::Enter Number Of random address You Need::");
    std::cin >> totalAddresses;
    
    const int threadCount = 15;
    
    int range = totalAddresses / threadCount;
    int remainder = totalAddresses % threadCount;
    
    std::vector<std::thread> threads;

    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create and launch threads
    for (int i = 0; i < threadCount; ++i) {
        int start = i * range;
        int end = (i == threadCount - 1) ? 
                  (start + range + remainder) :  // Last thread gets any remainder
                  (start + range);
        
        threads.emplace_back(runBruteForce, start, end);
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // End timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    std::cout << "All " << threadCount << " threads completed in " << duration << " milliseconds." << std::endl;
    system("pause");
    
    return 0;
}