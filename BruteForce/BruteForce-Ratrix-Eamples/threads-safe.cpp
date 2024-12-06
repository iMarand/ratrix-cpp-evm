#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <utility>
#include <atomic>
#include <mutex>
#include "../targetLists.h"

// Compile-time thread count and work distribution
constexpr int THREAD_COUNT = 34;
std::atomic<int> totalProcessedAddresses{0};
std::mutex fileMutex;

// Add the thread range calculation function before main
std::vector<std::pair<int, int>> calculateThreadRanges(int totalAddresses, int threadCount) {
    std::vector<std::pair<int, int>> ranges;
    int baseRange = totalAddresses / threadCount;
    int remainder = totalAddresses % threadCount;

    int currentStart = 0;
    for (int i = 0; i < threadCount; ++i) {
        int rangeSize = baseRange + (i < remainder ? 1 : 0);
        ranges.emplace_back(currentStart, currentStart + rangeSize);
        currentStart += rangeSize;
    }

    return ranges;
}

void runBruteForce(int start, int end) {
    int processingCount = 0;

    
    for(int measure = start; measure < end; measure++) {
        coutLn("Running Addresses: ");
        
        RTX_BTF::MATCH_ADDRESS ADDRESS_OBJ;
        ADDRESS_OBJ.rand_privateKey = RTX_BTF::randomizeHex(64);
        ADDRESS_OBJ.rand_address = RTX::toAddress(RTX_BTF::randomizeHex(64));
        
        bool res = ADDRESS_OBJ.compareList();

        if(res == true) {
            std::lock_guard<std::mutex> lock(fileMutex);
            RTX_BTF::AppendSeen(
                ADDRESS_OBJ.rand_privateKey,
                ADDRESS_OBJ.rand_address
            );
        }
        
        processingCount++;
        totalProcessedAddresses++;
    }
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
    
    std::cout << "Total Addresses Processed: " << totalProcessedAddresses << std::endl;
    std::cout << "All " << THREAD_COUNT << " threads completed in " << duration << " milliseconds." << std::endl;
    
    return 0;
}