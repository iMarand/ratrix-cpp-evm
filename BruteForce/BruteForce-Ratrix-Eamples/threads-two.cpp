#include <iostream>
#include <thread>
#include "../targetLists.h"

// Function to be executed by threads
void runBruteForce(int start, int end) {
    int measure = end - start;
    coutLn("Thread started for range: ", start, " to ",end);
    
    RTX_BTF::BruteForceFromList(measure); 
    coutLn("Thread finished for range: ", start, " to ", end);
}

auto main() -> int {
    int totalAddresses = 5000;
    int threadCount = 2;
    int range = totalAddresses / threadCount;

    std::thread thread1(runBruteForce, 0, range);
    std::thread thread2(runBruteForce, range, totalAddresses);

    thread1.join();
    thread2.join();

    std::cout << "All threads completed." << std::endl;

    return 0;
}
