#pragma once
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>

#include <functional>

#include "walletmodel.h"
#include "widgets.h"

class QVBoxLayout;

// Wallet-specific visuals: coin marks, the balance header, asset tiles,
// address tiles and the animated sidebar wallet list.

namespace wv {

QColor chainColor(Chain c);
QString chainName(Chain c);     // "Ethereum"
QString chainNetwork(Chain c);  // "Ethereum Mainnet", "TRC-20 · Tron"
Chain networkOf(Chain c);       // a token's network (UsdtTrx -> Trx); a coin is its own
bool isToken(Chain c);
int maxDecimals(Chain c);
QString typeLabel(const WalletView& w);  // "HD wallet", "Watch-only", ...
QString primaryAddress(const WalletView& w);
QString addressFor(const WalletView& w, Chain c);  // "" if the wallet has none for c

void paintCoin(QPainter& p, Chain c, const QRectF& r);
// paintCoin plus, for a token, a small badge of its network at the lower
// right, ringed in `bg` (the color behind the coin) so it reads as a cutout.
void paintAsset(QPainter& p, Chain c, const QRectF& r, const QColor& bg);

bool isAmount(const QString& raw);
QString prettyAmount(const QString& raw, int maxDp);  // "1,234.56"

}  // namespace wv

// Animates a displayed amount from its previous value to a new one.
class CountUp {
public:
    explicit CountUp(QWidget* owner);
    void start(const QString& raw, int maxDp);
    void clear();
    bool hasValue() const { return !final_.isEmpty(); }
    QString text() const;

private:
    QWidget* owner_;
    QVariantAnimation anim_;
    double from_ = 0, to_ = 0, t_ = 1;
    int dp_ = 2;
    QString final_;
};

class CoinBadge : public QWidget {
public:
    CoinBadge(Chain c, int size, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Chain c_;
};

// One asset as a rounded tile: coin + name on top, symbol / amount / status below.
class AssetTile : public QWidget {
public:
    explicit AssetTile(Chain c, QWidget* parent = nullptr);
    Chain chain() const { return c_; }
    void reset();
    void setLoading();
    void setValue(const QString& raw);
    void setFailed(const QString& why);
    void setPrice(double usd, double change7d);  // live market price + 7-day change
    void reveal(int delayMs);
    bool hasPositive() const { return st_ == State::Ready && value_ > 0; }
    QSize sizeHint() const override { return {240, 184}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    enum class State { Empty, Loading, Ready, Failed };
    void setShimmer(bool on);
    Chain c_;
    State st_ = State::Empty;
    bool refreshing_ = false;
    double value_ = 0;
    double price_ = 0, change7d_ = 0;
    bool hasPrice_ = false;
    CountUp amount_;
    Tween hover_, reveal_;
    QVariantAnimation shimmer_;
    qreal shimmerT_ = 0;
};

// The big primary balance at the top of a wallet page (sits on the canvas).
class BalanceHero : public QWidget {
public:
    explicit BalanceHero(QWidget* parent = nullptr);
    void setWallet(const WalletView& w, Chain primary);
    void setLoading();
    void setValue(const QString& raw);
    void setFailed();
    void setStatus(const QString& s, bool busy);
    void setPrice(double usd, double change7d);  // for the primary chain

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    enum class State { Empty, Loading, Ready, Failed };
    QRectF pillRect() const;
    void layoutChildren();
    void syncPulse();

    Chain primary_ = Chain::Eth;
    QString address_, status_;
    bool busy_ = false;
    State st_ = State::Empty;
    double value_ = 0, price_ = 0, change7d_ = 0;
    bool hasPrice_ = false;
    CountUp amount_;
    QVariantAnimation pulse_;  // drives shimmer + spinner while loading
    qreal pulseT_ = 0;
    Button* copy_;
};

// A receive address as a rounded tile with a copy button.
class AddressTile : public QWidget {
public:
    AddressTile(const QString& title, const QString& subtitle, Chain coin, QWidget* parent = nullptr);
    void setAddress(const QString& a);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    ElideLabel* addr_;
    Button* copy_;
    QString a_;
};

class WalletItem : public QWidget {
public:
    WalletItem(const WalletView& w, QWidget* parent);
    const WalletView& wallet() const { return w_; }
    void setSelected(bool s, bool animate);
    void reveal(int delayMs);  // < 0: show immediately
    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    WalletView w_;
    Tween hover_, sel_, reveal_;
};

// Sidebar wallet list with a selection highlight that glides between items.
class WalletList : public QWidget {
    Q_OBJECT
public:
    explicit WalletList(QWidget* parent = nullptr);
    void setWallets(const QVector<WalletView>& ws, const QString& select);
    void setFilter(const QString& f);
    void select(const QString& name);
    QString current() const { return current_; }
    int count() const { return int(items_.size()); }
    int visibleCount() const;

signals:
    void activated(const QString& name);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void applyFilter();
    void syncPill(bool animate);
    QRectF pillNow() const;

    QVBoxLayout* lay_;
    QVector<WalletItem*> items_;
    QString current_, filter_;
    QRectF from_, to_;
    Tween pillT_, pillAlpha_;
};

// Empty-state mark: a wallet glyph inside soft concentric rings.
class EmptyArt : public QWidget {
public:
    explicit EmptyArt(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
};
