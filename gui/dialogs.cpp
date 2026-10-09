#include "dialogs.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDesktopServices>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMainWindow>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>

#include "theme.h"
#include "walletmodel.h"
#include "walletview.h"
#include "widgets.h"

using theme::alpha;
using theme::mix;

namespace {


QString quoted(const QString& s) { return QStringLiteral("“") + s + QStringLiteral("”"); }

int wordCount(const QString& s) {
    static const QRegularExpression ws("\\s+");
    return int(s.split(ws, Qt::SkipEmptyParts).size());
}

// Hex digit count after an optional 0x prefix; -1 if any non-hex character.
int hexLength(QString v) {
    v = v.trimmed();
    if (v.startsWith("0x", Qt::CaseInsensitive)) v = v.mid(2);
    static const QRegularExpression re("^[0-9a-fA-F]*$");
    return re.match(v).hasMatch() ? int(v.size()) : -1;
}

// ---------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------

// Rounded note with an icon. Neutral by default; `tinted` washes it in `tone`.
class Callout : public QWidget {
public:
    Callout(Icon icon, const QColor& tone, const QString& text, bool tinted = false, QWidget* parent = nullptr)
        : QWidget(parent), icon_(icon), tone_(tone), tinted_(tinted) {
        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(44, 13, 16, 13);
        label_ = ui::label(text, 13, QFont::Normal, "muted");
        label_->setWordWrap(true);
        l->addWidget(label_);
    }
    QLabel* label() const { return label_; }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& P = theme::pal();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect());
        p.setPen(Qt::NoPen);
        p.setBrush(tinted_ ? alpha(tone_, 0.10) : P.surface);
        p.drawRoundedRect(r, 14, 14);
        icons::paint(p, icon_, QRectF(16, 14, 16, 16), tone_);
    }

private:
    Icon icon_;
    QColor tone_;
    bool tinted_;
    QLabel* label_;
};

// Small ghost button for in-body actions (never the dialog default).
Button* tool(const QString& text, Icon icon) {
    auto* b = new Button(text, Button::Ghost);
    b->setCompact(true);
    b->setIconId(icon);
    b->setAutoDefault(false);
    return b;
}

// Caption (+ optional trailing control) above a field.
QWidget* block(const QString& caption, QWidget* field, QWidget* trailing = nullptr, QLabel** captionOut = nullptr) {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    auto* cap = ui::caption(caption);
    head->addWidget(cap);
    head->addStretch();
    if (trailing) head->addWidget(trailing);
    v->addLayout(head);
    v->addWidget(field);
    if (captionOut) *captionOut = cap;
    return w;
}

// Numbered recovery-phrase chips with a staggered pop-in and a concealed mode.
class PhraseGrid : public QWidget {
public:
    explicit PhraseGrid(QWidget* parent = nullptr) : QWidget(parent), conceal_(this, 380), pop_(this, 700) {
        pop_.set(1);
    }
    bool clickToReveal = true;

    void setPhrase(const QString& phrase, bool animate = true) {
        static const QRegularExpression ws("\\s+");
        words_ = phrase.split(ws, Qt::SkipEmptyParts);
        const int rows = std::max<int>(1, int((words_.size() + cols() - 1) / cols()));
        setFixedHeight(rows * kRow + (rows - 1) * kGap);
        if (animate) {
            pop_.setDuration(int(420 + 24 * words_.size()));
            pop_.set(0);
            pop_.to(1);
        } else {
            pop_.set(1);
        }
        update();
    }
    QString phrase() const { return words_.join(' '); }

    void setConcealed(bool c, bool animate = true) {
        concealed_ = c;
        if (animate) conceal_.to(c ? 1 : 0);
        else conceal_.set(c ? 1 : 0);
        setCursor(c && clickToReveal ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

protected:
    void mousePressEvent(QMouseEvent*) override {
        if (concealed_ && clickToReveal) setConcealed(false);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& P = theme::pal();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int n = int(words_.size()), c = cols();
        const qreal cw = (width() - (c - 1) * kGap) / qreal(c);
        const qreal hide = std::clamp(conceal_.value(), 0.0, 1.0);
        const QFont numF = theme::mono(11);
        const QFont wordF = theme::mono(13, QFont::Medium);
        const QFontMetrics wfm(wordF);
        for (int i = 0; i < n; ++i) {
            const QRectF cr((i % c) * (cw + kGap), (i / c) * (kRow + kGap), cw, kRow);
            const qreal span = 0.45;
            const qreal start = n > 1 ? (1 - span) * i / (n - 1) : 0;
            const qreal lt = std::clamp((pop_.value() - start) / span, 0.0, 1.0);
            const qreal e = 1 - std::pow(1 - lt, 3);
            p.save();
            p.setOpacity(e);
            p.translate(cr.center());
            p.scale(0.96 + 0.04 * e, 0.96 + 0.04 * e);
            p.translate(-cr.center());
            p.translate(0, (1 - e) * 4);
            p.setPen(Qt::NoPen);
            p.setBrush(P.surface);
            p.drawRoundedRect(cr, 12, 12);
            const QRectF nr(cr.left() + 12, cr.top(), 20, cr.height());
            p.setFont(numF);
            p.setPen(P.faint);
            p.drawText(nr, Qt::AlignVCenter | Qt::AlignLeft, QString("%1").arg(i + 1, 2, 10, QChar('0')));
            const QRectF wr(nr.right() + 5, cr.top(), cr.right() - 10 - nr.right() - 5, cr.height());
            if (hide < 1) {
                p.setOpacity(e * (1 - hide));
                p.setFont(wordF);
                p.setPen(P.text);
                p.drawText(wr, Qt::AlignVCenter | Qt::AlignLeft,
                           wfm.elidedText(words_[i], Qt::ElideRight, int(wr.width())));
            }
            if (hide > 0) {
                p.setOpacity(e * hide);
                p.setPen(Qt::NoPen);
                p.setBrush(P.surface2);
                p.drawRoundedRect(QRectF(wr.left(), wr.center().y() - 3.5, wr.width() * 0.6, 7), 3.5, 3.5);
            }
            p.restore();
        }
        if (hide > 0.01 && n > 0) {
            p.setOpacity(hide);
            const QString t = clickToReveal ? "Click to reveal" : "Hold the button below to reveal";
            const QFont f = theme::font(12, QFont::Medium);
            const qreal pw = QFontMetricsF(f).horizontalAdvance(t) + 46, ph = 32;
            const QRectF pr(width() / 2.0 - pw / 2, height() / 2.0 - ph / 2, pw, ph);
            p.setPen(Qt::NoPen);
            p.setBrush(P.elevated);
            p.drawRoundedRect(pr, pr.height() / 2, pr.height() / 2);
            icons::paint(p, Icon::Eye, QRectF(pr.left() + 13, pr.center().y() - 7.5, 15, 15), P.muted);
            p.setFont(f);
            p.setPen(P.text);
            p.drawText(pr.adjusted(35, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, t);
        }
    }

private:
    static constexpr int kRow = 40, kGap = 8;
    int cols() const { return 3; }
    QStringList words_;
    bool concealed_ = false;
    Tween conceal_, pop_;
};

// A captioned, copyable value in a mono inset. Sensitive values can be
// concealed and auto-clear from the clipboard.
class SecretRow : public QWidget {
public:
    SecretRow(const QString& caption, const QString& value, bool sensitive, QWidget* parent = nullptr)
        : QWidget(parent), value_(value), sensitive_(sensitive) {
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(8);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        head->addWidget(ui::caption(caption));
        head->addStretch();
        copy_ = tool("Copy", Icon::Copy);
        QObject::connect(copy_, &QPushButton::clicked, this, [this] {
            ui::copy(this, value_, sensitive_);
            copy_->flash(Icon::Check);
        });
        head->addWidget(copy_);
        v->addLayout(head);
        auto* inset = new QFrame();
        inset->setObjectName("inset");
        auto* il = new QHBoxLayout(inset);
        il->setContentsMargins(16, 13, 16, 13);
        text_ = new QLabel();
        text_->setTextFormat(Qt::PlainText);
        text_->setFont(theme::mono(13));
        text_->setWordWrap(true);
        il->addWidget(text_);
        v->addWidget(inset);
        setConcealed(false);
    }

    void setConcealed(bool c) {
        text_->setText(c ? QString(std::min<qsizetype>(value_.size(), 44), QChar(0x2022)) : breakable(value_));
        ui::setTone(text_, c ? "faint" : nullptr);
        copy_->setEnabled(!c);
        if (!c) ui::fadeIn(text_, 320);
    }

    void setValue(const QString& v) {
        value_ = v;
        setConcealed(false);
    }

private:
    // Zero-width spaces let long hex strings wrap without visible change.
    static QString breakable(const QString& s) {
        QString out;
        for (qsizetype i = 0; i < s.size(); ++i) {
            if (i && i % 8 == 0) out += QChar(0x200B);
            out += s[i];
        }
        return out;
    }
    QString value_;
    bool sensitive_;
    QLabel* text_;
    Button* copy_;
};

// Coin mark that switches with the detected address type.
class CoinSlot : public QWidget {
public:
    explicit CoinSlot(int size, QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(size, size); }
    void set(std::optional<Chain> coin) {  // nullopt: nothing detected
        coin_ = coin;
        setVisible(coin_.has_value());
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!coin_) return;
        QPainter p(this);
        wv::paintCoin(p, *coin_, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }

private:
    std::optional<Chain> coin_;
};

// ---------------------------------------------------------------------------
// Sheet: a modal side panel that slides in from the right edge of the window
// ---------------------------------------------------------------------------

// Floating rounded panel. Swallows clicks so only clicks on the backdrop close.
class SheetPanel : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::pal().sidebar);
        p.drawRoundedRect(QRectF(rect()), 24, 24);
    }
    void mousePressEvent(QMouseEvent* e) override { e->accept(); }
};

class Sheet : public QWidget {
public:
    enum Tone { Neutral, Danger, Success };
    enum Result { Rejected = 0, Accepted = 1 };

    Sheet(QWidget* parent, const QString& title, const QString& subtitle, Icon icon, Tone tone = Neutral,
          int width = 440)
        : QWidget(ui::hostWindow(parent)), width_(width), t_(this, 280, QEasingCurve::OutCubic) {
        const auto& P = theme::pal();
        hide();
        success_ = tone == Success;

        panel_ = new SheetPanel(this);
        panel_->setFocusPolicy(Qt::StrongFocus);
        auto* v = new QVBoxLayout(panel_);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        // Header: optional status mark, title, subtitle, close.
        auto* header = new QWidget();
        auto* head = new QHBoxLayout(header);
        head->setContentsMargins(28, 26, 20, 10);
        head->setSpacing(14);
        tile_ = new IconTile(icon, tone == Danger ? P.danger : P.accent, 40);
        tile_->setSolid(tone == Success);
        tile_->setVisible(tone != Neutral);
        head->addWidget(tile_, 0, Qt::AlignTop);
        auto* titles = new QVBoxLayout();
        titles->setSpacing(6);
        titles->addWidget(ui::label(title, 22, QFont::Medium));
        if (!subtitle.isEmpty()) {
            auto* sl = ui::label(subtitle, 13, QFont::Normal, "faint");
            sl->setWordWrap(true);
            titles->addWidget(sl);
        }
        head->addLayout(titles, 1);
        auto* close = new Button("", Button::Square);
        close->setCompact(true);
        close->setIconId(Icon::Close);
        close->setFixedSize(36, 36);
        close->setToolTip("Close (Esc)");
        QObject::connect(close, &QPushButton::clicked, this, [this] { done(Rejected); });
        head->addWidget(close, 0, Qt::AlignTop);
        v->addWidget(header);

        auto* content = new QWidget();
        auto* cv = new QVBoxLayout(content);
        cv->setContentsMargins(28, 18, 28, 20);
        cv->setSpacing(0);
        body_ = new QVBoxLayout();
        body_->setSpacing(0);
        cv->addLayout(body_);
        errorRow_ = new QWidget();
        auto* el = new QVBoxLayout(errorRow_);
        el->setContentsMargins(0, 18, 0, 0);
        errorCallout_ = new Callout(Icon::Alert, P.danger, "", true);
        error_ = errorCallout_->label();
        ui::setTone(error_, "danger");
        el->addWidget(errorCallout_);
        errorRow_->hide();
        cv->addWidget(errorRow_);
        cv->addStretch(1);

        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->viewport()->setAutoFillBackground(false);
        content->setAutoFillBackground(false);
        scroll->setWidget(content);
        v->addWidget(scroll, 1);

        auto* footer = new QWidget();
        footer_ = new QHBoxLayout(footer);
        footer_->setContentsMargins(28, 14, 28, 26);
        footer_->setSpacing(10);
        footer_->addStretch();
        v->addWidget(footer);

        t_.onChange = [this](qreal) { place(); };
        parentWidget()->installEventFilter(this);
    }

    ~Sheet() override {
        if (central_) central_->setEnabled(true);
    }

    QVBoxLayout* body() const { return body_; }

    Button* addButton(const QString& text, Button::Variant v, bool primary) {
        auto* b = new Button(text, v);
        b->setFixedHeight(44);
        footer_->addWidget(b);
        if (primary) {
            primary_ = b;
            QObject::connect(b, &QPushButton::clicked, this, [this] {
                if (validate_) {
                    const QString err = validate_();
                    if (!err.isEmpty()) {
                        showError(err);
                        return;
                    }
                }
                done(Accepted);
            });
        } else {
            QObject::connect(b, &QPushButton::clicked, this, [this] { done(Rejected); });
        }
        return b;
    }

    void addFooterWidget(QWidget* w) { footer_->addWidget(w); }
    void close() { done(Rejected); }
    void onAccept(std::function<QString()> fn) { validate_ = std::move(fn); }
    void focusOn(QWidget* w) { focus_ = w; }

    void showError(const QString& msg) {
        error_->setText(msg);
        if (errorRow_->isHidden()) {
            errorRow_->show();
            ui::fadeIn(errorRow_, 200);
        }
        ui::shake(errorCallout_);
    }

    void clearError() {
        if (!errorRow_->isHidden()) errorRow_->hide();
    }

    // Shows the sheet and blocks (nested event loop) until it is closed.
    int exec() {
        QWidget* host = parentWidget();
        if (auto* mw = qobject_cast<QMainWindow*>(host)) central_ = mw->centralWidget();
        if (central_) central_->setEnabled(false);  // modal: nothing behind is reachable
        setGeometry(host->rect());
        t_.set(0);
        place();
        raise();
        show();
        if (focus_) focus_->setFocus(Qt::OtherFocusReason);
        else panel_->setFocus(Qt::OtherFocusReason);
        if (success_) QTimer::singleShot(160, tile_, [t = tile_] { t->pop(); });
        t_.to(1);

        QEventLoop loop;
        loop_ = &loop;
        loop.exec(QEventLoop::DialogExec);
        loop_ = nullptr;
        return result_;
    }

    void done(int r) {
        if (closing_ || !loop_) return;
        closing_ = true;
        result_ = r;
        t_.setDuration(200);
        t_.onFinished = [this] {
            if (t_.value() <= 0.001) finish();
        };
        t_.to(0);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), alpha(QColor(0, 0, 0), 0.62 * std::clamp(t_.value(), 0.0, 1.0)));
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (!panel_->geometry().contains(e->position().toPoint())) done(Rejected);
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) return done(Rejected);
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            auto* b = qobject_cast<QAbstractButton*>(QApplication::focusWidget());
            if (b && panel_->isAncestorOf(b)) b->click();
            else if (primary_ && primary_->isEnabled()) primary_->click();
            return;
        }
        QWidget::keyPressEvent(e);
    }

    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize) {
            setGeometry(parentWidget()->rect());
            place();
        }
        return QWidget::eventFilter(o, e);
    }

private:
    // Slides the panel in from beyond the right edge to a 12px inset.
    void place() {
        const qreal k = std::clamp(t_.value(), 0.0, 1.0);
        const int inset = 12;
        const int w = std::min(width_, width() - 2 * inset);
        panel_->setGeometry(width() - int(std::round((w + inset) * k)), inset, w, height() - 2 * inset);
        update();
    }

    void finish() {
        hide();
        if (central_) central_->setEnabled(true);
        if (loop_) loop_->quit();
    }

    int width_;
    Tween t_;
    SheetPanel* panel_;
    IconTile* tile_;
    QVBoxLayout* body_;
    QHBoxLayout* footer_;
    QWidget* errorRow_;
    Callout* errorCallout_;
    QLabel* error_;
    Button* primary_ = nullptr;
    QPointer<QWidget> focus_, central_;
    QEventLoop* loop_ = nullptr;
    int result_ = Rejected;
    bool closing_ = false, success_ = false;
    std::function<QString()> validate_;
};

// Confirmation shown after a wallet is created or imported.
void successSheet(QWidget* parent, const QString& title, const QString& subtitle, const WalletView& w,
                  const QString& phrase) {
    Sheet s(parent, title, subtitle, Icon::Check, Sheet::Success, 460);
    auto* b = s.body();
    if (!phrase.isEmpty()) {
        auto* grid = new PhraseGrid();
        grid->setPhrase(phrase, false);
        grid->setConcealed(true, false);
        auto* copy = tool("Copy", Icon::Copy);
        QObject::connect(copy, &QPushButton::clicked, &s, [&s, copy, phrase] {
            ui::copy(&s, phrase, true);
            copy->flash(Icon::Check);
        });
        b->addWidget(block("Recovery phrase", grid, copy));
        b->addSpacing(18);
    }
    const QString btc = w.btcSegwit.isEmpty() ? w.btcLegacy : w.btcSegwit;
    const std::pair<QString, QString> rows[] = {{"ETH / BNB / ERC-20 address", w.eth},
                                                {"Bitcoin address", btc},
                                                {"Tron address (TRX / TRC-20)", w.tron},
                                                {"Solana address", w.sol}};
    bool first = true;
    for (const auto& [label, address] : rows) {
        if (address.isEmpty()) continue;
        if (!first) b->addSpacing(14);
        b->addWidget(new SecretRow(label, address, false));
        first = false;
    }
    s.addButton("Done", Button::Primary, true);
    s.exec();
}

}  // namespace

namespace dlg {

QString createWallet(QWidget* parent, WalletModel* m) {
    const auto& P = theme::pal();
    Sheet s(parent, "New wallet", "A fresh recovery phrase, encrypted with your app passphrase.", Icon::Plus, Sheet::Neutral,
            460);
    auto* b = s.body();

    auto* name = new TextField("e.g. Main, Savings, Trading");
    b->addWidget(block("Wallet name", name));
    b->addSpacing(20);

    // Recovery phrase: random by default (CSPRNG-backed BIP39). "Use my own
    // words" switches to free text - any input derives a key, the project's
    // signature behavior.
    auto* words = new Segmented({"12 words", "24 words"});
    words->setFixedWidth(180);
    auto* grid = new PhraseGrid();
    grid->setPhrase(m->randomSeedPhrase(12));
    auto* area = new TextArea("Type your own words, separated by spaces");
    area->setMono(true);
    area->hide();
    auto* holder = new QWidget();
    auto* hl = new QVBoxLayout(holder);
    hl->setContentsMargins(0, 2, 0, 0);
    hl->addWidget(grid);
    hl->addWidget(area);
    b->addWidget(block("Recovery phrase", holder, words));
    b->addSpacing(10);

    auto* tools = new QHBoxLayout();
    tools->setSpacing(6);
    auto* regen = tool("Regenerate", Icon::Sparkle);
    auto* own = tool("Use my own words", Icon::Pencil);
    tools->addWidget(regen);
    tools->addWidget(own);
    tools->addStretch();
    auto* count = ui::label("", 12, QFont::Normal, "faint");
    tools->addWidget(count);
    b->addLayout(tools);
    b->addSpacing(16);
    b->addWidget(new Callout(Icon::Info, P.muted,
                             "Write these words down in order and keep them offline. "
                             "Anyone who has them controls this wallet."));

    bool custom = false;
    auto wanted = [words] { return words->current() == 0 ? 12 : 24; };
    auto updateCount = [&] {
        const int n = wordCount(custom ? area->text() : grid->phrase());
        count->setText(n == 1 ? QString("1 word") : QString("%1 words").arg(n));
    };
    QObject::connect(regen, &QPushButton::clicked, &s, [&] {
        grid->setPhrase(m->randomSeedPhrase(wanted()));
        updateCount();
    });
    QObject::connect(words, &Segmented::changed, &s, [&](int) {
        if (custom) return;
        grid->setPhrase(m->randomSeedPhrase(wanted()));
        updateCount();
    });
    QObject::connect(own, &QPushButton::clicked, &s, [&] {
        custom = !custom;
        if (custom) {
            area->setText(grid->phrase());
            grid->hide();
            area->show();
            ui::fadeIn(area, 220);
            area->edit()->setFocus();
            own->setText("Use generated phrase");
            own->setIconId(Icon::Sparkle);
        } else {
            grid->setPhrase(m->randomSeedPhrase(wanted()));
            area->hide();
            grid->show();
            own->setText("Use my own words");
            own->setIconId(Icon::Pencil);
        }
        regen->setEnabled(!custom);
        words->setEnabled(!custom);
        updateCount();
    });
    QObject::connect(area, &TextArea::textChanged, &s, [&] {
        updateCount();
        s.clearError();
    });
    QObject::connect(name, &TextField::textChanged, &s, [&] {
        name->setError(false);
        s.clearError();
    });
    updateCount();

    s.addButton("Cancel", Button::Secondary, false);
    s.addButton("Create wallet", Button::Primary, true)->setIconId(Icon::Plus);

    WalletView created;
    QString used;
    s.onAccept([&]() -> QString {
        const QString n = name->text().trimmed();
        if (n.isEmpty()) {
            name->setError(true);
            return "Give your wallet a name.";
        }
        if (m->exists(n)) {
            name->setError(true);
            return "A wallet with that name already exists.";
        }
        const QString phrase = (custom ? area->text() : grid->phrase()).trimmed();
        if (phrase.isEmpty()) return "The recovery phrase can't be empty.";
        try {
            created = m->createFromPhrase(n, phrase);
            used = phrase;
        } catch (const std::exception& e) {
            return QString::fromUtf8(e.what());
        }
        return {};
    });
    s.focusOn(name->edit());
    if (s.exec() != Sheet::Accepted) return {};

    successSheet(parent, "Wallet created",
                 quoted(created.name) + " is ready. Back up the recovery phrase before you send funds to it.",
                 created, used);
    return created.name;
}

QString importWallet(QWidget* parent, WalletModel* m) {
    Sheet s(parent, "Import wallet", "Restore access with a private key, recovery phrase or raw entropy.",
            Icon::Import, Sheet::Neutral, 460);
    auto* b = s.body();

    auto* name = new TextField("e.g. Old MetaMask, Hardware backup");
    b->addWidget(block("Wallet name", name));
    b->addSpacing(20);

    auto* kind = new Segmented({"Private key", "Recovery phrase", "Entropy"});
    b->addWidget(kind);
    b->addSpacing(16);

    auto* key = new TextField("64 hex characters");
    key->setMono(true);
    key->setLeadingIcon(Icon::Key);
    key->setPassword(true);
    auto* phrase = new TextArea("word1 word2 word3 …");
    phrase->setMono(true);
    phrase->hide();
    auto* ent = new TextField("32 to 64 hex characters");
    ent->setMono(true);
    ent->setLeadingIcon(Icon::Cube);
    ent->hide();
    auto* holder = new QWidget();
    auto* hl = new QVBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(key);
    hl->addWidget(phrase);
    hl->addWidget(ent);
    QLabel* cap = nullptr;
    b->addWidget(block("Private key", holder, nullptr, &cap));
    b->addSpacing(9);
    auto* hint = ui::label("", 12, QFont::Normal, "faint");
    hint->setContentsMargins(2, 0, 0, 0);
    b->addWidget(hint);

    auto setHint = [hint](const QString& t, const char* tone) {
        hint->setText(t);
        ui::setTone(hint, tone);
    };
    auto updateHint = [&] {
        switch (kind->current()) {
            case 0: {
                const int n = hexLength(key->text());
                if (key->text().trimmed().isEmpty()) setHint("Your key is encrypted before it touches the disk.", "faint");
                else if (n < 0) setHint("Only hex characters (0-9, a-f), with an optional 0x prefix.", "danger");
                else if (n == 64) setHint(QStringLiteral("✓  Looks like a valid private key"), "muted");
                else if (n > 64) setHint(QString("%1 hex characters · a private key has 64").arg(n), "danger");
                else setHint(QString("%1 / 64 hex characters").arg(n), "faint");
                break;
            }
            case 1: {
                const int n = wordCount(phrase->text());
                if (n == 0) setHint("Usually 12 or 24 words, separated by spaces.", "faint");
                else if (n == 12 || n == 15 || n == 18 || n == 21 || n == 24)
                    setHint(QString("✓  %1 words").arg(n), "muted");
                else setHint(QString("%1 words · not a standard BIP39 length").arg(n), "faint");
                break;
            }
            default: {
                const int n = hexLength(ent->text());
                if (ent->text().trimmed().isEmpty()) setHint("128 to 256 bits of entropy, as hex.", "faint");
                else if (n < 0) setHint("Only hex characters (0-9, a-f).", "danger");
                else if (n == 32 || n == 40 || n == 48 || n == 56 || n == 64)
                    setHint(QString("✓  %1 hex characters (%2 bits)").arg(n).arg(n * 4), "muted");
                else setHint(QString("%1 hex characters · needs 32, 40, 48, 56 or 64").arg(n), "faint");
                break;
            }
        }
    };
    QObject::connect(kind, &Segmented::changed, &s, [&](int i) {
        static const char* caps[] = {"Private key", "Recovery phrase", "Entropy (hex)"};
        cap->setText(caps[i]);
        QWidget* fields[] = {key, phrase, ent};
        for (int k = 0; k < 3; ++k) fields[k]->setVisible(k == i);
        ui::fadeIn(fields[i], 220);
        fields[i]->setFocus();
        s.clearError();
        updateHint();
    });
    QObject::connect(key, &TextField::textChanged, &s, [&] { s.clearError(); updateHint(); });
    QObject::connect(phrase, &TextArea::textChanged, &s, [&] { s.clearError(); updateHint(); });
    QObject::connect(ent, &TextField::textChanged, &s, [&] { s.clearError(); updateHint(); });
    QObject::connect(name, &TextField::textChanged, &s, [&] {
        name->setError(false);
        s.clearError();
    });
    updateHint();

    s.addButton("Cancel", Button::Secondary, false);
    s.addButton("Import wallet", Button::Primary, true)->setIconId(Icon::Import);

    WalletView imported;
    s.onAccept([&]() -> QString {
        const QString n = name->text().trimmed();
        if (n.isEmpty()) {
            name->setError(true);
            return "Give your wallet a name.";
        }
        if (m->exists(n)) {
            name->setError(true);
            return "A wallet with that name already exists.";
        }
        try {
            switch (kind->current()) {
                case 0: {
                    QString v = key->text().trimmed();
                    if (v.startsWith("0x", Qt::CaseInsensitive)) v = v.mid(2);
                    const int len = hexLength(v);
                    if (len <= 0 || len > 64) {
                        key->setError(true);
                        return "A private key is up to 64 hex characters (0-9, a-f).";
                    }
                    imported = m->importPrivateKey(n, v);
                    break;
                }
                case 1: {
                    const QString v = phrase->text().trimmed();
                    if (v.isEmpty()) return "Enter the recovery phrase.";
                    imported = m->importSeed(n, v);
                    break;
                }
                default: {
                    const QString v = ent->text().trimmed();
                    if (v.isEmpty()) return "Enter the entropy.";
                    imported = m->importEntropy(n, v);
                    break;
                }
            }
        } catch (const std::exception& e) {
            return QString::fromUtf8(e.what());
        }
        return {};
    });
    s.focusOn(name->edit());
    if (s.exec() != Sheet::Accepted) return {};

    successSheet(parent, "Wallet imported", quoted(imported.name) + " is ready to use.", imported, {});
    return imported.name;
}

QString watchAddress(QWidget* parent, WalletModel* m) {
    Sheet s(parent, "Watch address", "Follow any public address. No keys are stored, so it can't spend.",
            Icon::Eye, Sheet::Neutral, 460);
    auto* b = s.body();

    auto* name = new TextField("e.g. Cold storage, Treasury");
    b->addWidget(block("Label", name));
    b->addSpacing(20);

    auto* addr = new TextField(QStringLiteral("0x…  ·  bc1… / 1… / 3…  ·  T…  ·  Solana"));
    addr->setMono(true);
    addr->setLeadingIcon(Icon::Wallet);
    b->addWidget(block("Address", addr));
    b->addSpacing(10);

    auto* detectRow = new QHBoxLayout();
    detectRow->setContentsMargins(2, 0, 0, 0);
    detectRow->setSpacing(8);
    auto* coin = new CoinSlot(20);
    auto* hint = ui::label("", 12, QFont::Normal, "faint");
    detectRow->addWidget(coin);
    detectRow->addWidget(hint, 1);
    b->addLayout(detectRow);

    // Uses WalletModel::classify(), so the hint matches what watch() will track.
    auto detect = [m, addr, coin, hint] {
        const QString a = addr->text().trimmed();
        std::optional<Chain> c;
        QString t;
        const char* tone = "muted";
        switch (a.isEmpty() ? AddressKind::Unknown : m->classify(a)) {
            case AddressKind::Evm:
                c = Chain::Eth;
                t = QStringLiteral("EVM address · tracks ETH, BNB, USDT and USDC (ERC-20)");
                break;
            case AddressKind::BtcSegwit:
                c = Chain::Btc;
                t = "Bitcoin SegWit address";
                break;
            case AddressKind::BtcLegacy:
                c = Chain::Btc;
                t = "Bitcoin legacy address";
                break;
            case AddressKind::Tron:
                c = Chain::Trx;
                t = QStringLiteral("Tron address · tracks TRX and USDT (TRC-20)");
                break;
            case AddressKind::Solana:
                c = Chain::Sol;
                t = QStringLiteral("Solana address · tracks SOL");
                break;
            case AddressKind::Unknown:
                tone = "faint";
                if (a.isEmpty()) {
                    t = QStringLiteral("Paste an Ethereum (0x…), Bitcoin, Tron (T…) or Solana address.");
                } else if (a.startsWith("0x", Qt::CaseInsensitive) && a.size() != 42) {
                    c = Chain::Eth;
                    t = QString("Starts like an EVM address but has %1 of 42 characters").arg(a.size());
                } else if (a.size() < 26) {
                    t = "Not a complete address yet";
                } else {
                    t = "Unrecognized address format, or a typo in it";
                    tone = "danger";
                }
                break;
        }
        coin->set(c);
        hint->setText(t);
        ui::setTone(hint, tone);
    };
    QObject::connect(addr, &TextField::textChanged, &s, [&] {
        addr->setError(false);
        s.clearError();
        detect();
    });
    QObject::connect(name, &TextField::textChanged, &s, [&] {
        name->setError(false);
        s.clearError();
    });
    detect();

    s.addButton("Cancel", Button::Secondary, false);
    s.addButton("Watch address", Button::Primary, true)->setIconId(Icon::Eye);

    QString label;
    s.onAccept([&]() -> QString {
        label = name->text().trimmed();
        if (label.isEmpty()) {
            name->setError(true);
            return "Give this address a label.";
        }
        if (m->exists(label)) {
            name->setError(true);
            return "A wallet with that name already exists.";
        }
        try {
            m->watch(label, addr->text().trimmed());
        } catch (const std::exception& e) {
            addr->setError(true);
            return QString::fromUtf8(e.what());
        }
        return {};
    });
    s.focusOn(name->edit());
    return s.exec() == Sheet::Accepted ? label : QString();
}

bool resetPassphrase(QWidget* parent, WalletModel* m) {
    const auto& P = theme::pal();
    const int n = m->encryptedCount();
    Sheet s(parent, "Forgot your passphrase?",
            "Set a new one to keep creating and importing wallets. It can't open wallets sealed with the old one.",
            Icon::Key, Sheet::Neutral, 460);
    auto* b = s.body();
    const QString what =
        n == 0 ? QString("No encrypted wallets are stored, so nothing is moved. Watch-only wallets stay as they are.")
               : QString("Your %1 encrypted wallet%2 will be moved to a backup folder, not deleted. Watch-only wallets "
                         "stay. To use %3 again, import %4 from the recovery phrase or private key.")
                     .arg(n)
                     .arg(n == 1 ? "" : "s")
                     .arg(n == 1 ? "it" : "them")
                     .arg(n == 1 ? "it" : "each one");
    b->addWidget(new Callout(Icon::Info, P.muted, what));
    b->addSpacing(20);

    auto* pw = new TextField("New passphrase");
    pw->setPassword(true);
    b->addWidget(block("New passphrase", pw));
    b->addSpacing(10);
    auto* meter = new StrengthMeter();
    b->addWidget(meter);
    b->addSpacing(14);
    auto* pw2 = new TextField("Confirm passphrase");
    pw2->setPassword(true);
    b->addWidget(block("Confirm passphrase", pw2));

    QObject::connect(pw, &TextField::textChanged, &s, [&](const QString& t) {
        meter->setPassword(t);
        pw->setError(false);
        s.clearError();
    });
    QObject::connect(pw2, &TextField::textChanged, &s, [&] {
        pw2->setError(false);
        s.clearError();
    });

    s.addButton("Cancel", Button::Secondary, false);
    s.addButton("Set new passphrase", Button::Primary, true);

    QString backup;
    s.onAccept([&]() -> QString {
        if (pw->text().isEmpty()) {
            pw->setError(true);
            return "Choose a new passphrase.";
        }
        if (pw->text() != pw2->text()) {
            pw2->setError(true);
            return "Passphrases don't match.";
        }
        try {
            backup = m->resetApp(pw->text());
        } catch (const std::exception& e) {
            return QString::fromUtf8(e.what());
        }
        return {};
    });
    s.focusOn(pw->edit());
    if (s.exec() != Sheet::Accepted) return false;
    if (!backup.isEmpty())
        Toast::notify(parent, QString("New passphrase set · %1 wallet%2 moved to backup").arg(n).arg(n == 1 ? "" : "s"),
                      Icon::Shield);
    return true;
}

void revealSecret(QWidget* parent, WalletModel* m, const QString& walletName) {
    const auto& P = theme::pal();
    SecretView sec;
    try {
        sec = m->unlock(walletName);
    } catch (const std::exception& e) {
        Toast::notify(parent, QString::fromUtf8(e.what()), Icon::Alert, true);
        return;
    }

    Sheet s(parent, "Reveal secret keys",
            "For " + quoted(walletName) + ". Anyone with these can move your funds.", Icon::Key, Sheet::Neutral, 500);
    auto* b = s.body();
    b->addWidget(new Callout(Icon::Shield, P.muted,
                             "Never share these or type them into a website. "
                             "Make sure no one can see your screen."));
    b->addSpacing(20);

    PhraseGrid* grid = nullptr;
    Button* phraseCopy = nullptr;
    if (!sec.seedPhrase.isEmpty()) {
        grid = new PhraseGrid();
        grid->clickToReveal = false;
        grid->setPhrase(sec.seedPhrase, false);
        grid->setConcealed(true, false);
        phraseCopy = tool("Copy", Icon::Copy);
        phraseCopy->setEnabled(false);
        const QString seed = sec.seedPhrase;
        QObject::connect(phraseCopy, &QPushButton::clicked, &s, [&s, phraseCopy, seed] {
            ui::copy(&s, seed, true);
            phraseCopy->flash(Icon::Check);
        });
        b->addWidget(block("Recovery phrase", grid, phraseCopy));
        b->addSpacing(18);
    }
    auto* pk = new SecretRow("Private key · ETH, BNB, Tron", sec.privateKey, true);
    pk->setConcealed(true);
    b->addWidget(pk);
    SecretRow* wif = nullptr;
    if (!sec.wif.isEmpty()) {
        b->addSpacing(14);
        wif = new SecretRow("Bitcoin WIF", sec.wif, true);
        wif->setConcealed(true);
        b->addWidget(wif);
    }
    b->addSpacing(12);
    auto* where = ui::label(sec.seedPhrase.isEmpty()
                                ? QStringLiteral("The private key also opens the Tron address in TronLink.")
                                : QStringLiteral("The private key also opens the Tron address in TronLink. Solana is "
                                                 "restored from the recovery phrase (Phantom, Solflare)."),
                            12, QFont::Normal, "faint");
    where->setWordWrap(true);
    b->addWidget(where);

    s.addButton("Close", Button::Secondary, false);
    auto* hold = new HoldButton("Hold to reveal");
    hold->setIconId(Icon::Eye);
    hold->setAutoDefault(false);
    s.addFooterWidget(hold);
    QObject::connect(hold, &HoldButton::held, &s, [&] {
        if (grid) grid->setConcealed(false);
        if (phraseCopy) phraseCopy->setEnabled(true);
        pk->setConcealed(false);
        if (wif) wif->setConcealed(false);
        hold->setText("Revealed");
        hold->setIconId(Icon::Check);
        hold->setEnabled(false);
    });
    s.exec();
}

bool sendFunds(QWidget* parent, WalletModel* m, const WalletView& w) {
    const auto& P = theme::pal();
    // Assets this wallet can send, as Chain codes. USDT lives on several
    // networks (Ethereum, Tron, Solana), so it also gets a network choice.
    struct Option {
        QString sym;
        QVector<int> codes;  // one per network; USDT: {UsdtEth, UsdtTrx, UsdtSol}
    };
    QVector<Option> opts;
    const bool evm = !w.eth.isEmpty(), onTron = !w.tron.isEmpty(), onSol = !w.sol.isEmpty();
    if (evm) opts.push_back({"ETH", {int(Chain::Eth)}});
    if (!w.btcSegwit.isEmpty() || !w.btcLegacy.isEmpty()) opts.push_back({"BTC", {int(Chain::Btc)}});
    if (evm) opts.push_back({"BNB", {int(Chain::Bnb)}});
    if (onTron) opts.push_back({"TRX", {int(Chain::Trx)}});
    if (onSol) opts.push_back({"SOL", {int(Chain::Sol)}});
    Option usdt{"USDT", {}};
    if (evm) usdt.codes << int(Chain::UsdtEth);
    if (onTron) usdt.codes << int(Chain::UsdtTrx);
    if (onSol) usdt.codes << int(Chain::UsdtSol);
    if (!usdt.codes.isEmpty()) opts.push_back(usdt);
    if (evm) opts.push_back({"USDC", {int(Chain::UsdcEth)}});
    if (opts.isEmpty()) {
        Toast::notify(parent, "This wallet has no spendable address", Icon::Alert, true);
        return false;
    }
    QStringList syms;
    for (const auto& o : opts) syms << o.sym;

    Sheet s(parent, "Send", "Choose an asset, enter a recipient and amount, then review the fee.", Icon::Send,
            Sheet::Neutral, 480);
    auto* b = s.body();

    auto* picker = new Segmented(syms);
    b->addWidget(block("Asset", picker));
    b->addSpacing(20);

    // Only shown for an asset on more than one network (USDT); one segment per
    // network, in the order of usdt.codes.
    QStringList networks;
    for (int c : usdt.codes)
        networks << (c == int(Chain::UsdtTrx)   ? QStringLiteral("Tron · TRC-20")
                     : c == int(Chain::UsdtSol) ? QStringLiteral("Solana · SPL")
                                                : QStringLiteral("Ethereum · ERC-20"));
    auto* network = new Segmented(networks.isEmpty() ? QStringList{QString()} : networks);
    auto* networkRow = new QWidget();
    auto* nl = new QVBoxLayout(networkRow);
    nl->setContentsMargins(0, 0, 0, 20);
    nl->addWidget(block("Network", network));
    b->addWidget(networkRow);

    auto* to = new TextField();
    to->setMono(true);
    b->addWidget(block("Recipient address", to));
    b->addSpacing(9);
    auto* addrHint = ui::label("", 12, QFont::Normal, "faint");
    addrHint->setContentsMargins(2, 0, 0, 0);
    addrHint->setWordWrap(true);
    b->addWidget(addrHint);
    b->addSpacing(18);

    auto* amount = new TextField("0.0");
    auto* maxBtn = new Button("Max", Button::Ghost);
    maxBtn->setCompact(true);
    b->addWidget(block("Amount", amount, maxBtn));
    b->addSpacing(18);

    // Review panel (hidden until a review comes back).
    auto* review = new QFrame();
    review->setObjectName("inset");
    auto* rl = new QVBoxLayout(review);
    rl->setContentsMargins(16, 14, 16, 14);
    rl->setSpacing(10);
    auto rowOf = [&](const QString& cap, QLabel*& valueOut) {
        auto* row = new QHBoxLayout();
        row->addWidget(ui::label(cap, 13, QFont::Normal, "muted"));
        row->addStretch();
        valueOut = ui::label("", 13, QFont::Medium);
        row->addWidget(valueOut);
        rl->addLayout(row);
    };
    QLabel* rvAmount = nullptr;
    QLabel* rvFee = nullptr;
    rowOf("You send", rvAmount);
    rowOf("Network fee", rvFee);
    auto* rvNote = ui::label("", 12, QFont::Normal, "faint");
    rvNote->setWordWrap(true);
    rl->addWidget(rvNote);
    review->setVisible(false);
    b->addWidget(review);

    // Success panel (hidden until broadcast).
    auto* done = new QWidget();
    auto* dl = new QVBoxLayout(done);
    dl->setContentsMargins(0, 4, 0, 0);
    dl->setSpacing(12);
    auto* doneRow = new QHBoxLayout();
    doneRow->setSpacing(12);
    doneRow->addWidget(new IconTile(Icon::CheckCircle, P.accent, 40));
    auto* dt = new QVBoxLayout();
    dt->setSpacing(2);
    dt->addWidget(ui::label("Sent", 16, QFont::DemiBold));
    dt->addWidget(ui::label("Your transaction was broadcast to the network.", 12, QFont::Normal, "muted"));
    doneRow->addLayout(dt, 1);
    dl->addLayout(doneRow);
    auto* txidField = new SecretRow("Transaction ID", "", false);
    dl->addWidget(txidField);
    auto* explorerBtn = new Button("View in block explorer", Button::Secondary);
    explorerBtn->setIconId(Icon::Link);
    dl->addWidget(explorerBtn);
    done->setVisible(false);
    b->addWidget(done);

    // Footer.
    s.addButton("Cancel", Button::Secondary, false);
    auto* reviewBtn = new Button("Review", Button::Primary);
    reviewBtn->setIconId(Icon::ArrowRight);
    s.addFooterWidget(reviewBtn);
    auto* sendBtn = new HoldButton("Hold to send");
    sendBtn->setIconId(Icon::Send);
    sendBtn->setVisible(false);
    s.addFooterWidget(sendBtn);
    auto* doneBtn = new Button("Done", Button::Primary);
    doneBtn->setVisible(false);
    s.addFooterWidget(doneBtn);
    QObject::connect(doneBtn, &QPushButton::clicked, &s, [&s] { s.close(); });

    // --- state ---
    bool maxMode = false;
    bool sent = false;
    QString explorerUrl;
    quint64 gen = 0;

    auto multiNetwork = [&] { return opts[picker->current()].codes.size() > 1; };
    auto curAsset = [&] {
        const Option& o = opts[picker->current()];
        return o.codes.size() > 1 ? o.codes[std::min<int>(network->current(), int(o.codes.size()) - 1)] : o.codes[0];
    };
    auto placeholderFor = [](int a) {
        if (a == int(Chain::Btc)) return QStringLiteral("bc1…  or  1… / 3…");
        if (a == int(Chain::Trx) || a == int(Chain::UsdtTrx)) return QStringLiteral("T…");
        if (a == int(Chain::Sol) || a == int(Chain::UsdtSol)) return QStringLiteral("Solana wallet address");
        return QStringLiteral("0x…");
    };
    // A token sent on the wrong network is lost, so name the network up front.
    auto tokenStandard = [](int a) -> QString {
        if (a == int(Chain::UsdtEth) || a == int(Chain::UsdcEth)) return " (ERC-20)";
        if (a == int(Chain::UsdtTrx)) return " (TRC-20)";
        if (a == int(Chain::UsdtSol)) return " (SPL)";
        return {};
    };
    auto emptyHint = [&](int a) -> QString {
        const QString sym = syms[picker->current()];
        if (a == int(Chain::UsdtEth) || a == int(Chain::UsdcEth))
            return "Sends " + sym + " on Ethereum (ERC-20). The recipient must be an Ethereum address (0x…).";
        if (a == int(Chain::UsdtTrx))
            return QStringLiteral("Sends USDT on Tron (TRC-20). The recipient must be a Tron address (T…); "
                                  "the fee is paid in TRX.");
        if (a == int(Chain::UsdtSol))
            return QStringLiteral("Sends USDT on Solana (SPL). Enter the recipient's Solana wallet address; "
                                  "the fee is paid in SOL.");
        return QStringLiteral("The address funds will be sent to.");
    };
    auto validateAddr = [&] {
        const QString e = m->addressError(curAsset(), to->text());
        const QString sym = syms[picker->current()];
        if (to->text().trimmed().isEmpty()) {
            addrHint->setText(emptyHint(curAsset()));
            ui::setTone(addrHint, "faint");
            to->setError(false);
        } else if (e.isEmpty()) {
            addrHint->setText(QStringLiteral("✓  Valid ") + sym + tokenStandard(curAsset()) + " address");
            ui::setTone(addrHint, "muted");
            to->setError(false);
        } else {
            addrHint->setText(e);
            ui::setTone(addrHint, "danger");
        }
    };
    // Any input change invalidates a shown review.
    auto resetReview = [&] {
        if (review->isVisible()) {
            review->setVisible(false);
            sendBtn->setVisible(false);
            reviewBtn->setVisible(true);
        }
        s.clearError();
    };
    auto setMax = [&](bool on) {
        maxMode = on;
        amount->edit()->setReadOnly(on);
        amount->setText(on ? QString() : amount->text());
        amount->edit()->setPlaceholderText(on ? QStringLiteral("Maximum — all available") : "0.0");
        maxBtn->setVariant(on ? Button::Primary : Button::Ghost);
        resetReview();
    };

    auto assetChanged = [&] {
        networkRow->setVisible(multiNetwork());
        to->edit()->setPlaceholderText(placeholderFor(curAsset()));
        validateAddr();
        resetReview();
    };
    QObject::connect(picker, &Segmented::changed, &s, assetChanged);
    QObject::connect(network, &Segmented::changed, &s, assetChanged);
    QObject::connect(to, &TextField::textChanged, &s, [&] {
        // An address exists on only one of USDT's networks; follow it.
        if (multiNetwork()) {
            const AddressKind k = m->classify(to->text());
            const int code = k == AddressKind::Tron     ? int(Chain::UsdtTrx)
                             : k == AddressKind::Solana ? int(Chain::UsdtSol)
                             : k == AddressKind::Evm    ? int(Chain::UsdtEth)
                                                        : -1;
            const int idx = int(opts[picker->current()].codes.indexOf(code));
            if (idx >= 0) network->setCurrent(idx);
        }
        validateAddr();
        resetReview();
    });
    QObject::connect(amount, &TextField::textChanged, &s, [&] { resetReview(); });
    QObject::connect(maxBtn, &QPushButton::clicked, &s, [&] { setMax(!maxMode); });
    networkRow->setVisible(multiNetwork());
    to->edit()->setPlaceholderText(placeholderFor(curAsset()));
    validateAddr();

    // --- review ---
    QObject::connect(reviewBtn, &QPushButton::clicked, &s, [&] {
        const QString e = m->addressError(curAsset(), to->text());
        if (!e.isEmpty()) { to->setError(true); s.showError(e); return; }
        if (!maxMode && amount->text().trimmed().isEmpty()) { s.showError("Enter an amount to send."); return; }
        s.clearError();
        reviewBtn->setLoading(true);
        ++gen;
        m->reviewSend(gen, w, curAsset(), to->text(), amount->text(), maxMode);
    });
    QObject::connect(m, &WalletModel::sendReviewReady, &s,
                     [&](quint64 g, QString amt, QString amtSym, QString fee, QString feeSym, QString note) {
        if (g != gen) return;
        reviewBtn->setLoading(false);
        rvAmount->setText(amt + " " + amtSym);
        rvFee->setText(fee + " " + feeSym);
        rvNote->setText(note);
        rvNote->setVisible(!note.isEmpty());
        review->setVisible(true);
        ui::fadeIn(review, 200);
        reviewBtn->setVisible(false);
        sendBtn->setVisible(true);
    });
    QObject::connect(m, &WalletModel::sendReviewFailed, &s, [&](quint64 g, QString err) {
        if (g != gen) return;
        reviewBtn->setLoading(false);
        s.showError(err);
    });

    // --- send ---
    QObject::connect(sendBtn, &HoldButton::held, &s, [&] {
        sendBtn->setEnabled(false);
        sendBtn->setText("Sending…");
        sendBtn->setLoading(true);
        s.clearError();
        ++gen;
        try {
            m->executeSend(gen, w, curAsset(), to->text(), amount->text(), maxMode);
        } catch (const std::exception& ex) {
            sendBtn->reset();
            sendBtn->setText("Hold to send");
            s.showError(QString::fromUtf8(ex.what()));
        }
    });
    QObject::connect(m, &WalletModel::sendBroadcast, &s, [&](quint64 g, QString txid, QString url) {
        if (g != gen) return;
        sent = true;
        explorerUrl = url;
        // Swap the form for the success panel.
        picker->setEnabled(false);
        to->setEnabled(false);
        amount->setEnabled(false);
        maxBtn->setEnabled(false);
        review->setVisible(false);
        done->setVisible(true);
        txidField->setValue(txid);
        ui::fadeIn(done, 240);
        sendBtn->setVisible(false);
        doneBtn->setVisible(true);
        Toast::notify(parent, "Transaction sent", Icon::Send);
    });
    QObject::connect(m, &WalletModel::sendError, &s, [&](quint64 g, QString err) {
        if (g != gen) return;
        sendBtn->reset();
        sendBtn->setText("Hold to send");
        s.showError(err);
    });
    QObject::connect(explorerBtn, &QPushButton::clicked, &s, [&] {
        if (!explorerUrl.isEmpty()) QDesktopServices::openUrl(QUrl(explorerUrl));
    });

    s.focusOn(to->edit());
    s.exec();
    return sent;
}

bool confirmDelete(QWidget* parent, WalletModel* m, const WalletView& w) {
    Sheet s(parent, "Delete " + quoted(w.name) + "?",
            w.encrypted ? "This permanently removes its encrypted key from this device. Without a backup of the "
                          "recovery phrase or private key, any funds in it are lost."
                        : "This stops watching the address. No keys are involved.",
            Icon::Trash, Sheet::Danger);
    TextField* confirm = nullptr;
    if (w.encrypted) {
        confirm = new TextField(w.name);
        s.body()->addWidget(block("Type the wallet name to confirm", confirm));
    }
    s.addButton("Cancel", Button::Secondary, false);
    auto* del = s.addButton("Delete wallet", Button::Danger, true);
    del->setIconId(Icon::Trash);
    if (confirm) {
        del->setEnabled(false);
        QObject::connect(confirm, &TextField::textChanged, &s,
                         [del, n = w.name](const QString& t) { del->setEnabled(t.trimmed() == n); });
        s.focusOn(confirm->edit());
    }
    s.onAccept([&]() -> QString {
        try {
            m->remove(w.name);
        } catch (const std::exception& e) {
            return QString::fromUtf8(e.what());
        }
        return {};
    });
    return s.exec() == Sheet::Accepted;
}

}  // namespace dlg
