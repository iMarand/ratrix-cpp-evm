// Ratrix Wallet - secure command-line wallet
//
// Generates and manages your OWN wallets (EVM + Bitcoin from one private key),
// with secrets encrypted at rest and read-only balance lookups. Replaces the
// old plaintext-JSON CLI (now in legacy/old-cli-bin).

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "../core/rtx.h"
#include "../core/balance/BtcBalance.h"
#include "../core/balance/TokenBalance.h"
#include "console.h"
#include "walletstore.h"

namespace fs = std::filesystem;
using namespace con;
namespace store = rtxstore;

// --------------------------------------------------------------------------
// Point libcurl at a CA bundle so TLS verification works on dev machines.
// Prefers an already-set RATRIX_CAINFO; otherwise searches for the bundled cert.
static void setupTls(const char* argv0) {
    if (const char* s = std::getenv("RATRIX_CAINFO"); s && *s) return;
    std::vector<fs::path> roots;
    std::error_code ec;
    try {
        fs::path exe = fs::absolute(fs::path(argv0), ec);
        // Walk up to the filesystem root. parent_path() of a root (e.g. "D:/")
        // returns itself, so stop when it stops changing - otherwise infinite loop.
        for (fs::path d = exe.parent_path(); !d.empty();) {
            roots.push_back(d);
            fs::path up = d.parent_path();
            if (up == d) break;
            d = up;
        }
    } catch (...) {}
    roots.push_back(fs::current_path(ec));
    for (const auto& r : roots) {
        fs::path cand = r / "core" / "balance" / "curl-ca-bundle.crt";
        if (fs::exists(cand)) {
#ifdef _WIN32
            _putenv_s("RATRIX_CAINFO", cand.string().c_str());
#else
            setenv("RATRIX_CAINFO", cand.string().c_str(), 1);
#endif
            return;
        }
    }
}

static void banner() {
    std::cout << BOLD << CYAN
              << "\n  ____       _        _      \n"
              << " |  _ \\ __ _| |_ _ __(_)_  __\n"
              << " | |_) / _` | __| '__| \\ \\/ /\n"
              << " |  _ < (_| | |_| |  | |>  < \n"
              << " |_| \\_\\__,_|\\__|_|  |_/_/\\_\\\n"
              << RESET << GREY << "  Ratrix Wallet - secure multi-chain CLI\n" << RESET << "\n";
}

static void help() {
    banner();
    auto row = [](const char* c, const char* d) {
        std::cout << "  " << GREEN << c << RESET << std::string(std::max<int>(1, 34 - (int)std::string(c).size()), ' ')
                  << GREY << d << RESET << "\n";
    };
    std::cout << BOLD << "USAGE\n" << RESET;
    row("ratrix new <name>", "create a new HD wallet (EVM + BTC)");
    row("ratrix import <name> --pk <hex>", "import from a private key");
    row("ratrix import <name> --seed \"<words>\"", "import from a seed phrase");
    row("ratrix import <name> --entropy <hex>", "import from entropy");
    row("ratrix watch <name> <address>", "track an address (no keys, read-only)");
    row("ratrix list", "list your wallets");
    row("ratrix show <name> [--secret]", "show addresses (optionally reveal secret)");
    row("ratrix balance <name|address> [chain]", "balance: --eth --bnb --btc --usdt --usdc [--bsc]");
    row("ratrix export <name>", "reveal private key / WIF (asks passphrase)");
    row("ratrix remove <name> [--yes]", "delete a wallet file");
    std::cout << "\n" << GREY << "  Wallets: " << store::walletDir().string() << RESET << "\n\n";
}

// Prompt + confirm a passphrase for encryption.
static std::string newPassphrase() {
    std::string a = readSecret("  Set a passphrase to encrypt this wallet: ");
    if (a.empty()) throw std::runtime_error("passphrase cannot be empty");
    std::string b = readSecret("  Confirm passphrase: ");
    if (a != b) throw std::runtime_error("passphrases do not match");
    return a;
}

static void printAddresses(const store::Wallet& w) {
    std::cout << "  " << YELLOW << "ETH/BNB " << RESET << (w.eth.empty() ? GREY "(none)" RESET : w.eth) << "\n";
    if (!w.btcSegwit.empty()) std::cout << "  " << YELLOW << "BTC     " << RESET << w.btcSegwit << GREY " (segwit)" RESET << "\n";
    if (!w.btcLegacy.empty()) std::cout << "  " << YELLOW << "BTC     " << RESET << w.btcLegacy << GREY " (legacy)" RESET << "\n";
}

// Build a wallet (addresses) from a private key.
static store::Wallet walletFromPriv(const std::string& name, const std::string& type, const std::string& priv) {
    store::Wallet w;
    w.name = name;
    w.type = type;
    w.encrypted = true;
    w.eth = RTX::toAddress(priv);
    w.btcSegwit = RTX::toBtcAddress(priv);
    w.btcLegacy = RTX::toBtcLegacyAddress(priv);
    return w;
}

static int cmdNew(const std::string& name) {
    if (store::exists(name)) throw std::runtime_error("wallet '" + name + "' already exists");
    std::string entropy = RTX::randEntropy(32);
    std::string phrase = RTX::toSeedPhrase(entropy);
    std::string seed = RTX::toSeed(phrase);
    std::string priv = RTX::toPrivateKey(seed);

    store::Wallet w = walletFromPriv(name, "HD", priv);
    std::string pass = newPassphrase();
    store::save(w, {priv, phrase, entropy}, pass);

    std::cout << GREEN << "\n  Created wallet '" << name << "'\n" << RESET;
    printAddresses(w);
    std::cout << "\n  " << YELLOW << "Seed phrase (write it down, shown once):\n  " << RESET << phrase << "\n\n";
    return 0;
}

static int cmdImport(const std::vector<std::string>& a) {
    if (a.size() < 4) throw std::runtime_error("usage: ratrix import <name> --pk|--seed|--entropy <value>");
    const std::string& name = a[2];
    const std::string& flag = a[3];
    if (store::exists(name)) throw std::runtime_error("wallet '" + name + "' already exists");
    if (a.size() < 5) throw std::runtime_error("missing value after " + flag);
    const std::string& value = a[4];

    std::string priv, phrase, entropy, type;
    if (flag == "--pk") {
        priv = value;
        if (RTX::toAddress(priv).rfind("Error", 0) == 0) throw std::runtime_error("invalid private key");
        type = "IMPORTED_PK";
    } else if (flag == "--seed") {
        phrase = value;
        std::string seed = RTX::toSeed(phrase);
        priv = RTX::toPrivateKey(seed);
        type = "IMPORTED_SEED";
    } else if (flag == "--entropy") {
        entropy = value;
        phrase = RTX::toSeedPhrase(entropy);
        if (phrase.rfind("Error", 0) == 0) throw std::runtime_error("invalid entropy");
        priv = RTX::toPrivateKey(RTX::toSeed(phrase));
        type = "IMPORTED_SEED";
    } else {
        throw std::runtime_error("unknown flag: " + flag);
    }

    store::Wallet w = walletFromPriv(name, type, priv);
    std::string pass = newPassphrase();
    store::save(w, {priv, phrase, entropy}, pass);
    std::cout << GREEN << "\n  Imported wallet '" << name << "'\n" << RESET;
    printAddresses(w);
    std::cout << "\n";
    return 0;
}

static int cmdWatch(const std::vector<std::string>& a) {
    if (a.size() < 4) throw std::runtime_error("usage: ratrix watch <name> <address>");
    const std::string& name = a[2];
    std::string addr = a[3];
    if (store::exists(name)) throw std::runtime_error("wallet '" + name + "' already exists");

    store::Wallet w;
    w.name = name;
    w.type = "WATCH";
    w.encrypted = false;
    if (addr.rfind("0x", 0) == 0 || addr.rfind("0X", 0) == 0) w.eth = addr;
    else if (addr.rfind("bc1", 0) == 0) w.btcSegwit = addr;
    else if (!addr.empty() && (addr[0] == '1' || addr[0] == '3')) w.btcLegacy = addr;
    else throw std::runtime_error("unrecognized address format: " + addr);

    store::save(w, {}, "");
    std::cout << GREEN << "\n  Watching '" << name << "'\n" << RESET;
    printAddresses(w);
    std::cout << "\n";
    return 0;
}

static int cmdList() {
    auto names = store::list();
    if (names.empty()) { std::cout << GREY << "  no wallets yet - try: ratrix new <name>\n" << RESET; return 0; }
    std::cout << BOLD << "\n  Wallets (" << names.size() << ")\n" << RESET;
    for (const auto& n : names) {
        store::Wallet w = store::meta(store::loadRaw(n));
        const char* lock = w.encrypted ? (CYAN "encrypted") : (GREY "watch-only");
        std::cout << "  " << BOLD << n << RESET << GREY " [" << w.type << "] " << RESET << lock << RESET << "\n";
        std::string primary = !w.eth.empty() ? w.eth : (!w.btcSegwit.empty() ? w.btcSegwit : w.btcLegacy);
        std::cout << GREY << "    " << primary << RESET << "\n";
    }
    std::cout << "\n";
    return 0;
}

static int cmdShow(const std::vector<std::string>& a) {
    if (a.size() < 3) throw std::runtime_error("usage: ratrix show <name> [--secret]");
    const std::string& name = a[2];
    store::Wallet w = store::meta(store::loadRaw(name));
    std::cout << BOLD << "\n  " << name << RESET << GREY " [" << w.type << "]\n" << RESET;
    printAddresses(w);
    bool wantSecret = a.size() > 3 && a[3] == "--secret";
    if (wantSecret) {
        if (!w.encrypted) throw std::runtime_error("watch-only wallet has no secret");
        std::string pass = readSecret("  Passphrase: ");
        store::Secret s = store::unlock(name, pass);
        std::cout << "\n  " << RED << "Private key: " << RESET << s.privateKey << "\n";
        if (!s.seedPhrase.empty()) std::cout << "  " << RED << "Seed phrase: " << RESET << s.seedPhrase << "\n";
    }
    std::cout << "\n";
    return 0;
}

static int cmdExport(const std::vector<std::string>& a) {
    if (a.size() < 3) throw std::runtime_error("usage: ratrix export <name>");
    const std::string& name = a[2];
    store::Wallet w = store::meta(store::loadRaw(name));
    if (!w.encrypted) throw std::runtime_error("watch-only wallet has no secret to export");
    std::string pass = readSecret("  Passphrase: ");
    store::Secret s = store::unlock(name, pass);
    std::cout << "\n  " << RED << "Private key : " << RESET << s.privateKey << "\n";
    std::cout << "  " << RED << "BTC WIF     : " << RESET << RTX::toBtcWIF(s.privateKey) << "\n";
    if (!s.seedPhrase.empty()) std::cout << "  " << RED << "Seed phrase : " << RESET << s.seedPhrase << "\n";
    std::cout << "\n";
    return 0;
}

static int cmdRemove(const std::vector<std::string>& a) {
    if (a.size() < 3) throw std::runtime_error("usage: ratrix remove <name> [--yes]");
    const std::string& name = a[2];
    if (!store::exists(name)) throw std::runtime_error("wallet not found: " + name);
    bool yes = a.size() > 3 && a[3] == "--yes";
    if (!yes && !confirm("  Delete wallet '" + name + "'? This cannot be undone.")) {
        std::cout << GREY << "  cancelled\n" << RESET;
        return 0;
    }
    store::remove(name);
    std::cout << RED << "  Removed '" << name << "'\n" << RESET;
    return 0;
}

static std::string resolveTarget(const std::string& nameOrAddr, bool btc) {
    if (nameOrAddr.rfind("0x", 0) == 0 || nameOrAddr.rfind("bc1", 0) == 0 ||
        (!nameOrAddr.empty() && (nameOrAddr[0] == '1' || nameOrAddr[0] == '3')))
        return nameOrAddr;  // already an address
    store::Wallet w = store::meta(store::loadRaw(nameOrAddr));
    return btc ? (w.btcSegwit.empty() ? w.btcLegacy : w.btcSegwit) : w.eth;
}

static int cmdBalance(const std::vector<std::string>& a) {
    if (a.size() < 3) throw std::runtime_error("usage: ratrix balance <name|address> [--eth|--bnb|--btc|--usdt|--usdc] [--bsc]");
    const std::string& target = a[2];
    std::string chain = "--eth";
    bool bsc = false;
    for (size_t i = 3; i < a.size(); ++i) {
        if (a[i] == "--bsc") bsc = true; else chain = a[i];
    }

    if (chain == "--btc") {
        RTX_BALANCE::BtcBalanceChecker c;
        std::string addr = resolveTarget(target, true);
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " BTC\n" << RESET;
        return 0;
    }

    std::string addr = resolveTarget(target, false);
    if (chain == "--eth") {
        RTX_BALANCE::EthereumBalanceChecker c;
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " ETH\n" << RESET;
    } else if (chain == "--bnb") {
        RTX_BALANCE::BNBBalanceChecker c;
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " BNB\n" << RESET;
    } else if (chain == "--usdt") {
        RTX_BALANCE::TokenBalanceChecker c(bsc ? RTX_BALANCE::tokens::bscUSDT() : RTX_BALANCE::tokens::ethUSDT());
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " USDT" << (bsc ? " (BSC)" : "") << "\n" << RESET;
    } else if (chain == "--usdc") {
        RTX_BALANCE::TokenBalanceChecker c(bsc ? RTX_BALANCE::tokens::bscUSDC() : RTX_BALANCE::tokens::ethUSDC());
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " USDC" << (bsc ? " (BSC)" : "") << "\n" << RESET;
    } else {
        throw std::runtime_error("unknown chain flag: " + chain);
    }
    return 0;
}

int main(int argc, char** argv) {
    enableAnsi();
    setupTls(argc > 0 ? argv[0] : "");
    std::vector<std::string> a(argv, argv + argc);
    if (a.size() < 2) { help(); return 1; }

    try {
        const std::string& cmd = a[1];
        if (cmd == "new") return cmdNew(a.size() > 2 ? a[2] : throw std::runtime_error("usage: ratrix new <name>"));
        if (cmd == "import") return cmdImport(a);
        if (cmd == "watch") return cmdWatch(a);
        if (cmd == "list") return cmdList();
        if (cmd == "show") return cmdShow(a);
        if (cmd == "export") return cmdExport(a);
        if (cmd == "remove") return cmdRemove(a);
        if (cmd == "balance") return cmdBalance(a);
        if (cmd == "help" || cmd == "--help" || cmd == "-h") { help(); return 0; }
        std::cerr << RED << "  unknown command: " << cmd << RESET << "\n";
        help();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << RED << "  error: " << e.what() << RESET << "\n";
        return 1;
    }
}
