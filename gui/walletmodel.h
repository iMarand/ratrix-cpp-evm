#pragma once
#include <QObject>
#include <QString>
#include <QVector>

#include <string>

// Bridge between the Qt UI and the Ratrix core + encrypted wallet store.
// This is the ONLY GUI translation unit that pulls in the (non-inline,
// header-only) core crypto/balance headers, so their symbols are defined once.

struct WalletView {
    QString name;
    QString type;       // HD | IMPORTED_PK | IMPORTED_SEED | WATCH
    QString eth;        // also the BNB / token address
    QString btcSegwit;
    QString btcLegacy;
    bool encrypted = false;
};

struct SecretView {
    QString privateKey;
    QString seedPhrase;
    QString wif;
};

enum class Chain { Eth, Bnb, Btc, UsdtEth, UsdcEth };

QString chainLabel(Chain c);

class WalletModel : public QObject {
    Q_OBJECT
public:
    explicit WalletModel(QObject* parent = nullptr);

    // --- app-level passphrase (set once, held for the session) ---
    bool appInitialized() const;              // has an app passphrase been set?
    void setupApp(const QString& passphrase); // first run: create + hold it
    bool unlockApp(const QString& passphrase); // later runs: verify + hold it; false if wrong
    bool isUnlocked() const { return unlocked_; }
    void lock();                              // forget the session passphrase

    // Forgotten passphrase: moves every encrypted wallet and the old app.lock
    // into a "locked-<timestamp>" backup folder (nothing is deleted), then sets
    // up `passphrase` and unlocks. Watch-only wallets stay. Returns the backup
    // folder, or "" if nothing needed moving.
    QString resetApp(const QString& passphrase);
    int encryptedCount() const;               // wallets that need the passphrase

    QVector<WalletView> list() const;
    bool exists(const QString& name) const;

    // A fresh random BIP39 mnemonic (CSPRNG-backed), not yet saved anywhere.
    // Used to preview / regenerate a phrase in the New-wallet dialog.
    // `words`: 12, 15, 18, 21 or 24.
    QString randomSeedPhrase(int words = 12) const;

    // Create / import. These use the session passphrase (app must be unlocked).
    // Throw std::exception on error (caller shows the message).
    WalletView createFromPhrase(const QString& name, const QString& phrase);
    WalletView importPrivateKey(const QString& name, const QString& pk);
    WalletView importSeed(const QString& name, const QString& seed);
    WalletView importEntropy(const QString& name, const QString& entropy);
    WalletView watch(const QString& name, const QString& address);
    void remove(const QString& name);

    SecretView unlock(const QString& name);  // decrypts with the session passphrase

    // Starts async balance lookups for every chain the wallet has an address
    // for. Results arrive on the GUI thread via the signals below, tagged with
    // `gen`; callers bump/track gen to drop stale results.
    void refreshBalances(const WalletView& w, quint64 gen);

    // Starts one async lookup of live USD prices for every chain, emitting
    // priceReady per chain on the GUI thread (tagged with `gen`).
    void refreshPrices(quint64 gen);

    // --- sending funds ---------------------------------------------------
    // `asset` matches the Chain enum values. Both run on a worker thread and
    // report back on the GUI thread, tagged with `gen`.
    //
    // reviewSend fetches fees/balances and reports the amount + fee for the
    // review step (no signing, no broadcast).
    // Validates a recipient address for `asset`; returns "" if acceptable,
    // otherwise a human-readable reason. Synchronous (no network).
    QString addressError(int asset, const QString& to) const;

    void reviewSend(quint64 gen, const WalletView& w, int asset, const QString& to, const QString& amount, bool max);
    // executeSend decrypts the key with the session passphrase, then builds,
    // signs and broadcasts. Throws here only if the wallet can't be unlocked;
    // network/validation problems come back via sendError.
    void executeSend(quint64 gen, const WalletView& w, int asset, const QString& to, const QString& amount, bool max);

signals:
    void balanceReady(quint64 gen, int chain, QString value);
    void balanceFailed(quint64 gen, int chain, QString error);
    void priceReady(quint64 gen, int chain, double usd, double change24h, double change7d);
    void priceFailed(quint64 gen);
    void sendReviewReady(quint64 gen, QString amount, QString amountSym, QString fee, QString feeSym, QString note);
    void sendReviewFailed(quint64 gen, QString error);
    void sendBroadcast(quint64 gen, QString txid, QString explorerUrl);
    void sendError(quint64 gen, QString error);

private:
    std::string sessionPass_;
    bool unlocked_ = false;
};
