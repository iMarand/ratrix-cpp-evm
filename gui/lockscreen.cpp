#include "lockscreen.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include <stdexcept>

#include "dialogs.h"
#include "theme.h"
#include "walletmodel.h"
#include "widgets.h"

#ifndef RATRIX_VERSION
#define RATRIX_VERSION "dev"
#endif

namespace {

// Round lime arrow that sits inside a field, KryptVault-style.
Button* arrowButton() {
    auto* b = new Button("", Button::Primary);
    b->setIconId(Icon::ArrowRight);
    b->setFixedSize(40, 40);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip("Continue (Enter)");
    return b;
}

TextField* bigPassword(const QString& placeholder, Button* trailing) {
    auto* f = new TextField(placeholder);
    f->setPassword(false);
    f->setLarge(true);
    f->addTrailing(trailing);
    return f;
}

// "———— or ————"
QWidget* orDivider() {
    auto* w = new QWidget();
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    h->addWidget(ui::divider(), 1);
    h->addWidget(ui::label("or", 12, QFont::Normal, "faint"));
    h->addWidget(ui::divider(), 1);
    return w;
}

}  // namespace

LockScreen::LockScreen(WalletModel* m, QWidget* parent) : QWidget(parent), m_(m) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 22, 28, 24);
    outer->addStretch(2);

    column_ = new QWidget();
    column_->setFixedWidth(380);
    auto* lay = new QVBoxLayout(column_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* markRow = new QHBoxLayout();
    markRow->addStretch();
    markRow->addWidget(new LogoMark(96));
    markRow->addStretch();
    lay->addLayout(markRow);
    lay->addSpacing(22);

    title_ = ui::label("", 26, QFont::DemiBold);
    title_->setAlignment(Qt::AlignCenter);
    lay->addWidget(title_);
    lay->addSpacing(6);
    subtitle_ = ui::label("", 13, QFont::Normal, "faint");
    subtitle_->setAlignment(Qt::AlignCenter);
    subtitle_->setWordWrap(true);
    lay->addWidget(subtitle_);
    lay->addSpacing(32);

    go_ = arrowButton();
    pw_ = bigPassword("Passphrase", go_);
    lay->addWidget(pw_);

    setupBox_ = new QWidget();
    auto* sb = new QVBoxLayout(setupBox_);
    sb->setContentsMargins(0, 10, 0, 0);
    sb->setSpacing(12);
    go2_ = arrowButton();
    pw2_ = bigPassword("Confirm passphrase", go2_);
    sb->addWidget(pw2_);
    meter_ = new StrengthMeter();
    sb->addWidget(meter_);
    lay->addWidget(setupBox_);

    lay->addSpacing(8);
    error_ = ui::label("", 13, QFont::Normal, "danger");
    error_->setAlignment(Qt::AlignCenter);
    error_->setFixedHeight(24);
    lay->addWidget(error_);
    lay->addSpacing(10);

    lay->addWidget(orDivider());
    lay->addSpacing(18);
    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);
    create_ = new Button("Create wallet", Button::Secondary);
    create_->setIconId(Icon::Plus);
    import_ = new Button("Import wallet", Button::Secondary);
    import_->setIconId(Icon::Import);
    for (Button* b : {create_, import_}) {
        b->setFixedHeight(46);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        actions->addWidget(b, 1);
    }
    lay->addLayout(actions);
    lay->addSpacing(14);
    hint_ = ui::label("", 12, QFont::Normal, "faint");
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setWordWrap(true);
    lay->addWidget(hint_);

    auto* center = new QHBoxLayout();
    center->addStretch();
    center->addWidget(column_);
    center->addStretch();
    outer->addLayout(center);
    outer->addStretch(3);
    outer->addWidget(ui::label("Ratrix " RATRIX_VERSION, 12, QFont::Normal, "faint"), 0, Qt::AlignHCenter);

    auto next = [this] {
        if (firstRun_ && pw2_->text().isEmpty()) pw2_->edit()->setFocus();
        else submit(None);
    };
    connect(go_, &QPushButton::clicked, this, next);
    connect(pw_, &TextField::returnPressed, this, next);
    connect(go2_, &QPushButton::clicked, this, [this] { submit(None); });
    connect(pw2_, &TextField::returnPressed, this, [this] { submit(None); });
    connect(create_, &QPushButton::clicked, this, [this] { submit(Create); });
    connect(import_, &QPushButton::clicked, this, [this] { submit(Import); });
    connect(pw_, &TextField::textChanged, this, [this](const QString& t) {
        meter_->setPassword(t);
        pw_->setError(false);
        error_->clear();
    });
    connect(pw2_, &TextField::textChanged, this, [this] {
        pw2_->setError(false);
        error_->clear();
    });
}

Button* LockScreen::activeGo() const { return firstRun_ ? go2_ : go_; }

void LockScreen::reset() {
    firstRun_ = !m_->appInitialized();
    title_->setText("R-Wallet");
    subtitle_->setText(firstRun_ ? "Choose a passphrase to encrypt your wallets on this device."
                                 : "Enter your passphrase to unlock.");
    hint_->setText(firstRun_ ? "Set your passphrase above, then create or import your first wallet."
                             : "Forgot your passphrase? Leave it empty and choose create or import.");
    setupBox_->setVisible(firstRun_);
    go_->setVisible(!firstRun_);
    go_->setLoading(false);
    go2_->setLoading(false);
    pw_->setText({});
    pw2_->setText({});
    pw_->setError(false);
    pw2_->setError(false);
    error_->clear();
    QTimer::singleShot(0, pw_->edit(), [e = pw_->edit()] { e->setFocus(); });
}

void LockScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    ui::fadeIn(column_, 360);
}

void LockScreen::showError(const QString& msg) {
    error_->setText(msg);
    ui::shake(column_);
}

// Unlocks (or sets up) with the typed passphrase, then reports which flow to
// open. An empty field + create/import means "I forgot it": offer a reset.
void LockScreen::submit(int action) {
    Button* go = activeGo();
    if (go->isLoading()) return;
    const QString pass = pw_->text();
    if (pass.isEmpty()) {
        if (!firstRun_ && action != None) {
            if (dlg::resetPassphrase(this, m_)) emit unlocked(action);
            return;
        }
        pw_->setError(true);
        pw_->edit()->setFocus();
        return showError("Enter your passphrase.");
    }
    if (firstRun_ && pass != pw2_->text()) {
        pw2_->setError(true);
        return showError("Passphrases don't match.");
    }
    go->setLoading(true);
    error_->clear();
    // Let the spinner paint before the (briefly blocking) key derivation.
    QTimer::singleShot(60, this, [this, pass, go, action] {
        bool ok = false;
        QString err;
        try {
            if (firstRun_) {
                m_->setupApp(pass);
                ok = true;
            } else {
                ok = m_->unlockApp(pass);
            }
        } catch (const std::exception& e) {
            err = QString::fromUtf8(e.what());
        }
        go->setLoading(false);
        if (!ok) {
            pw_->setError(true);
            pw_->edit()->selectAll();
            pw_->edit()->setFocus();
            showError(err.isEmpty() ? "Wrong passphrase." : err);
            return;
        }
        pw_->setText({});
        pw2_->setText({});
        emit unlocked(action);
    });
}

void LockScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::pal().bg);
}
