#pragma once
#include <QHash>
#include <QMainWindow>
#include <QVector>

#include "walletmodel.h"

class AddressTile;
class AssetTile;
class Avatar;
class BalanceHero;
class Button;
class LockScreen;
class QGridLayout;
class QLabel;
class QScrollArea;
class QStackedWidget;
class TextField;
class WalletList;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(WalletModel* model, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QWidget* buildShell();
    QWidget* buildAside();
    QWidget* buildEmptyState();
    QWidget* buildWalletPage();

    bool unlocked() const;
    void onUnlocked(int action);
    void lockApp();
    void reloadList(const QString& select = {});
    void activate(const QString& name);
    void showWallet(const WalletView& w);
    void refresh();
    void onBalanceReady(quint64 gen, int chain, QString value);
    void onBalanceFailed(quint64 gen, int chain, QString error);
    void onPriceReady(quint64 gen, int chain, double usd, double change24h, double change7d);
    void applyPrices();  // push cached prices onto the tiles + hero
    void settleOne();
    void newWallet();
    void importWallet();
    void watchAddress();
    void updateListState();
    void relayoutGrids();
    bool showsAsset(Chain c) const;  // is this tile part of the current wallet's page?

    WalletModel* model_;
    QStackedWidget* root_ = nullptr;  // lock screen | shell
    LockScreen* lock_ = nullptr;
    QWidget* shell_ = nullptr;

    WalletList* list_ = nullptr;
    TextField* search_ = nullptr;
    QLabel* listCount_ = nullptr;
    QLabel* listEmpty_ = nullptr;

    QStackedWidget* detail_ = nullptr;  // empty state | wallet page
    QScrollArea* pageScroll_ = nullptr;
    QWidget* page_ = nullptr;
    Avatar* avatar_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* meta_ = nullptr;
    Button* sendBtn_ = nullptr;
    Button* refreshBtn_ = nullptr;
    Button* revealBtn_ = nullptr;
    Button* deleteBtn_ = nullptr;
    BalanceHero* hero_ = nullptr;
    QLabel* assetsMeta_ = nullptr;
    QGridLayout* assetGrid_ = nullptr;   // coins
    QWidget* stableBox_ = nullptr;       // "Stablecoins" caption + grid
    QGridLayout* stableGrid_ = nullptr;
    QVector<AssetTile*> tiles_;          // coins, then stablecoins
    QGridLayout* addrGrid_ = nullptr;
    QVector<AddressTile*> addrTiles_;    // EVM, BTC SegWit, Tron, Solana, BTC legacy

    WalletView current_;
    Chain primary_ = Chain::Eth;
    quint64 gen_ = 0;
    int pending_ = 0;
    struct Price { double usd = 0, change7d = 0; };
    QHash<int, Price> prices_;  // chain -> latest market price (persists across wallet switches)
};
