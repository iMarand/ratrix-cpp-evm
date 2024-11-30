#include <iostream>
#include <thread>
#include <chrono>
#include "../targetLists.h"

void runBruteForce(int start, int end) {
    int measure = end - start;
    std::cout << "Thread started for range: " << start << " to " << end << std::endl;
    RTX_BTF::BruteForceFromList(measure);
    std::cout << "Thread finished for range: " << start << " to " << end << std::endl;
}

auto main() -> int {
    int totalAddresses;

    coutLn("::Enter Number Of random address You Need::");
    std::cin >> totalAddresses;

    int threadCount = 3;
    int range = totalAddresses / threadCount;
    int remainder = totalAddresses % threadCount;

    auto startTime = std::chrono::high_resolution_clock::now();

    std::thread thread1(runBruteForce, 0, range);
    std::thread thread2(runBruteForce, range, 2 * range);
    std::thread thread3(runBruteForce, 2 * range, 3 * range + remainder);

    thread1.join();
    thread2.join();
    thread3.join();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "All threads completed in " << duration << " milliseconds." << std::endl;

    return 0;
}
