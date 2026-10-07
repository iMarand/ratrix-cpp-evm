#include "walletview.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "theme.h"

using theme::alpha;
using theme::mix;

namespace {

const QColor kWhite(255, 255, 255);

// Inserts thousands separators into the integer part of a decimal string.
QString grouped(const QString& num) {
    QString s = num;
    const bool neg = s.startsWith('-');
    if (neg) s.remove(0, 1);
    const qsizetype dot = s.indexOf('.');
    QString whole = dot < 0 ? s : s.left(dot);
    const QString frac = dot < 0 ? QString() : s.mid(dot);
    for (qsizetype i = whole.size() - 3; i > 0; i -= 3) whole.insert(i, ',');
    return (neg ? QStringLiteral("-") : QString()) + whole + frac;
}

QRectF lerpRect(const QRectF& a, const QRectF& b, qreal t) {
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t,
                  a.width() + (b.width() - a.width()) * t, a.height() + (b.height() - a.height()) * t);
}

// A rounded bar with a highlight band sweeping across it (loading skeleton).
void shimmerBar(QPainter& p, const QRectF& b, qreal phase) {
    const qreal x = b.left() - 80 + phase * (b.width() + 160);
    QLinearGradient g(QPointF(x - 70, 0), QPointF(x + 70, 0));
    g.setColorAt(0, alpha(kWhite, 0.05));
    g.setColorAt(0.5, alpha(kWhite, 0.10));
    g.setColorAt(1, alpha(kWhite, 0.05));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(b, std::min(8.0, b.height() / 2), std::min(8.0, b.height() / 2));
}

// A USD amount: "$3,421.50", or more decimals under $1 so cents aren't lost.
QString formatUsd(double v) {
    if (v <= 0) return "$0.00";
    const int dp = v >= 1 ? 2 : (v >= 0.01 ? 4 : 6);
    QString s = QString::number(v, 'f', dp);
    const qsizetype dot = s.indexOf('.');
    qsizetype start = dot < 0 ? s.size() : dot;
    for (qsizetype i = start - 3; i > 0; i -= 3) s.insert(i, ',');
    return "$" + s;
}

QString changeText(double pct) { return QString::number(std::abs(pct), 'f', std::abs(pct) >= 10 ? 1 : 2) + "%"; }

// Width of a change chip without drawing it (for right-alignment / layout).
qreal changeWidth(double pct, qreal px = 12) {
    const QString arrow = pct >= 0 ? QStringLiteral("▲") : QStringLiteral("▼");
    return QFontMetricsF(theme::font(px - 2, QFont::Bold)).horizontalAdvance(arrow) + 4 +
           QFontMetricsF(theme::font(px, QFont::Medium)).horizontalAdvance(changeText(pct));
}

// Draws "▲ 2.3%" (up, lime) or "▼ 1.1%" (down, red) left-aligned from `at`
// (vertical center); returns the drawn width. A finance up/down signal the
// user asked for, so this is the one place color encodes direction.
qreal paintChange(QPainter& p, const QPointF& at, double pct, qreal px = 12) {
    const auto& P = theme::pal();
    const QColor c = pct >= 0 ? P.accent : P.danger;
    const QString arrow = pct >= 0 ? QStringLiteral("▲") : QStringLiteral("▼");
    const QString text = changeText(pct);
    QFont af = theme::font(px - 2, QFont::Bold);
    QFont tf = theme::font(px, QFont::Medium);
    const qreal aw = QFontMetricsF(af).horizontalAdvance(arrow), tw = QFontMetricsF(tf).horizontalAdvance(text);
    p.setPen(c);
    p.setFont(af);
    p.drawText(QRectF(at.x(), at.y() - px, aw + 4, px * 2), Qt::AlignVCenter | Qt::AlignLeft, arrow);
    p.setFont(tf);
    p.drawText(QRectF(at.x() + aw + 4, at.y() - px, tw + 2, px * 2), Qt::AlignVCenter | Qt::AlignLeft, text);
    return aw + 4 + tw;
}

// Small status line: icon (or spinner) + text.
void statusLine(QPainter& p, const QRectF& r, Icon icon, const QColor& iconColor, const QString& text,
                const QColor& textColor, qreal spinAngle = -1) {
    const QRectF ir(r.left(), r.center().y() - 6.5, 13, 13);
    if (spinAngle >= 0) ui::paintSpinner(p, ir.adjusted(0.5, 0.5, -0.5, -0.5), spinAngle, iconColor, 1.5);
    else icons::paint(p, icon, ir, iconColor, 2.0);
    p.setPen(textColor);
    p.setFont(theme::font(12, QFont::Medium));
    p.drawText(QRectF(ir.right() + 6, r.top(), r.width() - 20, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
}

}  // namespace

// ===========================================================================
// wv::
// ===========================================================================

namespace wv {

QColor chainColor(Chain c) {
    const auto& P = theme::pal();
    switch (c) {
        case Chain::Eth: return P.eth;
        case Chain::Bnb: return P.bnb;
        case Chain::Btc: return P.btc;
        case Chain::UsdtEth: return P.usdt;
        case Chain::UsdcEth: return P.usdc;
    }
    return P.accent;
}

QString chainName(Chain c) {
    switch (c) {
        case Chain::Eth: return "Ethereum";
        case Chain::Bnb: return "BNB";
        case Chain::Btc: return "Bitcoin";
        case Chain::UsdtEth: return "Tether USD";
        case Chain::UsdcEth: return "USD Coin";
    }
    return "?";
}

QString chainNetwork(Chain c) {
    switch (c) {
        case Chain::Eth: return "Ethereum Mainnet";
        case Chain::Bnb: return "BNB Smart Chain";
        case Chain::Btc: return "Bitcoin Mainnet";
        case Chain::UsdtEth:
        case Chain::UsdcEth: return QStringLiteral("ERC-20 · Ethereum");
    }
    return {};
}

int maxDecimals(Chain c) { return c == Chain::Btc ? 8 : 6; }

QString typeLabel(const WalletView& w) {
    if (w.type == "HD") return "HD wallet";
    if (w.type == "IMPORTED_PK") return "Imported key";
    if (w.type == "IMPORTED_SEED") return "Imported phrase";
    if (w.type == "WATCH") return "Watch-only";
    return w.type;
}

QString primaryAddress(const WalletView& w) {
    if (!w.eth.isEmpty()) return w.eth;
    return w.btcSegwit.isEmpty() ? w.btcLegacy : w.btcSegwit;
}

void paintCoin(QPainter& p, Chain c, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(chainColor(c));
    p.drawEllipse(r);
    const QColor gc = kWhite;
    const qreal s = r.width() * 0.58 / 24.0;
    p.translate(r.center());
    p.scale(s, s);
    p.translate(-12, -12);
    switch (c) {
        case Chain::Eth:
            p.setBrush(gc);
            p.drawPolygon(QPolygonF{QPointF(12, 1.5), QPointF(19, 12.3), QPointF(12, 16.2), QPointF(5, 12.3)});
            p.setBrush(alpha(gc, 0.7));
            p.drawPolygon(QPolygonF{QPointF(12, 17.6), QPointF(19, 13.7), QPointF(12, 22.5), QPointF(5, 13.7)});
            break;
        case Chain::Bnb: {
            p.setBrush(gc);
            auto diamond = [&p](QPointF o, qreal h) {
                p.drawPolygon(QPolygonF{QPointF(o.x(), o.y() - h), QPointF(o.x() + h, o.y()), QPointF(o.x(), o.y() + h),
                                        QPointF(o.x() - h, o.y())});
            };
            diamond({12, 12}, 3.0);
            diamond({5.4, 12}, 1.9);
            diamond({18.6, 12}, 1.9);
            p.drawPolygon(QPolygonF{QPointF(12, 2.5), QPointF(17.6, 8.1), QPointF(15.7, 10), QPointF(12, 6.3),
                                    QPointF(8.3, 10), QPointF(6.4, 8.1)});
            p.drawPolygon(QPolygonF{QPointF(12, 21.5), QPointF(17.6, 15.9), QPointF(15.7, 14), QPointF(12, 17.7),
                                    QPointF(8.3, 14), QPointF(6.4, 15.9)});
            break;
        }
        case Chain::Btc:
            p.translate(12, 12);
            p.rotate(14);
            p.translate(-12, -12);
            p.setPen(QPen(gc, 2.0, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(10.2, 3.0), QPointF(10.2, 6.5));
            p.drawLine(QPointF(13.4, 3.0), QPointF(13.4, 6.5));
            p.drawLine(QPointF(10.2, 17.5), QPointF(10.2, 21.0));
            p.drawLine(QPointF(13.4, 17.5), QPointF(13.4, 21.0));
            p.setPen(gc);
            p.setFont(theme::font(17, QFont::Bold));
            p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, "B");
            break;
        case Chain::UsdtEth:
            p.setBrush(gc);
            p.drawRoundedRect(QRectF(5.5, 4.8, 13, 3.4), 0.8, 0.8);
            p.drawRoundedRect(QRectF(10.3, 4.8, 3.4, 15.4), 0.8, 0.8);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(gc, 1.6));
            p.drawEllipse(QPointF(12, 11.8), 7.4, 2.2);
            break;
        case Chain::UsdcEth:
            p.setPen(QPen(gc, 1.8, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(2.5, 2.5, 19, 19), 115 * 16, 130 * 16);
            p.drawArc(QRectF(2.5, 2.5, 19, 19), -65 * 16, 130 * 16);
            p.setPen(gc);
            p.setFont(theme::font(14, QFont::Bold));
            p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, "$");
            break;
    }
    p.restore();
}

bool isAmount(const QString& raw) {
    bool ok = false;
    raw.trimmed().toDouble(&ok);
    return ok;
}

QString prettyAmount(const QString& raw, int maxDp) {
    QString s = raw.trimmed();
    const bool neg = s.startsWith('-');
    if (neg) s.remove(0, 1);
    QString whole = s.section('.', 0, 0), frac = s.section('.', 1, 1);
    if (whole.isEmpty()) whole = "0";
    if (frac.size() > maxDp) frac.truncate(maxDp);  // never overstate a balance
    while (frac.size() > 2 && frac.endsWith('0')) frac.chop(1);
    while (frac.size() < 2) frac.append('0');
    return grouped((neg ? QStringLiteral("-") : QString()) + whole + "." + frac);
}

}  // namespace wv

// ===========================================================================
// CountUp / CoinBadge
// ===========================================================================

CountUp::CountUp(QWidget* owner) : owner_(owner) {
    anim_.setDuration(950);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    anim_.setStartValue(0.0);
    anim_.setEndValue(1.0);
    QObject::connect(&anim_, &QVariantAnimation::valueChanged, &anim_, [this](const QVariant& v) {
        t_ = v.toReal();
        owner_->update();
    });
}

void CountUp::start(const QString& raw, int maxDp) {
    const double shown = final_.isEmpty() ? 0.0 : from_ + (to_ - from_) * t_;
    final_ = wv::prettyAmount(raw, maxDp);
    to_ = raw.trimmed().toDouble();
    from_ = shown;
    dp_ = int(final_.section('.', 1).size());
    anim_.stop();
    if (std::abs(to_ - from_) < 1e-12) {
        t_ = 1;
        owner_->update();
        return;
    }
    t_ = 0;
    anim_.start();
}

void CountUp::clear() {
    anim_.stop();
    final_.clear();
    from_ = to_ = 0;
    t_ = 1;
}

QString CountUp::text() const {
    if (t_ >= 1.0 || final_.isEmpty()) return final_;
    return grouped(QString::number(from_ + (to_ - from_) * t_, 'f', dp_));
}

CoinBadge::CoinBadge(Chain c, int size, QWidget* parent) : QWidget(parent), c_(c) { setFixedSize(size, size); }

void CoinBadge::paintEvent(QPaintEvent*) {
    QPainter p(this);
    wv::paintCoin(p, c_, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

// ===========================================================================
// AssetTile
// ===========================================================================

AssetTile::AssetTile(Chain c, QWidget* parent)
    : QWidget(parent), c_(c), amount_(this), hover_(this, 160), reveal_(this, 420) {
    setMinimumSize(200, 172);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
    reveal_.set(1);
    shimmer_.setStartValue(0.0);
    shimmer_.setEndValue(1.0);
    shimmer_.setDuration(1300);
    shimmer_.setLoopCount(-1);
    connect(&shimmer_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        shimmerT_ = v.toReal();
        update();
    });
}

void AssetTile::setShimmer(bool on) {
    if (on && shimmer_.state() != QAbstractAnimation::Running) shimmer_.start();
    else if (!on) shimmer_.stop();
}

void AssetTile::reset() {
    st_ = State::Empty;
    refreshing_ = false;
    value_ = 0;
    amount_.clear();
    setShimmer(false);
    setToolTip({});
    update();
}

void AssetTile::setLoading() {
    if (st_ == State::Ready) refreshing_ = true;
    else st_ = State::Loading;
    setShimmer(true);
    update();
}

void AssetTile::setValue(const QString& raw) {
    st_ = State::Ready;
    refreshing_ = false;
    value_ = raw.trimmed().toDouble();
    setShimmer(false);
    setToolTip({});
    amount_.start(raw, wv::maxDecimals(c_));
    update();
}

void AssetTile::setFailed(const QString& why) {
    refreshing_ = false;
    setShimmer(false);
    if (st_ != State::Ready) st_ = State::Failed;  // keep a previously loaded value
    setToolTip(why);
    update();
}

void AssetTile::setPrice(double usd, double change7d) {
    price_ = usd;
    change7d_ = change7d;
    hasPrice_ = usd > 0;
    update();
}

void AssetTile::reveal(int delayMs) {
    reveal_.set(0);
    QTimer::singleShot(std::max(0, delayMs), this, [this] { reveal_.to(1); });
}

void AssetTile::enterEvent(QEnterEvent* e) {
    hover_.to(1);
    QWidget::enterEvent(e);
}

void AssetTile::leaveEvent(QEvent* e) {
    hover_.to(0);
    QWidget::leaveEvent(e);
}

void AssetTile::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal rv = std::clamp(reveal_.value(), 0.0, 1.0);
    p.setOpacity(rv);
    p.translate(0, (1 - rv) * 10);

    const QRectF r = QRectF(rect());
    p.setPen(Qt::NoPen);
    p.setBrush(mix(P.surface, P.surface2, 0.7 * hover_));
    p.drawRoundedRect(r, 20, 20);

    const qreal pad = 18;
    const QRectF coin(pad, pad, 38, 38);
    wv::paintCoin(p, c_, coin);
    const qreal tx = coin.right() + 12;
    // Top-right: live 7-day price change.
    const qreal chW = hasPrice_ ? changeWidth(change7d_, 12) : 0;
    const qreal nameW = r.width() - tx - pad - (hasPrice_ ? chW + 10 : 0);
    p.setPen(P.text);
    p.setFont(theme::font(14, QFont::Medium));
    p.drawText(QRectF(tx, coin.top(), nameW, 19), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(theme::font(14, QFont::Medium)).elidedText(wv::chainName(c_), Qt::ElideRight, int(nameW)));
    p.setPen(P.faint);
    p.setFont(theme::font(12));
    p.drawText(QRectF(tx, coin.top() + 20, nameW, 18), Qt::AlignLeft | Qt::AlignVCenter, wv::chainNetwork(c_));
    if (hasPrice_) paintChange(p, QPointF(r.width() - pad - chW, coin.center().y()), change7d_, 12);

    const qreal L = pad, W = r.width() - 2 * pad, H = r.height();
    const QRectF symR(L, H - pad - 78, W, 16);
    const QRectF amtR(L, H - pad - 60, W, 34);
    const QRectF stR(L, H - pad - 16, W, 16);
    p.setPen(P.text);
    p.setFont(theme::font(12, QFont::Medium));
    p.drawText(symR, Qt::AlignLeft | Qt::AlignVCenter, chainLabel(c_));

    // Bottom-left: live market price per coin (shown whenever we have it).
    const bool showPrice = hasPrice_ && st_ != State::Loading;
    auto bottom = [&](Icon icon, const QColor& ic, const QString& fallback, const QColor& fc, qreal spin = -1) {
        if (showPrice) {
            p.setPen(P.muted);
            p.setFont(theme::font(12, QFont::Medium));
            p.drawText(stR, Qt::AlignLeft | Qt::AlignVCenter, formatUsd(price_) + "  ·  7d");
        } else {
            statusLine(p, stR, icon, ic, fallback, fc, spin);
        }
    };

    const QFont amtF = theme::display(26, QFont::Medium);
    switch (st_) {
        case State::Loading:
            shimmerBar(p, QRectF(L, amtR.top() + 6, std::min(150.0, W), 22), shimmerT_);
            statusLine(p, stR, Icon::None, P.faint, "Updating", P.faint, shimmerT_ * 720);
            break;
        case State::Ready:
            p.setPen(refreshing_ ? P.muted : P.text);
            p.setFont(amtF);
            p.drawText(amtR, Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetrics(amtF).elidedText(amount_.text(), Qt::ElideRight, int(W)));
            if (refreshing_) statusLine(p, stR, Icon::None, P.faint, "Updating", P.faint, shimmerT_ * 720);
            else bottom(Icon::CheckCircle, P.faint, "Up to date", P.faint);
            break;
        case State::Failed:
            p.setPen(P.faint);
            p.setFont(amtF);
            p.drawText(amtR, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("—"));
            statusLine(p, stR, Icon::XCircle, P.danger, "Unavailable", P.danger);
            break;
        case State::Empty:
            p.setPen(P.faint);
            p.setFont(amtF);
            p.drawText(amtR, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("—"));
            bottom(Icon::Info, P.faint, "No address", P.faint);
            break;
    }
}

// ===========================================================================
// BalanceHero
// ===========================================================================

BalanceHero::BalanceHero(QWidget* parent) : QWidget(parent), amount_(this) {
    setFixedHeight(200);
    pulse_.setStartValue(0.0);
    pulse_.setEndValue(1.0);
    pulse_.setDuration(1300);
    pulse_.setLoopCount(-1);
    connect(&pulse_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        pulseT_ = v.toReal();
        update();
    });
    copy_ = new Button("", Button::Ghost, this);
    copy_->setCompact(true);
    copy_->setIconId(Icon::Copy);
    copy_->setFixedSize(30, 30);
    copy_->setToolTip("Copy address");
    connect(copy_, &QPushButton::clicked, this, [this] {
        if (address_.isEmpty()) return;
        ui::copy(this, address_);
        copy_->flash(Icon::Check);
    });
}

void BalanceHero::setWallet(const WalletView& w, Chain primary) {
    primary_ = primary;
    address_ = primary == Chain::Btc ? (w.btcSegwit.isEmpty() ? w.btcLegacy : w.btcSegwit) : w.eth;
    st_ = State::Empty;
    amount_.clear();
    status_.clear();
    value_ = 0;
    hasPrice_ = false;  // the primary chain may have changed; wait for its price
    busy_ = false;
    syncPulse();
    layoutChildren();
    update();
}

void BalanceHero::syncPulse() {
    const bool on = st_ == State::Loading || busy_;
    if (on && pulse_.state() != QAbstractAnimation::Running) pulse_.start();
    else if (!on) pulse_.stop();
}

void BalanceHero::setLoading() {
    if (st_ != State::Ready) st_ = State::Loading;
    syncPulse();
    update();
}

void BalanceHero::setValue(const QString& raw) {
    st_ = State::Ready;
    value_ = raw.trimmed().toDouble();
    amount_.start(raw, wv::maxDecimals(primary_));
    syncPulse();
    update();
}

void BalanceHero::setPrice(double usd, double change7d) {
    price_ = usd;
    change7d_ = change7d;
    hasPrice_ = usd > 0;
    update();
}

void BalanceHero::setFailed() {
    if (st_ != State::Ready) st_ = State::Failed;
    syncPulse();
    update();
}

void BalanceHero::setStatus(const QString& s, bool busy) {
    status_ = s;
    busy_ = busy;
    syncPulse();
    update();
}

QRectF BalanceHero::pillRect() const {
    QFontMetricsF fm(theme::mono(13));
    const qreal w = 8 + 26 + 10 + fm.horizontalAdvance(ui::shortAddress(address_, 10, 8)) + 8 + 36;
    return QRectF(0, height() - 42, w, 42);
}

void BalanceHero::layoutChildren() {
    const QRectF pr = pillRect();
    copy_->move(int(pr.right() - 36), int(pr.center().y() - 15));
    copy_->setVisible(!address_.isEmpty());
}

void BalanceHero::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    layoutChildren();
}

void BalanceHero::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal W = width();

    p.setPen(P.text);
    p.setFont(theme::font(15));
    p.drawText(QRectF(0, 0, W, 22), Qt::AlignVCenter | Qt::AlignLeft, wv::chainName(primary_) + " balance");

    if (!status_.isEmpty()) {
        const QFont sf = theme::font(13);
        const qreal sw = QFontMetricsF(sf).horizontalAdvance(status_);
        const QRectF ir(W - sw - 20, 4, 14, 14);
        if (busy_) ui::paintSpinner(p, ir.adjusted(0.5, 0.5, -0.5, -0.5), pulseT_ * 720, P.muted, 1.5);
        else icons::paint(p, Icon::Clock, ir, P.muted, 2.0);
        p.setPen(P.muted);
        p.setFont(sf);
        p.drawText(QRectF(W - sw, 0, sw, 22), Qt::AlignVCenter | Qt::AlignRight, status_);
    }

    const qreal baseY = 100;
    const QFont vf = theme::display(60, QFont::DemiBold);
    switch (st_) {
        case State::Ready: {
            const QString a = amount_.text();
            p.setFont(vf);
            p.setPen(P.text);
            p.drawText(QPointF(-3, baseY), a);
            const qreal aw = QFontMetricsF(vf).horizontalAdvance(a);
            p.setFont(theme::display(24, QFont::Medium));
            p.setPen(P.faint);
            p.drawText(QPointF(aw + 8, baseY), chainLabel(primary_));
            // Live USD value of this balance + the coin's 7-day change.
            if (hasPrice_) {
                const QString usd = QStringLiteral("≈ ") + formatUsd(value_ * price_);
                const QFont uf = theme::font(15, QFont::Medium);
                p.setFont(uf);
                p.setPen(P.muted);
                p.drawText(QPointF(0, baseY + 32), usd);
                const qreal uw = QFontMetricsF(uf).horizontalAdvance(usd);
                const qreal cx = uw + 14;
                const qreal cw = paintChange(p, QPointF(cx, baseY + 26), change7d_, 13);
                p.setPen(P.faint);
                p.setFont(theme::font(13));
                p.drawText(QPointF(cx + cw + 6, baseY + 32), QStringLiteral("7d"));
            }
            break;
        }
        case State::Loading:
            shimmerBar(p, QRectF(0, baseY - 50, 300, 50), pulseT_);
            break;
        case State::Failed:
            p.setFont(theme::display(26, QFont::Medium));
            p.setPen(P.muted);
            p.drawText(QPointF(0, baseY - 12), "Balance unavailable");
            break;
        case State::Empty:
            p.setFont(vf);
            p.setPen(P.faint);
            p.drawText(QPointF(-3, baseY), QStringLiteral("—"));
            break;
    }

    if (!address_.isEmpty()) {
        const QRectF pr = pillRect();
        p.setPen(Qt::NoPen);
        p.setBrush(P.surface);
        p.drawRoundedRect(pr, pr.height() / 2, pr.height() / 2);
        wv::paintCoin(p, primary_, QRectF(pr.left() + 8, pr.center().y() - 13, 26, 26));
        p.setPen(P.muted);
        p.setFont(theme::mono(13));
        p.drawText(QRectF(pr.left() + 44, pr.top(), pr.width() - 44 - 40, pr.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, ui::shortAddress(address_, 10, 8));
    }
}

// ===========================================================================
// AddressTile
// ===========================================================================

AddressTile::AddressTile(const QString& title, const QString& subtitle, Chain coin, QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(18, 16, 14, 18);
    v->setSpacing(14);

    auto* head = new QHBoxLayout();
    head->setSpacing(12);
    head->addWidget(new CoinBadge(coin, 34));
    auto* titles = new QVBoxLayout();
    titles->setSpacing(1);
    titles->addWidget(ui::label(title, 14, QFont::Medium));
    titles->addWidget(ui::label(subtitle, 12, QFont::Normal, "faint"));
    head->addLayout(titles, 1);
    copy_ = new Button("", Button::Square);
    copy_->setCompact(true);
    copy_->setIconId(Icon::Copy);
    copy_->setToolTip("Copy address");
    head->addWidget(copy_, 0, Qt::AlignTop);
    v->addLayout(head);

    addr_ = new ElideLabel();
    addr_->setFont(theme::mono(13));
    addr_->setColor(theme::pal().muted);
    v->addWidget(addr_);

    connect(copy_, &QPushButton::clicked, this, [this] {
        if (a_.isEmpty()) return;
        ui::copy(this, a_);
        copy_->flash(Icon::Check);
    });
}

void AddressTile::setAddress(const QString& a) {
    a_ = a;
    addr_->setText(a);
}

void AddressTile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::pal().surface);
    p.drawRoundedRect(QRectF(rect()), 20, 20);
}

// ===========================================================================
// WalletItem / WalletList
// ===========================================================================

WalletItem::WalletItem(const WalletView& w, QWidget* parent)
    : QWidget(parent), w_(w), hover_(this, 140), sel_(this, 220), reveal_(this, 360) {
    setFixedHeight(60);
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    reveal_.set(1);
}

void WalletItem::setSelected(bool s, bool animate) {
    if (animate) sel_.to(s ? 1 : 0);
    else sel_.set(s ? 1 : 0);
}

void WalletItem::reveal(int delayMs) {
    if (delayMs < 0) {
        reveal_.set(1);
        return;
    }
    reveal_.set(0);
    QTimer::singleShot(delayMs, this, [this] { reveal_.to(1); });
}

void WalletItem::enterEvent(QEnterEvent* e) {
    hover_.to(1);
    QWidget::enterEvent(e);
}

void WalletItem::leaveEvent(QEvent* e) {
    hover_.to(0);
    QWidget::leaveEvent(e);
}

void WalletItem::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClick) onClick();
    QWidget::mouseReleaseEvent(e);
}

void WalletItem::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal rv = std::clamp(reveal_.value(), 0.0, 1.0);
    p.setOpacity(rv);
    p.translate((1 - rv) * -10, 0);

    const QRectF r = QRectF(rect());
    const qreal h = hover_ * (1 - sel_);
    if (h > 0.01) {
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(P.surface, h));
        p.drawRoundedRect(r, 16, 16);
    }

    const qreal as = 38;
    const QRectF av(r.left() + 11, r.center().y() - as / 2, as, as);
    ui::paintAvatar(p, av, w_.name);
    if (sel_ > 0.01) {  // a lime ring marks the active wallet
        p.setPen(QPen(alpha(P.accent, sel_), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(av.adjusted(-2.5, -2.5, 2.5, 2.5));
    }

    const qreal ix = r.right() - 12 - 14;
    icons::paint(p, w_.encrypted ? Icon::Lock : Icon::Eye, QRectF(ix, r.center().y() - 7, 14, 14), P.faint);

    const qreal tx = av.right() + 13, tw = ix - 10 - tx;
    const QFont nf = theme::font(14, QFont::Medium);
    p.setFont(nf);
    p.setPen(mix(P.muted, P.text, std::max(sel_.value(), 0.75 + 0.25 * hover_)));
    p.drawText(QRectF(tx, r.top(), tw, r.height() / 2 + 1), Qt::AlignLeft | Qt::AlignBottom,
               QFontMetrics(nf).elidedText(w_.name, Qt::ElideRight, int(tw)));
    const QFont sf = theme::font(12);
    p.setFont(sf);
    p.setPen(P.faint);
    const QString sub = wv::typeLabel(w_) + QStringLiteral(" · ") + ui::shortAddress(wv::primaryAddress(w_), 6, 4);
    p.drawText(QRectF(tx, r.center().y() + 3, tw, r.height() / 2 - 3), Qt::AlignLeft | Qt::AlignTop,
               QFontMetrics(sf).elidedText(sub, Qt::ElideRight, int(tw)));
}

WalletList::WalletList(QWidget* parent) : QWidget(parent), pillT_(this, 300), pillAlpha_(this, 180) {
    lay_ = new QVBoxLayout(this);
    lay_->setContentsMargins(0, 0, 0, 0);
    lay_->setSpacing(4);
    lay_->addStretch(1);
    pillT_.set(1);
}

void WalletList::setWallets(const QVector<WalletView>& ws, const QString& select) {
    QSet<QString> known;
    for (auto* it : items_) {
        known.insert(it->wallet().name);
        lay_->removeWidget(it);
        it->hide();
        it->deleteLater();
    }
    const bool firstFill = items_.isEmpty();
    items_.clear();

    int stagger = 0;
    for (const auto& w : ws) {
        auto* it = new WalletItem(w, this);
        const QString name = w.name;
        it->onClick = [this, name] {
            this->select(name);
            emit activated(name);
        };
        lay_->insertWidget(lay_->count() - 1, it);
        items_.push_back(it);
        it->setSelected(name == select, false);
        if (firstFill || !known.contains(name)) it->reveal(40 + 35 * stagger++);
        else it->reveal(-1);
    }
    current_ = select;
    applyFilter();
    syncPill(false);
    QTimer::singleShot(0, this, [this] { syncPill(false); });
}

void WalletList::setFilter(const QString& f) {
    filter_ = f.trimmed();
    applyFilter();
    QTimer::singleShot(0, this, [this] { syncPill(true); });
}

void WalletList::applyFilter() {
    for (auto* it : items_) {
        const auto& w = it->wallet();
        const bool show = filter_.isEmpty() || w.name.contains(filter_, Qt::CaseInsensitive) ||
                          wv::primaryAddress(w).contains(filter_, Qt::CaseInsensitive);
        it->setVisible(show);
    }
}

int WalletList::visibleCount() const {
    int n = 0;
    for (auto* it : items_)
        if (!it->isHidden()) ++n;
    return n;
}

void WalletList::select(const QString& name) {
    current_ = name;
    for (auto* it : items_) it->setSelected(it->wallet().name == name, true);
    syncPill(true);
}

QRectF WalletList::pillNow() const { return lerpRect(from_, to_, std::clamp(pillT_.value(), 0.0, 1.0)); }

void WalletList::syncPill(bool animate) {
    lay_->activate();
    WalletItem* target = nullptr;
    for (auto* it : items_)
        if (it->wallet().name == current_ && !it->isHidden()) target = it;
    if (!target) {
        pillAlpha_.to(0);
        return;
    }
    const QRectF g = QRectF(target->geometry());
    if (!animate || pillAlpha_.target() == 0 || to_.isNull()) {
        from_ = to_ = g;
        pillT_.set(1);
        pillAlpha_.to(1);
        update();
        return;
    }
    if (g == to_) return;
    from_ = pillNow();
    to_ = g;
    pillT_.set(0);
    pillT_.to(1);
}

void WalletList::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    syncPill(false);
}

void WalletList::paintEvent(QPaintEvent*) {
    if (pillAlpha_ <= 0.01) return;
    const QRectF pr = pillNow();
    if (pr.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(pillAlpha_);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::pal().surface2);
    p.drawRoundedRect(pr, 16, 16);
}

// ===========================================================================
// EmptyArt
// ===========================================================================

EmptyArt::EmptyArt(QWidget* parent) : QWidget(parent) { setFixedSize(176, 176); }

void EmptyArt::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(rect()).center();
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 3; ++i) {
        p.setPen(QPen(alpha(P.borderStrong, 0.9 - i * 0.3), 1));
        p.drawEllipse(c, 46.0 + i * 19, 46.0 + i * 19);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(P.surface2);
    p.drawEllipse(c, 34, 34);
    icons::paint(p, Icon::Wallet, QRectF(c.x() - 13, c.y() - 13, 26, 26), P.accent);
}
