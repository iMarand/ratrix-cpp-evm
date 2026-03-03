#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../targetLists.h"

namespace {

constexpr const char* kMatchOutputFile = "found_matches.txt";
constexpr const char* kSeedOutputFile = "found_address_seedphrase.txt";
constexpr const char* kRandomAddressOutputFile = "bruteRandom.data";
constexpr const char* kRandomPrivateOutputFile = "bruteRandomPrivate.data";

std::atomic<long long> gProcessed{0};
std::atomic<long long> gFound{0};
std::atomic<bool> gRunning{true};
std::mutex gFileMutex;

enum class BruteMode {
    FastPrivateKey = 1,
    SeedPhrase = 2,
    RandomDump = 3
};

int effectiveThreadCount(long long totalAddresses) {
    const unsigned hw = std::thread::hardware_concurrency();
    int count = static_cast<int>(hw == 0 ? 8 : hw);
    if (totalAddresses > 0 && count > totalAddresses) {
        count = static_cast<int>(totalAddresses);
    }
    return std::max(1, count);
}

std::vector<long long> splitWork(long long totalAddresses, int threadCount) {
    std::vector<long long> chunks;
    chunks.reserve(threadCount);

    const long long base = totalAddresses / threadCount;
    const long long remainder = totalAddresses % threadCount;

    for (int i = 0; i < threadCount; ++i) {
        chunks.push_back(base + (i < remainder ? 1 : 0));
    }
    return chunks;
}

std::string normalizeAddress(std::string address) {
    std::transform(address.begin(), address.end(), address.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return address;
}

const std::unordered_map<std::string, size_t>& targetIndexMap() {
    static const std::unordered_map<std::string, size_t> indexMap = [] {
        std::unordered_map<std::string, size_t> map;
        map.reserve(targetAddresses.size() * 2);
        for (size_t i = 0; i < targetAddresses.size(); ++i) {
            map.emplace(normalizeAddress(targetAddresses[i]), i);
        }
        return map;
    }();
    return indexMap;
}

std::string randomHexFast(size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    thread_local std::mt19937_64 rng([] {
        std::random_device rd;
        std::seed_seq seed{
            rd(), rd(), rd(), rd(),
            static_cast<unsigned int>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count() & 0xffffffffu
            )
        };
        return std::mt19937_64(seed);
    }());
    thread_local std::uniform_int_distribution<int> dist(0, 15);

    std::string output;
    output.resize(len);
    for (size_t i = 0; i < len; ++i) {
        output[i] = kHex[dist(rng)];
    }
    return output;
}

void flushThreadBuffers(const std::string& matchBuffer, const std::string& seedBuffer, const std::string& randomAddrBuffer, const std::string& randomPkBuffer) {
    std::lock_guard<std::mutex> lock(gFileMutex);

    if (!matchBuffer.empty()) {
        std::ofstream file(kMatchOutputFile, std::ios::app);
        if (file) {
            file << matchBuffer;
        }
    }

    if (!seedBuffer.empty()) {
        std::ofstream file(kSeedOutputFile, std::ios::app);
        if (file) {
            file << seedBuffer;
        }
    }

    if (!randomAddrBuffer.empty()) {
        std::ofstream file(kRandomAddressOutputFile, std::ios::app);
        if (file) {
            file << randomAddrBuffer;
        }
    }

    if (!randomPkBuffer.empty()) {
        std::ofstream file(kRandomPrivateOutputFile, std::ios::app);
        if (file) {
            file << randomPkBuffer;
        }
    }
}

void runWorker(long long iterations, BruteMode mode) {
    const auto& targets = targetIndexMap();

    std::ostringstream matchBuffer;
    std::ostringstream seedBuffer;
    std::ostringstream randomAddrBuffer;
    std::ostringstream randomPkBuffer;

    auto flushLocalIfNeeded = [&](bool force = false) {
        const bool shouldFlush = force
            || matchBuffer.tellp() > 8192
            || seedBuffer.tellp() > 8192
            || randomAddrBuffer.tellp() > 131072
            || randomPkBuffer.tellp() > 131072;

        if (!shouldFlush) {
            return;
        }

        flushThreadBuffers(matchBuffer.str(), seedBuffer.str(), randomAddrBuffer.str(), randomPkBuffer.str());
        matchBuffer.str("");
        seedBuffer.str("");
        randomAddrBuffer.str("");
        randomPkBuffer.str("");
        matchBuffer.clear();
        seedBuffer.clear();
        randomAddrBuffer.clear();
        randomPkBuffer.clear();
    };

    for (long long i = 0; i < iterations; ++i) {
        std::string entropy;
        std::string seedPhrase;
        std::string privateKey;
        std::string address;

        if (mode == BruteMode::FastPrivateKey) {
            privateKey = randomHexFast(64);
            address = RTX::toAddress(privateKey);
        } else if (mode == BruteMode::SeedPhrase) {
            entropy = randomHexFast(32);
            seedPhrase = RTX::toSeedPhrase(entropy);
            const std::string seed = RTX::toSeed(seedPhrase);
            privateKey = RTX::toPrivateKey(seed);
            address = RTX::toAddress(privateKey);
        } else {
            privateKey = randomHexFast(64);
            address = RTX::toAddress(privateKey);
            randomAddrBuffer << address << '\n';
            randomPkBuffer << privateKey << '\n';
            ++gProcessed;
            continue;
        }

        const std::string normalizedAddress = normalizeAddress(address);
        const auto it = targets.find(normalizedAddress);
        if (it != targets.end()) {
            const size_t matchedIndex = it->second;
            ++gFound;

            matchBuffer
                << "Address: " << address
                << " | MatchedTarget: " << targetAddresses[matchedIndex]
                << " | TargetIndex: " << matchedIndex
                << " | PrivateKey: " << privateKey
                << '\n';

            if (mode == BruteMode::SeedPhrase) {
                seedBuffer
                    << "Address: " << address
                    << " | SeedPhrase: " << seedPhrase
                    << " | Entropy: " << entropy
                    << '\n';
            } else {
                seedBuffer
                    << "Address: " << address
                    << " | SeedPhrase: N/A (fast private-key mode)"
                    << '\n';
            }
        }

        ++gProcessed;
        flushLocalIfNeeded();
    }

    flushLocalIfNeeded(true);
}

void showProgress(long long targetTotal, std::chrono::steady_clock::time_point startTime) {
    while (gRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        const long long processed = gProcessed.load();
        const long long found = gFound.load();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime
        ).count();
        const double seconds = elapsedMs <= 0 ? 0.001 : (static_cast<double>(elapsedMs) / 1000.0);
        const double rate = static_cast<double>(processed) / seconds;
        const double pct = targetTotal > 0 ? (100.0 * static_cast<double>(processed) / static_cast<double>(targetTotal)) : 100.0;

        std::cout
            << "\rProcessed: " << processed << "/" << targetTotal
            << " | Found: " << found
            << " | Rate: " << static_cast<long long>(rate) << "/s"
            << " | " << std::fixed << std::setprecision(2) << std::min(100.0, pct) << "%"
            << std::flush;

        if (processed >= targetTotal) {
            break;
        }
    }
}

} // namespace

auto main() -> int {
    int modeInput = 0;
    long long totalAddresses = 0;

    coutLn("::Choose Brute Force Type::");
    coutLn("1: Fast Private-Key Brute Force (matches + logs)");
    coutLn("2: Seed-Phrase Brute Force (matches + seed phrase logs)");
    coutLn("3: Random Dump Only (address/privatekey files)");
    std::cin >> modeInput;

    if (modeInput < 1 || modeInput > 3) {
        coutLn("Invalid option selected. Exiting.");
        return 1;
    }

    coutLn("::Enter Number Of Addresses To Process::");
    std::cin >> totalAddresses;
    if (totalAddresses <= 0) {
        coutLn("Address count must be > 0.");
        return 1;
    }

    const BruteMode mode = static_cast<BruteMode>(modeInput);
    const int threadCount = effectiveThreadCount(totalAddresses);
    const std::vector<long long> work = splitWork(totalAddresses, threadCount);

    gProcessed = 0;
    gFound = 0;
    gRunning = true;

    const auto start = std::chrono::steady_clock::now();
    std::thread progressThread(showProgress, totalAddresses, start);

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (long long chunk : work) {
        if (chunk > 0) {
            workers.emplace_back(runWorker, chunk, mode);
        }
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    gRunning = false;
    progressThread.join();

    const auto end = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << std::endl;
    std::cout << "Completed with " << workers.size() << " thread(s) in " << durationMs << " ms." << std::endl;
    std::cout << "Processed: " << gProcessed.load() << ", Matches: " << gFound.load() << std::endl;
    if (mode != BruteMode::RandomDump) {
        std::cout << "Match file: " << kMatchOutputFile << std::endl;
        std::cout << "Seed file: " << kSeedOutputFile << std::endl;
    } else {
        std::cout << "Random outputs: " << kRandomAddressOutputFile << ", " << kRandomPrivateOutputFile << std::endl;
    }

    return 0;
}
