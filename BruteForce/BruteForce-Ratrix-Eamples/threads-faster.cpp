#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <utility>
#include "../targetLists.h"

// Compile-time thread count and work distribution
constexpr int THREAD_COUNT = 50;

// Work distribution calculation function
std::vector<std::pair<int, int>> calculateThreadRanges(int totalAddresses, int threadCount) {
    std::vector<std::pair<int, int>> ranges;
    int baseRange = totalAddresses / threadCount;
    int remainder = totalAddresses % threadCount;

    for (int i = 0; i < threadCount; ++i) {
        int start = i * baseRange;
        int end = (i == threadCount - 1) 
            ? (start + baseRange + remainder)
            : (start + baseRange);
        
        ranges.emplace_back(start, end);
    }

    return ranges;
}

void runBruteForce(int start, int end) {
    int measure = end - start;
    RTX_BTF::BruteForceFromList(measure);
}

auto main() -> int {
    int totalAddresses;
    
    coutLn("::Enter Number Of random address You Need::");
    std::cin >> totalAddresses;
    
    // Calculate thread ranges
    auto threadRanges = calculateThreadRanges(totalAddresses, THREAD_COUNT);
    
    // Vector to store threads
    std::vector<std::thread> threads;
    
    // Start timing
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create and launch threads using calculated ranges
    for (const auto& [start, end] : threadRanges) {
        threads.emplace_back(runBruteForce, start, end);
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // End timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    std::cout << "All " << THREAD_COUNT << " threads completed in " << duration << " milliseconds." << std::endl;
    
    return 0;
}