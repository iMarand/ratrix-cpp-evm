// Ratrix Wallet - secure command-line wallet
//
// Generates and manages your OWN wallets (EVM, Bitcoin and Tron from one private
// key; Solana from the recovery phrase),
// with secrets encrypted at rest and read-only balance lookups. Replaces the
// old plaintext-JSON CLI (now in legacy/old-cli-bin).

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "../core/rtx.h"
#include "../core/address_kind.h"
#include "../core/balance/BtcBalance.h"
#include "../core/balance/SolBalance.h"
#include "../core/balance/TokenBalance.h"
#include "../core/balance/TronBalance.h"
#include "../core/send/send.h"
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
    row("ratrix new <name>", "create a new HD wallet (EVM, BTC, Tron, Solana)");
    row("ratrix import <name> --pk <hex>", "import from a private key");
    row("ratrix import <name> --seed \"<words>\"", "import from a seed phrase");
    row("ratrix import <name> --entropy <hex>", "import from entropy");
    row("ratrix watch <name> <address>", "track an address (no keys, read-only)");
    row("ratrix list", "list your wallets");
    row("ratrix show <name> [--secret]", "show addresses (optionally reveal secret)");
    row("ratrix balance <name|address> [chain]", "--eth --bnb --btc --trx --sol --usdt --usdc [--bsc|--tron|--sol]");
    row("ratrix export <name>", "reveal private key / WIF (asks passphrase)");
    row("ratrix send <name> <asset> <to> <amount|max>", "send eth|bnb|btc|trx|sol|usdt|usdc (reviews first)");
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

// The Tron address of a key wallet saved before Tron support is the EVM one
// re-encoded (same key); watch-only wallets track only what was entered.
static std::string tronOf(const store::Wallet& w) {
    if (!w.tron.empty() || !w.encrypted || w.eth.empty()) return w.tron;
    return tron::fromEvmAddress(w.eth);
}

static void printAddresses(const store::Wallet& w) {
    if (!w.eth.empty() || w.encrypted)
        std::cout << "  " << YELLOW << "ETH/BNB " << RESET << (w.eth.empty() ? GREY "(none)" RESET : w.eth) << "\n";
    if (!w.btcSegwit.empty()) std::cout << "  " << YELLOW << "BTC     " << RESET << w.btcSegwit << GREY " (segwit)" RESET << "\n";
    if (!w.btcLegacy.empty()) std::cout << "  " << YELLOW << "BTC     " << RESET << w.btcLegacy << GREY " (legacy)" RESET << "\n";
    if (const std::string t = tronOf(w); !t.empty())
        std::cout << "  " << YELLOW << "TRON    " << RESET << t << GREY " (TRX, USDT TRC-20)" RESET << "\n";
    if (!w.sol.empty()) std::cout << "  " << YELLOW << "SOLANA  " << RESET << w.sol << "\n";
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
    w.tron = tron::fromEvmAddress(w.eth);
    return w;
}

static int cmdNew(const std::string& name) {
    if (store::exists(name)) throw std::runtime_error("wallet '" + name + "' already exists");
    std::string entropy = RTX::randEntropy(32);
    std::string phrase = RTX::toSeedPhrase(entropy);
    std::string seed = RTX::toSeed(phrase);
    std::string priv = RTX::toPrivateKey(seed);

    store::Wallet w = walletFromPriv(name, "HD", priv);
    w.sol = sol::addressFromSeed(seed);
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

    std::string priv, phrase, entropy, type, seed;
    if (flag == "--pk") {
        priv = value;
        if (RTX::toAddress(priv).rfind("Error", 0) == 0) throw std::runtime_error("invalid private key");
        type = "IMPORTED_PK";
    } else if (flag == "--seed") {
        phrase = value;
        seed = RTX::toSeed(phrase);
        priv = RTX::toPrivateKey(seed);
        type = "IMPORTED_SEED";
    } else if (flag == "--entropy") {
        entropy = value;
        phrase = RTX::toSeedPhrase(entropy);
        if (phrase.rfind("Error", 0) == 0) throw std::runtime_error("invalid entropy");
        seed = RTX::toSeed(phrase);
        priv = RTX::toPrivateKey(seed);
        type = "IMPORTED_SEED";
    } else {
        throw std::runtime_error("unknown flag: " + flag);
    }

    store::Wallet w = walletFromPriv(name, type, priv);
    if (!seed.empty()) w.sol = sol::addressFromSeed(seed);  // no Solana key without a phrase
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
    switch (rtxaddr::classify(addr)) {
        case rtxaddr::Kind::Evm: w.eth = addr; break;
        case rtxaddr::Kind::BtcSegwit: w.btcSegwit = addr; break;
        case rtxaddr::Kind::BtcLegacy: w.btcLegacy = addr; break;
        case rtxaddr::Kind::Tron: w.tron = addr; break;
        case rtxaddr::Kind::Solana: w.sol = addr; break;
        case rtxaddr::Kind::Unknown: throw std::runtime_error("unrecognized address format: " + addr);
    }

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
        std::string primary = !w.eth.empty()         ? w.eth
                              : !w.btcSegwit.empty() ? w.btcSegwit
                              : !w.btcLegacy.empty() ? w.btcLegacy
                              : !w.tron.empty()      ? w.tron
                                                     : w.sol;
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

enum class Net { Evm, Btc, Tron, Sol };

// An address passes through; a wallet name resolves to its address on `net`.
static std::string resolveTarget(const std::string& nameOrAddr, Net net) {
    if (rtxaddr::classify(nameOrAddr) != rtxaddr::Kind::Unknown) return nameOrAddr;  // already an address
    store::Wallet w = store::meta(store::loadRaw(nameOrAddr));
    std::string addr;
    switch (net) {
        case Net::Evm: addr = w.eth; break;
        case Net::Btc: addr = w.btcSegwit.empty() ? w.btcLegacy : w.btcSegwit; break;
        case Net::Tron: addr = tronOf(w); break;
        case Net::Sol: addr = w.sol; break;
    }
    if (addr.empty()) throw std::runtime_error("wallet '" + nameOrAddr + "' has no address on that network");
    return addr;
}

static int cmdBalance(const std::vector<std::string>& a) {
    if (a.size() < 3)
        throw std::runtime_error(
            "usage: ratrix balance <name|address> [--eth|--bnb|--btc|--trx|--sol|--usdt|--usdc] [--bsc|--tron|--sol]");
    const std::string& target = a[2];
    // Flags in any order: an asset, plus for USDT/USDC the network
    // (--bsc, --tron, --sol; Ethereum by default).
    const std::vector<std::string> flags(a.begin() + 3, a.end());
    auto has = [&](const char* f) { return std::find(flags.begin(), flags.end(), f) != flags.end(); };
    for (const auto& f : flags)
        if (f != "--eth" && f != "--bnb" && f != "--btc" && f != "--trx" && f != "--sol" && f != "--usdt" &&
            f != "--usdc" && f != "--bsc" && f != "--tron")
            throw std::runtime_error("unknown flag: " + f);
    const bool bsc = has("--bsc"), onTron = has("--tron");
    const std::string chain = has("--usdt") ? "--usdt"
                              : has("--usdc") ? "--usdc"
                              : has("--btc")  ? "--btc"
                              : has("--trx")  ? "--trx"
                              : has("--sol")  ? "--sol"
                              : has("--bnb")  ? "--bnb"
                                              : "--eth";
    const bool onSol = chain == "--usdt" && has("--sol");
    auto show = [](const std::string& addr, const std::string& amount, const char* unit) {
        std::cout << "  " << addr << "\n  " << GREEN << amount << " " << unit << "\n" << RESET;
    };

    if (chain == "--btc") {
        const std::string addr = resolveTarget(target, Net::Btc);
        show(addr, RTX_BALANCE::BtcBalanceChecker().getBalance(addr), "BTC");
        return 0;
    }
    if (chain == "--trx" || (chain == "--usdt" && onTron)) {
        const std::string addr = resolveTarget(target, Net::Tron);
        RTX_BALANCE::TronBalanceChecker c;
        if (chain == "--trx") show(addr, c.getBalance(addr), "TRX");
        else show(addr, c.getTokenBalance(RTX_BALANCE::tokens::tronUSDT(), addr), "USDT (TRC-20)");
        return 0;
    }
    if (chain == "--sol" || onSol) {
        const std::string addr = resolveTarget(target, Net::Sol);
        RTX_BALANCE::SolBalanceChecker c;
        if (onSol) show(addr, c.getTokenBalance(sol::kUsdtMint, 6, addr), "USDT (SPL)");
        else show(addr, c.getBalance(addr), "SOL");
        return 0;
    }

    std::string addr = resolveTarget(target, Net::Evm);
    if (chain == "--eth") {
        RTX_BALANCE::EthereumBalanceChecker c;
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " ETH\n" << RESET;
    } else if (chain == "--bnb") {
        RTX_BALANCE::BNBBalanceChecker c;
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " BNB\n" << RESET;
    } else if (chain == "--usdt") {
        RTX_BALANCE::TokenBalanceChecker c(bsc ? RTX_BALANCE::tokens::bscUSDT() : RTX_BALANCE::tokens::ethUSDT());
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " USDT" << (bsc ? " (BSC)" : " (ERC-20)") << "\n" << RESET;
    } else if (chain == "--usdc") {
        RTX_BALANCE::TokenBalanceChecker c(bsc ? RTX_BALANCE::tokens::bscUSDC() : RTX_BALANCE::tokens::ethUSDC());
        std::cout << "  " << addr << "\n  " << GREEN << c.getBalance(addr) << " USDC" << (bsc ? " (BSC)" : "") << "\n" << RESET;
    } else {
        throw std::runtime_error("unknown chain flag: " + chain);
    }
    return 0;
}

// ratrix send <wallet> <asset> <to> <amount|max>
// Reviews first (amount + network fee, no passphrase needed), asks to confirm,
// then decrypts the key and signs + broadcasts. For USDT the recipient address
// picks the network, so it can't go out on the wrong one.
static int cmdSend(const std::vector<std::string>& a) {
    if (a.size() < 6)
        throw std::runtime_error("usage: ratrix send <wallet> <eth|bnb|btc|trx|sol|usdt|usdc> <to> <amount|max>");
    const std::string& name = a[2];
    std::string asset = a[3];
    for (char& c : asset) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string& to = a[4];
    store::Wallet w = store::meta(store::loadRaw(name));
    if (!w.encrypted) throw std::runtime_error("'" + name + "' is watch-only: it holds no key to send with");

    using A = rtxsend::Asset;
    rtxsend::Params p;
    p.ethAddress = w.eth;
    p.btcAddress = w.btcSegwit.empty() ? w.btcLegacy : w.btcSegwit;
    p.tronAddress = tronOf(w);
    p.solAddress = w.sol;
    p.to = to;
    p.max = a[5] == "max";
    p.amount = p.max ? "" : a[5];
    std::string network;
    if (asset == "eth") p.asset = A::Eth, network = "Ethereum";
    else if (asset == "bnb") p.asset = A::Bnb, network = "BNB Smart Chain";
    else if (asset == "btc") p.asset = A::Btc, network = "Bitcoin";
    else if (asset == "trx") p.asset = A::Trx, network = "Tron";
    else if (asset == "sol") p.asset = A::Sol, network = "Solana";
    else if (asset == "usdc") p.asset = A::UsdcEth, network = "Ethereum (ERC-20)";
    else if (asset == "usdt") {
        switch (rtxaddr::classify(to)) {
            case rtxaddr::Kind::Evm: p.asset = A::UsdtEth, network = "Ethereum (ERC-20)"; break;
            case rtxaddr::Kind::Tron: p.asset = A::UsdtTrx, network = "Tron (TRC-20)"; break;
            case rtxaddr::Kind::Solana: p.asset = A::UsdtSol, network = "Solana (SPL)"; break;
            default: throw std::runtime_error("for USDT, enter a 0x…, T… or Solana address (the address picks the network)");
        }
    } else {
        throw std::runtime_error("unknown asset '" + a[3] + "' (eth, bnb, btc, trx, sol, usdt, usdc)");
    }
    const bool solana = p.asset == A::Sol || p.asset == A::UsdtSol;
    if (solana && w.sol.empty())
        throw std::runtime_error("this wallet has no Solana address (it was imported from a private key)");

    std::cout << GREY << "  Reviewing...\n" << RESET;
    const rtxsend::Result review = rtxsend::run(p);
    std::cout << "\n  " << YELLOW << "Send    " << RESET << BOLD << review.amountStr << " " << review.amountSym << RESET
              << GREY << "  on " << network << RESET << "\n";
    std::cout << "  " << YELLOW << "To      " << RESET << to << "\n";
    std::cout << "  " << YELLOW << "Fee     " << RESET << review.feeStr << " " << review.feeSym << "\n";
    if (!review.note.empty()) std::cout << "  " << GREY << review.note << RESET << "\n";
    std::cout << "\n";
    if (!confirm("  Send this transaction?")) {
        std::cout << GREY << "  cancelled\n" << RESET;
        return 0;
    }

    std::string pass = readSecret("  Passphrase: ");
    store::Secret s = store::unlock(name, pass);
    pass.assign(pass.size(), '\0');
    if (solana && !s.seedPhrase.empty()) {
        std::array<uint8_t, 32> k = sol::accountKey(RTX::toSeed(s.seedPhrase));
        p.solKeyHex = rtxsend::tron::hex(k.data(), k.size());
        OPENSSL_cleanse(k.data(), k.size());
    } else if (!solana) {
        p.privHex = s.privateKey;
    }
    for (std::string* f : {&s.privateKey, &s.seedPhrase, &s.entropy}) f->assign(f->size(), '\0');
    if (p.privHex.empty() && p.solKeyHex.empty()) throw std::runtime_error("this wallet has no key for that network");

    std::cout << GREY << "  Signing and broadcasting...\n" << RESET;
    rtxsend::Result r;
    try {
        r = rtxsend::run(p);
    } catch (...) {
        p.privHex.assign(p.privHex.size(), '\0');
        p.solKeyHex.assign(p.solKeyHex.size(), '\0');
        throw;
    }
    p.privHex.assign(p.privHex.size(), '\0');
    p.solKeyHex.assign(p.solKeyHex.size(), '\0');
    std::cout << GREEN << "\n  Sent " << r.amountStr << " " << r.amountSym << RESET << GREY << " (fee " << r.feeStr << " "
              << r.feeSym << ")\n" << RESET;
    std::cout << "  " << YELLOW << "Tx      " << RESET << r.txid << "\n";
    std::cout << "  " << YELLOW << "View    " << RESET << r.explorerUrl << "\n\n";
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
        if (cmd == "send") return cmdSend(a);
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
