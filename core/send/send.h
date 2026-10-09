#ifndef RTX_SEND_SEND_H
#define RTX_SEND_SEND_H

// High-level "send funds" orchestration tying together amount parsing, fee
// estimation, transaction building, signing and broadcast for each asset. The
// same code path produces the review estimate and performs the real send (the
// latter re-fetches nonce/UTXOs and supplies the private key), so what the user
// confirms matches what is broadcast.

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <stdexcept>
#include <string>

#include "../solana.h"
#include "../tron.h"
#include "bignum.h"
#include "btc_rpc.h"
#include "btc_tx.h"
#include "evm.h"
#include "evm_rpc.h"
#include "sol_rpc.h"
#include "sol_tx.h"
#include "tron_rpc.h"
#include "tron_tx.h"

namespace rtxsend {

enum class Asset { Eth, Bnb, Btc, UsdtEth, UsdcEth, Trx, UsdtTrx, Sol, UsdtSol };

struct Params {
    Asset asset;
    std::string ethAddress;   // sender EVM address (for Eth/Bnb/tokens)
    std::string btcAddress;   // sender P2WPKH address (for Btc)
    std::string tronAddress;  // sender "T..." address (for Trx/UsdtTrx); same key as ethAddress
    std::string solAddress;   // sender Solana address (for Sol/UsdtSol); its own ed25519 key
    std::string to;          // recipient
    std::string amount;      // decimal string (ignored when max)
    bool max = false;        // send the entire spendable balance
    // Only set when actually broadcasting: the secp256k1 key (EVM, BTC, Tron)
    // or, for Solana, the ed25519 key derived from the recovery phrase.
    std::string privHex;
    std::string solKeyHex;
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
    if (amount > tokenBal) throw std::runtime_error(std::string("Not enough ") + sym + " on Ethereum");

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

// ------------------------------- Tron -------------------------------------
// Tron has no gas price: a transaction consumes bandwidth (its size in bytes)
// and, for contract calls, energy. Both come free from the account's daily
// allowance or staked TRX; whatever isn't covered is paid by burning TRX.

constexpr const char* kTronUsdt = "TR7NHqjeKQxGTCi8q8ZY4pL8otSzgjLj6t";  // USDT (TRC-20)
constexpr uint64_t kTrx = 1000000;                                    // sun per TRX

inline std::string trxStr(uint64_t sun) { return formatUnits(Big(sun), 6, 6); }

inline uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

// Checks the sender is set and, when signing, that the key really controls it.
inline void checkTronSender(const Params& p) {
    if (p.tronAddress.empty()) throw std::runtime_error("This wallet has no Tron address");
    if (p.to == p.tronAddress) throw std::runtime_error("That's this wallet's own address");
    if (!p.privHex.empty() && tron::addressOf(p.privHex) != p.tronAddress)
        throw std::runtime_error("This wallet's key doesn't match its Tron address");
}

// TRX burned for bandwidth: nothing if the free allowance or staked bandwidth
// covers the whole transaction (each is tried on its own), else every byte.
inline uint64_t bandwidthFee(const tron::Resources& res, const tron::Prices& pr, uint64_t bytes) {
    return (res.stakedBandwidth >= bytes || res.freeBandwidth >= bytes) ? 0 : bytes * pr.perBandwidthByte;
}

// An amount in base units (sun, lamports), within the int64 the chains allow.
inline uint64_t toU64(const Big& v) {
    const uint64_t s = v.toU64();
    if (s > static_cast<uint64_t>(INT64_MAX)) throw std::runtime_error("Amount is too large");
    return s;
}

inline Result runTrx(const Params& p) {
    checkTronSender(p);
    const tron::Account from = tron::account(p.tronAddress);
    if (!from.exists) throw std::runtime_error("This Tron address has no TRX yet");
    const tron::Account to = tron::account(p.to);
    const tron::Prices pr = tron::prices();
    const tron::Resources res = tron::resources(p.tronAddress);
    const tron::BlockRef ref = tron::latestBlock();
    const uint64_t now = nowMs();

    // Sending to an address that has never held TRX activates it, which costs
    // a flat fee plus bandwidth that only staked bandwidth can cover.
    auto feeFor = [&](uint64_t amount) {
        const uint64_t bytes = tron::bandwidth(tron::build(ref, tron::transferContract(p.tronAddress, p.to, amount), now));
        if (!to.exists) return pr.activation + (res.stakedBandwidth >= bytes ? 0 : pr.createAccount);
        return bandwidthFee(res, pr, bytes);
    };

    Result r;
    uint64_t amount, fee;
    if (p.max) {
        fee = feeFor(from.balanceSun);  // the largest amount gives the largest (upper-bound) size
        if (from.balanceSun <= fee) throw std::runtime_error("Balance doesn't cover the network fee");
        amount = from.balanceSun - fee;
        r.note = "Sends your full balance minus the network fee.";
    } else {
        amount = toU64(parseUnits(p.amount, 6));
        if (amount == 0) throw std::runtime_error("Enter an amount");
        fee = feeFor(amount);
        if (amount + fee > from.balanceSun) throw std::runtime_error("Not enough TRX for this amount plus the fee");
    }
    std::string why;
    if (!to.exists)
        why = "The recipient address hasn't been used yet; " + trxStr(pr.activation) +
              " TRX of the fee goes to activating it.";
    else if (fee == 0)
        why = "No fee: covered by this address's free daily bandwidth.";
    if (!why.empty()) r.note = r.note.empty() ? why : why + " " + r.note;

    r.amountStr = trxStr(amount);
    r.amountSym = "TRX";
    r.feeStr = trxStr(fee);
    r.feeSym = "TRX";
    if (!p.privHex.empty()) {
        const tron::Unsigned u = tron::build(ref, tron::transferContract(p.tronAddress, p.to, amount), now);
        const std::string txid = tron::hex(u.txid.data(), u.txid.size());
        r.txid = tron::broadcast(tron::sign(u, p.privHex), txid);
        r.explorerUrl = "https://tronscan.org/#/transaction/" + r.txid;
    }
    return r;
}

inline Result runTrc20(const Params& p) {
    checkTronSender(p);
    const Big tokenBal = tron::trc20Balance(kTronUsdt, p.tronAddress);
    const Big amount = p.max ? tokenBal : parseUnits(p.amount, 6);
    if (amount.isZero()) throw std::runtime_error("Nothing to send");
    if (amount > tokenBal) throw std::runtime_error("Not enough USDT on Tron");

    // transfer(address,uint256): the same ABI call as an ERC-20 transfer.
    const Bytes data = evm::erc20Transfer(::tron::toEvmHex(p.to), amount);
    const Bytes args(data.begin() + 4, data.end());
    const uint64_t energy = tron::simulate(p.tronAddress, kTronUsdt, "transfer(address,uint256)", args).energy;

    const tron::Account from = tron::account(p.tronAddress);
    if (!from.exists)
        throw std::runtime_error("This Tron address has no TRX. USDT transfers on Tron are paid for in TRX, "
                                 "so send it some TRX first.");
    const tron::Prices pr = tron::prices();
    const tron::Resources res = tron::resources(p.tronAddress);
    const tron::BlockRef ref = tron::latestBlock();
    const uint64_t now = nowMs();

    // fee_limit caps the TRX the call may burn, counting staked energy too. A
    // limit below what the call needs makes it fail with the fee still spent,
    // so allow 50% over the estimate, at least 30 TRX, at most 150 TRX.
    const uint64_t feeLimit = std::clamp<uint64_t>(energy * pr.perEnergy / 2 * 3, 30 * kTrx, 150 * kTrx);
    const tron::Unsigned u = tron::build(ref, tron::triggerContract(p.tronAddress, kTronUsdt, data), now, feeLimit);
    const uint64_t energyFee = energy > res.energy ? (energy - res.energy) * pr.perEnergy : 0;
    const uint64_t fee = energyFee + bandwidthFee(res, pr, tron::bandwidth(u));
    if (fee > from.balanceSun)
        throw std::runtime_error("Not enough TRX to pay the network fee (about " + trxStr(fee) +
                                 " TRX). USDT transfers on Tron are paid for in TRX.");

    Result r;
    r.amountStr = formatUnits(amount, 6, 6);
    r.amountSym = "USDT";
    r.feeStr = trxStr(fee);
    r.feeSym = "TRX";
    r.note = "Estimated: paid in TRX for the energy and bandwidth used. If the transfer needs more energy than "
             "expected, up to " + trxStr(feeLimit) + " TRX can be charged.";
    if (p.max) r.note = "Sends your full USDT balance. " + r.note;
    if (!p.privHex.empty()) {
        const std::string txid = tron::hex(u.txid.data(), u.txid.size());
        r.txid = tron::broadcast(tron::sign(u, p.privHex), txid);
        r.explorerUrl = "https://tronscan.org/#/transaction/" + r.txid;
    }
    return r;
}

// ------------------------------- Solana -----------------------------------
// The fee is a flat 5,000 lamports per signature plus a priority fee (price per
// compute unit times the compute-unit limit). Every account must also keep a
// rent minimum, or be emptied to zero.

constexpr uint32_t kSolTransferUnits = 10000;  // a System transfer uses a few hundred; generous on purpose

inline std::string solStr(uint64_t lamports) { return formatUnits(Big(lamports), 9, 9); }

// The ed25519 key, when signing (empty array for a review).
inline std::array<uint8_t, 32> solKey(const Params& p) {
    std::array<uint8_t, 32> k{};
    if (p.solKeyHex.empty()) return k;
    const std::vector<uint8_t> v = ::btc::hexToBytes(p.solKeyHex);
    if (v.size() != 32) throw std::runtime_error("Bad Solana key");
    std::copy(v.begin(), v.end(), k.begin());
    return k;
}

inline void checkSolSender(const Params& p) {
    if (p.solAddress.empty()) throw std::runtime_error("This wallet has no Solana address");
    if (p.to == p.solAddress) throw std::runtime_error("That's this wallet's own address");
    if (!p.solKeyHex.empty()) {
        std::array<uint8_t, 32> k = solKey(p);
        const bool match = ::sol::encode(::sol::publicKey(k)) == p.solAddress;
        OPENSSL_cleanse(k.data(), k.size());
        if (!match) throw std::runtime_error("This wallet's key doesn't match its Solana address");
    }
}

// The fee the network quotes for a message; computed the same way if the
// quote isn't available.
inline uint64_t solFee(const Bytes& message, uint64_t price, uint32_t units) {
    try {
        return sol::feeForMessage(message);
    } catch (const std::exception&) {
        return 5000 + (price * units + 999999) / 1000000;
    }
}

// Only accounts owned by the System program can pay fees or send SOL; one
// reassigned to another program (it happens to leaked keys) can't.
inline void checkSolPayer(const sol::AccountInfo& a) {
    if (a.exists && a.owner != ::sol::kSystemProgram)
        throw std::runtime_error("This Solana account is controlled by a program (" + a.owner +
                                 "), so it can't send or pay fees.");
}

inline void checkSolLeftover(uint64_t left, uint64_t rentMin) {
    if (left > 0 && left < rentMin)
        throw std::runtime_error("That would leave " + solStr(left) + " SOL, below the " + solStr(rentMin) +
                                 " SOL a Solana account must keep. Send a little less, or use Max.");
}

// Signs and broadcasts a compiled message; returns the transaction id.
inline std::string solBroadcast(const Params& p, const Bytes& message, Result& r) {
    std::array<uint8_t, 32> k = solKey(p);
    const sol::Signature sig = sol::signMessage(message, k);
    OPENSSL_cleanse(k.data(), k.size());
    r.txid = sol::send(sol::serialize(message, sig), ::btc::base58(std::vector<uint8_t>(sig.begin(), sig.end())));
    r.explorerUrl = "https://solscan.io/tx/" + r.txid;
    return r.txid;
}

inline Result runSol(const Params& p) {
    checkSolSender(p);
    const sol::Key from = ::sol::decode(p.solAddress), to = ::sol::decode(p.to);
    const sol::AccountInfo src = sol::accountInfo(p.solAddress);
    if (!src.exists || src.lamports == 0) throw std::runtime_error("This Solana address has no SOL");
    checkSolPayer(src);
    const sol::AccountInfo dst = sol::accountInfo(p.to);
    const uint64_t rentMin = sol::rentMinimum(0);
    const uint64_t price = sol::priorityPrice({p.solAddress, p.to});
    const sol::Key blockhash = sol::latestBlockhash();
    auto message = [&](uint64_t lamports) {
        return sol::compileMessage(from, {sol::setComputeUnitLimit(kSolTransferUnits), sol::setComputeUnitPrice(price),
                                          sol::transfer(from, to, lamports)},
                                   blockhash);
    };
    const uint64_t fee = solFee(message(0), price, kSolTransferUnits);  // the amount doesn't change the fee

    Result r;
    uint64_t amount;
    if (p.max) {
        if (src.lamports <= fee) throw std::runtime_error("Balance doesn't cover the network fee");
        amount = src.lamports - fee;
        r.note = "Sends your full balance minus the network fee.";
    } else {
        amount = toU64(parseUnits(p.amount, 9));
        if (amount == 0) throw std::runtime_error("Enter an amount");
        if (amount + fee > src.lamports) throw std::runtime_error("Not enough SOL for this amount plus the fee");
        checkSolLeftover(src.lamports - amount - fee, rentMin);
    }
    if (!dst.exists && amount < rentMin)
        throw std::runtime_error("This address hasn't been used yet, so it must receive at least " + solStr(rentMin) +
                                 " SOL (the Solana account minimum).");

    r.amountStr = solStr(amount);
    r.amountSym = "SOL";
    r.feeStr = solStr(fee);
    r.feeSym = "SOL";
    if (!p.solKeyHex.empty()) solBroadcast(p, message(amount), r);
    return r;
}

inline Result runSpl(const Params& p) {
    checkSolSender(p);
    const sol::Key owner = ::sol::decode(p.solAddress), to = ::sol::decode(p.to), mint = ::sol::decode(::sol::kUsdtMint);
    // Tokens go to the recipient's associated token account, derived from
    // their wallet address; a token account pasted here would be a dead end.
    const sol::AccountInfo toInfo = sol::accountInfo(p.to);
    if (toInfo.exists && toInfo.owner == ::sol::kTokenProgram)
        throw std::runtime_error("That's a token account, not a wallet address. Ask for their Solana wallet address.");

    const sol::Key srcAta = ::sol::associatedTokenAccount(owner, mint), dstAta = ::sol::associatedTokenAccount(to, mint);
    const sol::AccountInfo src = sol::accountInfo(::sol::encode(srcAta));
    uint64_t have = 0;
    if (src.exists) {
        const ::sol::TokenAccount t = ::sol::parseTokenAccount(src.data);
        // A reassigned owner means none of it is this wallet's to send.
        if (t.mint != mint) throw std::runtime_error("Unexpected USDT account for this wallet");
        if (t.owner == owner) have = t.amount;
    }
    const uint64_t amount = p.max ? have : toU64(parseUnits(p.amount, 6));
    if (amount == 0) throw std::runtime_error(have == 0 ? "No USDT on Solana to send" : "Nothing to send");
    if (amount > have) throw std::runtime_error("Not enough USDT on Solana");

    const bool create = !sol::accountInfo(::sol::encode(dstAta)).exists;
    const sol::AccountInfo payer = sol::accountInfo(p.solAddress);
    if (!payer.exists || payer.lamports == 0)
        throw std::runtime_error("This Solana address has no SOL. Sending USDT on Solana needs a little SOL for the fee" +
                                 std::string(create ? " and to open the recipient's USDT account." : "."));
    checkSolPayer(payer);
    const uint64_t rentMin = sol::rentMinimum(0);
    const uint64_t accountRent = create ? sol::rentMinimum(165) : 0;  // a token account is 165 bytes

    std::vector<sol::Instruction> body;
    if (create) body.push_back(sol::createTokenAccount(owner, dstAta, to, mint));
    body.push_back(sol::transferChecked(srcAta, mint, dstAta, owner, amount, 6));
    const uint64_t price = sol::priorityPrice({p.solAddress, ::sol::encode(srcAta), ::sol::encode(dstAta)});
    const sol::Key blockhash = sol::latestBlockhash();
    auto message = [&](uint32_t units) {
        std::vector<sol::Instruction> ixs = {sol::setComputeUnitLimit(units), sol::setComputeUnitPrice(price)};
        ixs.insert(ixs.end(), body.begin(), body.end());
        return sol::compileMessage(owner, ixs, blockhash);
    };
    // Measure the compute it really uses (and catch any rejection now), then
    // request that plus headroom: a lower limit keeps the priority fee small.
    const sol::Simulation sim = sol::simulate(sol::serialize(message(200000)), false);
    if (!sim.error.empty()) throw std::runtime_error("The network would reject this transfer: " + sol::reason(sim));
    const uint32_t units = static_cast<uint32_t>(std::min<uint64_t>(sim.units * 6 / 5 + 2000, 200000));
    const Bytes msg = message(units);
    const uint64_t cost = solFee(msg, price, units) + accountRent;
    if (cost > payer.lamports)
        throw std::runtime_error("Not enough SOL: this transfer needs about " + solStr(cost) + " SOL" +
                                 (create ? " (fee plus opening the recipient's USDT account)." : " for the fee."));
    checkSolLeftover(payer.lamports - cost, rentMin);

    Result r;
    r.amountStr = formatUnits(Big(amount), 6, 6);
    r.amountSym = "USDT";
    r.feeStr = solStr(cost);
    r.feeSym = "SOL";
    if (create)
        r.note = "Includes " + solStr(accountRent) + " SOL to open the recipient's USDT account: they haven't held "
                 "USDT on Solana before.";
    if (p.max) r.note = r.note.empty() ? "Sends your full USDT balance." : "Sends your full USDT balance. " + r.note;
    if (!p.solKeyHex.empty()) solBroadcast(p, msg, r);
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
    } else if (a == Asset::Trx || a == Asset::UsdtTrx) {
        if (::tron::isAddress(to)) return;
        if (to.rfind("0x", 0) == 0 || to.rfind("0X", 0) == 0)
            throw std::runtime_error("That's an Ethereum-style address. On Tron, send to an address starting with T.");
        if (::sol::isAddress(to)) throw std::runtime_error("That's a Solana address. On Tron, send to an address starting with T.");
        throw std::runtime_error(to[0] == 'T' && to.size() == 34 ? "That Tron address has a typo: its checksum doesn't match"
                                                                 : "A Tron address starts with T and has 34 characters");
    } else if (a == Asset::Sol || a == Asset::UsdtSol) {
        if (::tron::isAddress(to)) throw std::runtime_error("That's a Tron address. On Solana, send to a Solana address.");
        if (to.rfind("0x", 0) == 0 || to.rfind("0X", 0) == 0)
            throw std::runtime_error("That's an Ethereum-style address. On Solana, send to a Solana address.");
        if (!::sol::isAddress(to)) throw std::runtime_error("Not a Solana address (32-44 Base58 characters)");
    } else {
        // USDT exists on several networks; a Tron or Solana address here is the
        // classic way to lose funds, so name the mismatch instead of "bad hex".
        const bool usdt = a == Asset::UsdtEth;
        if (::tron::isAddress(to))
            throw std::runtime_error(usdt ? "That's a Tron address. To send USDT there, choose the Tron network."
                                          : "That's a Tron address, which can't receive this asset.");
        if (::sol::isAddress(to))
            throw std::runtime_error(usdt ? "That's a Solana address. To send USDT there, choose the Solana network."
                                          : "That's a Solana address, which can't receive this asset.");
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
        case Asset::Trx: return detail::runTrx(p);
        case Asset::UsdtTrx: return detail::runTrc20(p);
        case Asset::Sol: return detail::runSol(p);
        case Asset::UsdtSol: return detail::runSpl(p);
    }
    throw std::runtime_error("Unsupported asset");
}

}  // namespace rtxsend

#endif  // RTX_SEND_SEND_H
