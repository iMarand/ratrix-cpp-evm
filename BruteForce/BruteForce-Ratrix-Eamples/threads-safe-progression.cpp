#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <utility>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <cmath>
#include "../targetLists.h"

constexpr int THREAD_COUNT = 34;
std::atomic<int> totalProcessedAddresses{0};
std::atomic<bool> programRunning{true};
std::mutex fileMutex;
int targetTotalAddresses = 0;

// Progress Bar Function
void displayProgressBar() {
    while (programRunning) {
        // Calculate percentage
        double percentage = (static_cast<double>(totalProcessedAddresses) / targetTotalAddresses) * 100.0;
        
        // Prepare progress bar
        int barWidth = 50;
        int progressWidth = static_cast<int>((percentage / 100.0) * barWidth);
        
        // Create progress bar string
        std::stringstream progressBar;
        progressBar << "\r[";
        for (int i = 0; i < barWidth; ++i) {
            if (i < progressWidth) {
                progressBar << "=";
            } else {
                progressBar << " ";
            }
        }
        progressBar << "] " 
                    << std::fixed << std::setprecision(2) 
                    << percentage << "% ("
                    << totalProcessedAddresses << "/" 
                    << targetTotalAddresses << ")";
        
        // Output progress
        std::cout << progressBar.str() << std::flush;
        
        // Sleep to reduce CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Break if target reached
        if (totalProcessedAddresses >= targetTotalAddresses) {
            break;
        }
    }
}

void runBruteForce(int start, int end) {
    int processingCount = 0;
    
    for(int measure = start; measure < end; measure++) {
        RTX_BTF::MATCH_ADDRESS ADDRESS_OBJ;
        ADDRESS_OBJ.rand_privateKey = RTX_BTF::randomizeHex(64);
        ADDRESS_OBJ.rand_address = RTX::toAddress(ADDRESS_OBJ.rand_privateKey);
        
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

auto main() -> int {
    int totalAddresses;
    
    coutLn("::Enter Number Of random address You Need::");
    std::cin >> totalAddresses;
    
    // Set global target for progress tracking
    targetTotalAddresses = totalAddresses;
    
    // Calculate thread ranges
    auto threadRanges = calculateThreadRanges(totalAddresses, THREAD_COUNT);
    
    // Vector to store threads
    std::vector<std::thread> threads;
    
    // Start progress monitoring thread
    std::thread progressThread(displayProgressBar);
    
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
    
    // Signal progress thread to stop
    programRunning = false;
    progressThread.join();
    
    // Final output to ensure 100% is shown
    std::cout << "\r[" << std::string(50, '=') << "] 100.00% ("
              << totalProcessedAddresses << "/" 
              << targetTotalAddresses << ")" << std::endl;
    
    // End timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    std::cout << "All " << THREAD_COUNT << " threads completed in " << duration << " milliseconds." << std::endl;
    
    return 0;
}