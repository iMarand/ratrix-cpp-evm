#include <iostream>
#include <thread>
#include "../targetLists.h"

void runBruteForce(int start, int end) {
    int measure = end - start;
    coutLn("Thread started for range: ", start, " to ", end);

    RTX_BTF::BruteForceFromList(measure);
    coutLn("Thread finished for range: ", start, " to ", end);
}

auto main() -> int {
    int totalAddresses = 5000; 
    int threadCount = 3;
    int range = totalAddresses / threadCount;
    int remainder = totalAddresses % threadCount; 

    std::thread thread1(runBruteForce, 0, range);
    std::thread thread2(runBruteForce, range, 2 * range);
    std::thread thread3(runBruteForce, 2 * range, 3 * range + remainder);

    thread1.join();
    thread2.join();
    thread3.join();

    std::cout << "All threads completed." << std::endl;

    return 0;
}
