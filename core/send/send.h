#ifndef RTX_SEND_SEND_H
#define RTX_SEND_SEND_H

// High-level "send funds" orchestration tying together amount parsing, fee
// estimation, transaction building, signing and broadcast for each asset. The
// same code path produces the review estimate and performs the real send (the
// latter re-fetches nonce/UTXOs and supplies the private key), so what the user
// confirms matches what is broadcast.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "bignum.h"
#include "btc_rpc.h"
#include "btc_tx.h"
#include "evm.h"
#include "evm_rpc.h"

namespace rtxsend {

enum class Asset { Eth, Bnb, Btc, UsdtEth, UsdcEth };

struct Params {
    Asset asset;
    std::string ethAddress;  // sender EVM address (for Eth/Bnb/tokens)
    std::string btcAddress;  // sender P2WPKH address (for Btc)
    std::string to;          // recipient
    std::string amount;      // decimal string (ignored when max)
    bool max = false;        // send the entire spendable balance
    std::string privHex;     // only set when actually broadcasting
};

struct Result {
    // Review fields (always set):
    std::string amountStr, amountSym;  // what the recipient receives
    std::string feeStr, feeSym;        // network fee (paid in the chain's coin)
    std::string note;                  // e.g. "You have X, this sends the maximum."
    // Broadcast fields (set when privHex was supplied):
    std::string txid;
    std::string explorerUrl;
};

namespace detail {

inline int tokenDecimals(Asset) { return 6; }  // USDT and USDC on Ethereum both use 6
inline std::string tokenContract(Asset a) {
    return a == Asset::UsdtEth ? "0xdAC17F958D2ee523a2206206994597C13D831ec7"
                               : "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48";
}

inline std::string evmExplorer(const evm::Chain& c, const std::string& txid) {
    return (c.chainId == 56 ? "https://bscscan.com/tx/" : "https://etherscan.io/tx/") + txid;
}

inline Result runEvmNative(const Params& p, const evm::Chain& chain, const char* sym) {
    const Big gasPrice = evm::getGasPrice(chain);
    const Big gasLimit(21000);
    const Big fee = gasPrice * gasLimit;
    const Big balance = evm::getBalance(chain, p.ethAddress);

    Big value;
    Result r;
    if (p.max) {
        if (balance <= fee) throw std::runtime_error("Balance doesn't cover the network fee");
        value = balance - fee;
        r.note = "Sends your full balance minus the network fee.";
    } else {
        value = parseUnits(p.amount, 18);
        if (value + fee > balance) throw std::runtime_error("Not enough " + std::string(sym) + " for this amount plus the fee");
    }
    r.amountStr = formatUnits(value, 18, 8);
    r.amountSym = sym;
    r.feeStr = formatUnits(fee, 18, 8);
    r.feeSym = sym;
    if (!p.privHex.empty()) {
        const Big nonce = evm::getNonce(chain, p.ethAddress);
        evm::Tx tx{nonce, gasPrice, gasLimit, value, evm::parseAddress(p.to), {}, chain.chainId};
        r.txid = evm::sendRaw(chain, evm::buildSigned(tx, p.privHex));
        r.explorerUrl = evmExplorer(chain, r.txid);
    }
    return r;
}

inline Result runErc20(const Params& p) {
    const evm::Chain chain = evm::ethChain();
    const int dec = tokenDecimals(p.asset);
    const std::string token = tokenContract(p.asset);
    const char* sym = p.asset == Asset::UsdtEth ? "USDT" : "USDC";

    const Big tokenBal = evm::tokenBalance(chain, token, p.ethAddress);
    Big amount = p.max ? tokenBal : parseUnits(p.amount, dec);
    if (amount.isZero()) throw std::runtime_error("Nothing to send");
    if (amount > tokenBal) throw std::runtime_error(std::string("Not enough ") + sym);

    const Bytes data = evm::erc20Transfer(p.to, amount);
    const Big gasPrice = evm::getGasPrice(chain);
    Big gasLimit;
    try {
        gasLimit = evm::estimateGas(chain, p.ethAddress, token, Big(0), data);
        gasLimit = gasLimit + gasLimit.divCeil(5);  // +20% headroom
    } catch (...) {
        gasLimit = Big(90000);  // typical ERC-20 transfer
    }
    const Big fee = gasPrice * gasLimit;
    const Big ethBal = evm::getBalance(chain, p.ethAddress);
    if (fee > ethBal) throw std::runtime_error("Not enough ETH to pay the network fee");

    Result r;
    r.amountStr = formatUnits(amount, dec, dec);
    r.amountSym = sym;
    r.feeStr = formatUnits(fee, 18, 8);
    r.feeSym = "ETH";
    if (p.max) r.note = "Sends your full " + std::string(sym) + " balance.";
    if (!p.privHex.empty()) {
        const Big nonce = evm::getNonce(chain, p.ethAddress);
        evm::Tx tx{nonce, gasPrice, gasLimit, Big(0), evm::parseAddress(token), data, chain.chainId};
        r.txid = evm::sendRaw(chain, evm::buildSigned(tx, p.privHex));
        r.explorerUrl = evmExplorer(chain, r.txid);
    }
    return r;
}

inline Result runBtc(const Params& p) {
    using namespace rtxsend::btc;
    std::vector<Utxo> utxos = fetchUtxos(p.btcAddress);
    uint64_t have = 0;
    for (const auto& u : utxos) have += u.value;
    if (utxos.empty()) throw std::runtime_error("This address has no confirmed coins to spend");

    const double rate = feeRate();
    const Bytes toScript = scriptForAddress(p.to);
    const Bytes changeScript = scriptForAddress(p.btcAddress);
    constexpr uint64_t kDust = 546;

    Result r;
    r.amountSym = r.feeSym = "BTC";

    uint64_t amount = p.max ? 0 : parseUnits(p.amount, 8).toU64();
    std::vector<Utxo> ins;
    uint64_t fee = 0, change = 0;

    if (p.max) {
        ins = utxos;  // spend everything, single output, no change
        fee = static_cast<uint64_t>(std::ceil(estimateVsize(ins.size(), 1) * rate));
        if (have <= fee) throw std::runtime_error("Balance doesn't cover the network fee");
        amount = have - fee;
        r.note = "Sends your full balance minus the network fee.";
    } else {
        if (amount == 0) throw std::runtime_error("Enter an amount");
        // Grow the input set (largest coins first) until it covers amount + fee.
        std::sort(utxos.begin(), utxos.end(), [](const Utxo& a, const Utxo& b) { return a.value > b.value; });
        uint64_t total = 0;
        for (size_t n = 1; n <= utxos.size(); ++n) {
            total += utxos[n - 1].value;
            const uint64_t feeWithChange = static_cast<uint64_t>(std::ceil(estimateVsize(n, 2) * rate));
            if (total >= amount + feeWithChange) {
                ins.assign(utxos.begin(), utxos.begin() + n);
                change = total - amount - feeWithChange;
                if (change < kDust) {  // change too small to be worth a second output
                    fee = total - amount;
                    change = 0;
                } else {
                    fee = feeWithChange;
                }
                break;
            }
        }
        if (ins.empty()) throw std::runtime_error("Not enough BTC for this amount plus the network fee");
    }

    std::vector<Output> outs = {{toScript, amount}};
    if (change > 0) outs.push_back({changeScript, change});

    auto sats = [](uint64_t v) { return formatUnits(Big(v), 8, 8); };
    r.amountStr = sats(amount);
    r.feeStr = sats(fee);
    if (!p.privHex.empty()) {
        const Signed tx = buildSigned(ins, outs, p.privHex);
        r.txid = broadcast(tx.rawHex);
        if (r.txid.size() != 64) r.txid = tx.txid;  // some nodes echo nothing useful
        r.explorerUrl = "https://mempool.space/tx/" + r.txid;
    }
    return r;
}

}  // namespace detail

// Validates `to` for the asset's chain; throws with a friendly message.
inline void validateRecipient(Asset a, const std::string& to) {
    if (to.empty()) throw std::runtime_error("Enter a recipient address");
    if (a == Asset::Btc) {
        try {
            (void)btc::scriptForAddress(to);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Recipient: ") + e.what());
        }
    } else {
        (void)evm::parseAddress(to);  // length/hex
        if (!evm::checksumOk(to)) throw std::runtime_error("That address fails its checksum — please re-check it");
    }
}

// One entry point for both review (privHex empty) and send (privHex set).
inline Result run(const Params& p) {
    validateRecipient(p.asset, p.to);
    switch (p.asset) {
        case Asset::Eth: return detail::runEvmNative(p, evm::ethChain(), "ETH");
        case Asset::Bnb: return detail::runEvmNative(p, evm::bscChain(), "BNB");
        case Asset::UsdtEth:
        case Asset::UsdcEth: return detail::runErc20(p);
        case Asset::Btc: return detail::runBtc(p);
    }
    throw std::runtime_error("Unsupported asset");
}

}  // namespace rtxsend

#endif  // RTX_SEND_SEND_H
