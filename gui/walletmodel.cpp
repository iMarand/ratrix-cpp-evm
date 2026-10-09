#include "walletmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

#include "../core/rtx.h"
#include "../core/address_kind.h"
#include "../core/balance/BtcBalance.h"
#include "../core/balance/SolBalance.h"
#include "../core/balance/TokenBalance.h"
#include "../core/balance/TronBalance.h"
#include "../core/price/prices.h"
#include "../core/send/send.h"
#include "../app/walletstore.h"

namespace store = rtxstore;

QString chainLabel(Chain c) {
    switch (c) {
        case Chain::Eth: return "ETH";
        case Chain::Bnb: return "BNB";
        case Chain::Btc: return "BTC";
        case Chain::UsdtEth: return "USDT";
        case Chain::UsdcEth: return "USDC";
        case Chain::Trx: return "TRX";
        case Chain::UsdtTrx: return "USDT";
        case Chain::Sol: return "SOL";
        case Chain::UsdtSol: return "USDT";
    }
    return "?";
}

static WalletView toView(const store::Wallet& w) {
    WalletView v;
    v.name = QString::fromStdString(w.name);
    v.type = QString::fromStdString(w.type);
    v.eth = QString::fromStdString(w.eth);
    v.btcSegwit = QString::fromStdString(w.btcSegwit);
    v.btcLegacy = QString::fromStdString(w.btcLegacy);
    v.tron = QString::fromStdString(w.tron);
    v.sol = QString::fromStdString(w.sol);
    v.encrypted = w.encrypted;
    // Wallets saved before Tron support: their Tron address is just the EVM
    // one re-encoded (same key). Not for watch-only, which tracks exactly the
    // address the user entered.
    if (v.tron.isEmpty() && w.encrypted && !w.eth.empty()) {
        try {
            v.tron = QString::fromStdString(tron::fromEvmAddress(w.eth));
        } catch (const std::exception&) {
        }
    }
    return v;
}

static store::Wallet fromPriv(const std::string& name, const std::string& type, const std::string& priv) {
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

// Same, for a wallet with a recovery phrase: also gets a Solana address.
static store::Wallet fromSeed(const std::string& name, const std::string& type, const std::string& seedHex,
                              std::string& privOut) {
    privOut = RTX::toPrivateKey(seedHex);
    store::Wallet w = fromPriv(name, type, privOut);
    w.sol = sol::addressFromSeed(seedHex);
    return w;
}

WalletModel::WalletModel(QObject* parent) : QObject(parent) {}

bool WalletModel::appInitialized() const { return store::appInitialized(); }

void WalletModel::setupApp(const QString& passphrase) {
    store::setupApp(passphrase.toStdString());
    sessionPass_ = passphrase.toStdString();
    unlocked_ = true;
}

bool WalletModel::unlockApp(const QString& passphrase) {
    if (!store::verifyApp(passphrase.toStdString())) return false;
    sessionPass_ = passphrase.toStdString();
    unlocked_ = true;
    addMissingAddresses();
    return true;
}

// Wallets created before Solana support have no Solana address. It comes from
// the recovery phrase, which is encrypted, so derive it once here while the
// passphrase is at hand and store it with the other public addresses. Runs
// once per wallet; afterwards the address is already there.
void WalletModel::addMissingAddresses() {
    for (const auto& name : store::list()) {
        try {
            const store::Wallet w = store::meta(store::loadRaw(name));
            if (!w.encrypted || !w.sol.empty() || w.type == "IMPORTED_PK") continue;
            store::Secret s = store::unlock(name, sessionPass_);
            if (!s.seedPhrase.empty()) store::setAddress(name, "sol", sol::addressFromSeed(RTX::toSeed(s.seedPhrase)));
            for (std::string* f : {&s.privateKey, &s.seedPhrase, &s.entropy}) f->assign(f->size(), '\0');
        } catch (const std::exception&) {
            // Unreadable, or sealed with another passphrase (e.g. made by the
            // CLI): leave it as it is.
        }
    }
}

int WalletModel::encryptedCount() const {
    int n = 0;
    for (const auto& name : store::list()) {
        try {
            n += store::meta(store::loadRaw(name)).encrypted ? 1 : 0;
        } catch (...) {
            ++n;  // unreadable: treat as encrypted
        }
    }
    return n;
}

QString WalletModel::resetApp(const QString& passphrase) {
    namespace sfs = std::filesystem;
    if (passphrase.isEmpty()) throw std::runtime_error("Passphrase cannot be empty");
    sfs::path backup;
    auto moveAside = [&backup](const sfs::path& from) {
        if (backup.empty()) {
            const std::string stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss").toStdString();
            backup = store::walletDir() / ("locked-" + stamp);
            sfs::create_directories(backup);
        }
        sfs::rename(from, backup / from.filename());
    };
    for (const auto& name : store::list()) {
        bool encrypted = true;
        try {
            encrypted = store::meta(store::loadRaw(name)).encrypted;
        } catch (...) {
        }
        if (encrypted) moveAside(store::walletPath(name));
    }
    if (sfs::exists(store::appLockPath())) moveAside(store::appLockPath());
    setupApp(passphrase);
    return backup.empty() ? QString() : QString::fromStdString(backup.string());
}

void WalletModel::lock() {
    if (!sessionPass_.empty()) sessionPass_.assign(sessionPass_.size(), '\0');
    sessionPass_.clear();
    unlocked_ = false;
}

QString WalletModel::randomSeedPhrase(int words) const {
    // BIP39: 12..24 words <-> 128..256 bits <-> 32..64 hex chars of entropy.
    const int nibbles = std::clamp(words, 12, 24) / 3 * 8;
    return QString::fromStdString(RTX::toSeedPhrase(RTX::randEntropy(nibbles)));
}

WalletView WalletModel::createFromPhrase(const QString& name, const QString& phrase) {
    if (!unlocked_) throw std::runtime_error("App is locked");
    std::string p = phrase.trimmed().toStdString();
    if (p.empty()) throw std::runtime_error("Recovery phrase cannot be empty");
    std::string priv;
    store::Wallet w = fromSeed(name.toStdString(), "HD", RTX::toSeed(p), priv);
    store::save(w, {priv, p, ""}, sessionPass_);
    return toView(w);
}

QVector<WalletView> WalletModel::list() const {
    QVector<WalletView> out;
    for (const auto& n : store::list()) out.push_back(toView(store::meta(store::loadRaw(n))));
    return out;
}

bool WalletModel::exists(const QString& name) const { return store::exists(name.toStdString()); }

WalletView WalletModel::importPrivateKey(const QString& name, const QString& pk) {
    if (!unlocked_) throw std::runtime_error("App is locked");
    std::string priv = pk.trimmed().toStdString();
    if (RTX::toAddress(priv).rfind("Error", 0) == 0) throw std::runtime_error("Invalid private key");
    store::Wallet w = fromPriv(name.toStdString(), "IMPORTED_PK", priv);
    store::save(w, {priv, "", ""}, sessionPass_);
    return toView(w);
}

WalletView WalletModel::importSeed(const QString& name, const QString& seed) {
    if (!unlocked_) throw std::runtime_error("App is locked");
    std::string phrase = seed.trimmed().toStdString();
    std::string priv;
    store::Wallet w = fromSeed(name.toStdString(), "IMPORTED_SEED", RTX::toSeed(phrase), priv);
    store::save(w, {priv, phrase, ""}, sessionPass_);
    return toView(w);
}

WalletView WalletModel::importEntropy(const QString& name, const QString& entropy) {
    if (!unlocked_) throw std::runtime_error("App is locked");
    std::string e = entropy.trimmed().toStdString();
    std::string phrase = RTX::toSeedPhrase(e);
    if (phrase.rfind("Error", 0) == 0) throw std::runtime_error("Invalid entropy (need 32/40/48/56/64 hex chars)");
    std::string priv;
    store::Wallet w = fromSeed(name.toStdString(), "IMPORTED_SEED", RTX::toSeed(phrase), priv);
    store::save(w, {priv, phrase, e}, sessionPass_);
    return toView(w);
}

WalletView WalletModel::watch(const QString& name, const QString& address) {
    store::Wallet w;
    w.name = name.toStdString();
    w.type = "WATCH";
    w.encrypted = false;
    const std::string a = address.trimmed().toStdString();
    switch (classify(address)) {
        case AddressKind::Evm: w.eth = a; break;
        case AddressKind::BtcSegwit: w.btcSegwit = a; break;
        case AddressKind::BtcLegacy: w.btcLegacy = a; break;
        case AddressKind::Tron: w.tron = a; break;
        case AddressKind::Solana: w.sol = a; break;
        case AddressKind::Unknown: throw std::runtime_error("Unrecognized address format");
    }
    store::save(w, {}, "");
    return toView(w);
}

AddressKind WalletModel::classify(const QString& address) const {
    switch (rtxaddr::classify(address.trimmed().toStdString())) {
        case rtxaddr::Kind::Evm: return AddressKind::Evm;
        case rtxaddr::Kind::BtcSegwit: return AddressKind::BtcSegwit;
        case rtxaddr::Kind::BtcLegacy: return AddressKind::BtcLegacy;
        case rtxaddr::Kind::Tron: return AddressKind::Tron;
        case rtxaddr::Kind::Solana: return AddressKind::Solana;
        case rtxaddr::Kind::Unknown: break;
    }
    return AddressKind::Unknown;
}

void WalletModel::remove(const QString& name) { store::remove(name.toStdString()); }

SecretView WalletModel::unlock(const QString& name) {
    if (!unlocked_) throw std::runtime_error("App is locked");
    store::Secret s = store::unlock(name.toStdString(), sessionPass_);
    SecretView v;
    v.privateKey = QString::fromStdString(s.privateKey);
    v.seedPhrase = QString::fromStdString(s.seedPhrase);
    if (!s.privateKey.empty()) {
        try { v.wif = QString::fromStdString(RTX::toBtcWIF(s.privateKey)); } catch (...) {}
    }
    return v;
}

void WalletModel::refreshBalances(const WalletView& w, quint64 gen) {
    // Marshal results onto the GUI thread via qApp (alive for the whole run) and
    // guard the model with a QPointer, so a balance thread that finishes after
    // the model is gone cannot emit into freed memory.
    QPointer<WalletModel> self(this);
    auto emitOk = [self, gen](Chain c, const std::string& val) {
        QMetaObject::invokeMethod(qApp, [self, gen, c, val] {
            if (self) emit self->balanceReady(gen, (int)c, QString::fromStdString(val));
        }, Qt::QueuedConnection);
    };
    auto emitErr = [self, gen](Chain c, const std::string& err) {
        QMetaObject::invokeMethod(qApp, [self, gen, c, err] {
            if (self) emit self->balanceFailed(gen, (int)c, QString::fromStdString(err));
        }, Qt::QueuedConnection);
    };

    auto run = [emitOk, emitErr](Chain c, std::function<std::string()> fn) {
        std::thread([emitOk, emitErr, c, fn] {
            try { emitOk(c, fn()); }
            catch (const std::exception& e) { emitErr(c, e.what()); }
            catch (...) { emitErr(c, "unknown error"); }
        }).detach();
    };

    const std::string eth = w.eth.toStdString();
    const std::string btc = (w.btcSegwit.isEmpty() ? w.btcLegacy : w.btcSegwit).toStdString();
    const std::string trx = w.tron.toStdString();
    const std::string solana = w.sol.toStdString();

    if (!eth.empty()) {
        run(Chain::Eth,     [eth] { return RTX_BALANCE::EthereumBalanceChecker().getBalance(eth); });
        run(Chain::Bnb,     [eth] { return RTX_BALANCE::BNBBalanceChecker().getBalance(eth); });
        run(Chain::UsdtEth, [eth] { return RTX_BALANCE::TokenBalanceChecker(RTX_BALANCE::tokens::ethUSDT()).getBalance(eth); });
        run(Chain::UsdcEth, [eth] { return RTX_BALANCE::TokenBalanceChecker(RTX_BALANCE::tokens::ethUSDC()).getBalance(eth); });
    }
    if (!btc.empty()) {
        run(Chain::Btc, [btc] { return RTX_BALANCE::BtcBalanceChecker().getBalance(btc); });
    }
    if (!trx.empty()) {
        run(Chain::Trx,     [trx] { return RTX_BALANCE::TronBalanceChecker().getBalance(trx); });
        run(Chain::UsdtTrx, [trx] {
            return RTX_BALANCE::TronBalanceChecker().getTokenBalance(RTX_BALANCE::tokens::tronUSDT(), trx);
        });
    }
    if (!solana.empty()) {
        run(Chain::Sol, [solana] { return RTX_BALANCE::SolBalanceChecker().getBalance(solana); });
        run(Chain::UsdtSol, [solana] {
            return RTX_BALANCE::SolBalanceChecker().getTokenBalance(sol::kUsdtMint, 6, solana);
        });
    }
}

void WalletModel::refreshPrices(quint64 gen) {
    QPointer<WalletModel> self(this);
    std::thread([self, gen] {
        std::map<std::string, rtxprice::Quote> quotes;
        bool ok = true;
        try {
            quotes = rtxprice::fetchAll();
        } catch (...) {
            ok = false;
        }
        QMetaObject::invokeMethod(qApp, [self, gen, quotes, ok] {
            if (!self) return;
            if (!ok) {
                emit self->priceFailed(gen);
                return;
            }
            const Chain chains[] = {Chain::Eth,     Chain::Bnb, Chain::Btc,     Chain::UsdtEth,
                                    Chain::UsdcEth, Chain::Trx, Chain::UsdtTrx, Chain::Sol, Chain::UsdtSol};
            for (Chain c : chains) {
                const auto it = quotes.find(chainLabel(c).toStdString());  // USDT on any network: one price
                if (it != quotes.end() && it->second.valid)
                    emit self->priceReady(gen, (int)c, it->second.usd, it->second.change24h, it->second.change7d);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

// --------------------------- sending funds -------------------------------

static rtxsend::Params sendParams(const WalletView& w, int asset, const QString& to, const QString& amount,
                                  bool max) {
    rtxsend::Params p;
    p.asset = static_cast<rtxsend::Asset>(asset);
    p.ethAddress = w.eth.toStdString();
    p.btcAddress = (w.btcSegwit.isEmpty() ? w.btcLegacy : w.btcSegwit).toStdString();
    p.tronAddress = w.tron.toStdString();
    p.solAddress = w.sol.toStdString();
    p.to = to.trimmed().toStdString();
    p.amount = amount.trimmed().toStdString();
    p.max = max;
    return p;
}

QString WalletModel::addressError(int asset, const QString& to) const {
    try {
        rtxsend::validateRecipient(static_cast<rtxsend::Asset>(asset), to.trimmed().toStdString());
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    }
    return {};
}

void WalletModel::reviewSend(quint64 gen, const WalletView& w, int asset, const QString& to, const QString& amount,
                             bool max) {
    const rtxsend::Params params = sendParams(w, asset, to, amount, max);  // no privHex: review only
    QPointer<WalletModel> self(this);
    std::thread([self, gen, params] {
        rtxsend::Result res;
        QString err;
        try {
            res = rtxsend::run(params);
        } catch (const std::exception& e) {
            err = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(qApp, [self, gen, res, err] {
            if (!self) return;
            if (!err.isEmpty()) {
                emit self->sendReviewFailed(gen, err);
                return;
            }
            emit self->sendReviewReady(gen, QString::fromStdString(res.amountStr),
                                       QString::fromStdString(res.amountSym), QString::fromStdString(res.feeStr),
                                       QString::fromStdString(res.feeSym), QString::fromStdString(res.note));
        }, Qt::QueuedConnection);
    }).detach();
}

void WalletModel::executeSend(quint64 gen, const WalletView& w, int asset, const QString& to, const QString& amount,
                              bool max) {
    if (!unlocked_) throw std::runtime_error("App is locked");
    // Decrypt the key on this (GUI) thread, then hand only the hex to the worker.
    store::Secret secret = store::unlock(w.name.toStdString(), sessionPass_);
    rtxsend::Params params = sendParams(w, asset, to, amount, max);
    const bool solana = asset == int(Chain::Sol) || asset == int(Chain::UsdtSol);
    if (solana && !secret.seedPhrase.empty()) {
        // Solana's ed25519 key comes from the recovery phrase, not the private key.
        std::array<uint8_t, 32> k = sol::accountKey(RTX::toSeed(secret.seedPhrase));
        params.solKeyHex = rtxsend::tron::hex(k.data(), k.size());
        OPENSSL_cleanse(k.data(), k.size());
    } else if (!solana) {
        params.privHex = secret.privateKey;
    }
    for (std::string* f : {&secret.privateKey, &secret.seedPhrase, &secret.entropy}) f->assign(f->size(), '\0');
    if (params.privHex.empty() && params.solKeyHex.empty())
        throw std::runtime_error(solana ? "This wallet has no Solana key: it was imported from a private key"
                                        : "This wallet has no spending key");

    QPointer<WalletModel> self(this);
    std::thread([self, gen, params]() mutable {
        rtxsend::Result res;
        QString err;
        try {
            res = rtxsend::run(params);
        } catch (const std::exception& e) {
            err = QString::fromUtf8(e.what());
        }
        params.privHex.assign(params.privHex.size(), '\0');  // wipe the worker's copies
        params.solKeyHex.assign(params.solKeyHex.size(), '\0');
        QMetaObject::invokeMethod(qApp, [self, gen, res, err] {
            if (!self) return;
            if (!err.isEmpty()) {
                emit self->sendError(gen, err);
                return;
            }
            emit self->sendBroadcast(gen, QString::fromStdString(res.txid), QString::fromStdString(res.explorerUrl));
        }, Qt::QueuedConnection);
    }).detach();
}
