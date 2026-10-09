#include "mainwindow.h"

#include <QBoxLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <stdexcept>

#include "dialogs.h"
#include "lockscreen.h"
#include "theme.h"
#include "walletview.h"
#include "widgets.h"

namespace {

constexpr int kAsideW = 288;
constexpr int kGutter = 12;  // space around the floating aside
const Chain kCoins[] = {Chain::Eth, Chain::Btc, Chain::Bnb, Chain::Sol, Chain::Trx};
// The same token on different networks gets its own tile: USDT on Ethereum and
// USDT on Tron are separate balances at separate addresses.
const Chain kStables[] = {Chain::UsdtEth, Chain::UsdtTrx, Chain::UsdtSol, Chain::UsdcEth};
const Chain kPrimaryOrder[] = {Chain::Eth, Chain::Btc, Chain::Trx, Chain::Sol};

QString quoted(const QString& s) { return QStringLiteral("“") + s + QStringLiteral("”"); }

// The floating, rounded aside panel.
class AsideFrame : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::pal().sidebar);
        p.drawRoundedRect(QRectF(rect()), 24, 24);
    }
};

class Canvas : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), theme::pal().bg);
    }
};

QScrollArea* scrollFor(QWidget* content) {
    auto* s = new QScrollArea();
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    s->setWidget(content);
    return s;
}

QHBoxLayout* sectionHeader(const QString& title, QLabel* trailing) {
    auto* h = new QHBoxLayout();
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(ui::label(title, 20, QFont::Medium));
    h->addStretch();
    if (trailing) h->addWidget(trailing, 0, Qt::AlignBottom);
    return h;
}

}  // namespace

MainWindow::MainWindow(WalletModel* model, QWidget* parent) : QMainWindow(parent), model_(model) {
    setWindowTitle("Ratrix Wallet");
    setMinimumSize(1000, 680);
    QSize want(1320, 860);
    if (QScreen* s = QGuiApplication::primaryScreen()) want = want.boundedTo(s->availableGeometry().size() * 0.92);
    resize(want);

    connect(model_, &WalletModel::balanceReady, this, &MainWindow::onBalanceReady);
    connect(model_, &WalletModel::balanceFailed, this, &MainWindow::onBalanceFailed);
    connect(model_, &WalletModel::priceReady, this, &MainWindow::onPriceReady);

    root_ = new QStackedWidget();
    lock_ = new LockScreen(model_);
    shell_ = buildShell();
    root_->addWidget(lock_);
    root_->addWidget(shell_);
    setCentralWidget(root_);
    connect(lock_, &LockScreen::unlocked, this, &MainWindow::onUnlocked);
    lock_->reset();

    auto shortcut = [this](const QKeySequence& k, std::function<void()> fn) {
        auto* sc = new QShortcut(k, this);
        connect(sc, &QShortcut::activated, this, [this, fn] {
            if (unlocked()) fn();
        });
    };
    shortcut(QKeySequence::New, [this] { newWallet(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_I), [this] { importWallet(); });
    shortcut(QKeySequence(Qt::Key_F5), [this] { refresh(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_R), [this] { refresh(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_L), [this] { lockApp(); });
    shortcut(QKeySequence::Find, [this] {
        search_->edit()->setFocus();
        search_->edit()->selectAll();
    });
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildShell() {
    auto* shell = new Canvas();
    shell->setFocusPolicy(Qt::ClickFocus);  // clicking empty space clears input focus
    auto* lay = new QHBoxLayout(shell);
    lay->setContentsMargins(kGutter, kGutter, 0, kGutter);
    lay->setSpacing(0);
    lay->addWidget(buildAside());
    detail_ = new QStackedWidget();
    detail_->addWidget(buildEmptyState());
    detail_->addWidget(buildWalletPage());
    lay->addWidget(detail_, 1);
    return shell;
}

QWidget* MainWindow::buildAside() {
    auto* side = new AsideFrame();
    side->setFixedWidth(kAsideW);
    auto* lay = new QVBoxLayout(side);
    lay->setContentsMargins(16, 20, 16, 16);
    lay->setSpacing(0);

    auto* brand = new QHBoxLayout();
    brand->setContentsMargins(4, 0, 0, 0);
    brand->setSpacing(12);
    brand->addWidget(new LogoMark(38));
    auto* bt = new QVBoxLayout();
    bt->setSpacing(0);
    bt->addWidget(ui::label("Ratrix", 16, QFont::DemiBold));
    bt->addWidget(ui::label("Multi-chain wallet", 12, QFont::Normal, "faint"));
    brand->addLayout(bt, 1);
    lay->addLayout(brand);
    lay->addSpacing(22);

    search_ = new TextField("Search wallets");
    search_->setLeadingIcon(Icon::Search);
    connect(search_, &TextField::textChanged, this, [this](const QString& t) {
        list_->setFilter(t);
        updateListState();
    });
    lay->addWidget(search_);
    lay->addSpacing(22);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(6, 0, 6, 0);
    head->addWidget(ui::caption("Wallets"));
    head->addStretch();
    listCount_ = ui::label("0", 12, QFont::Medium, "faint");
    head->addWidget(listCount_);
    lay->addLayout(head);
    lay->addSpacing(8);

    listEmpty_ = ui::label("", 13, QFont::Normal, "faint");
    listEmpty_->setAlignment(Qt::AlignCenter);
    listEmpty_->setWordWrap(true);
    listEmpty_->setContentsMargins(8, 24, 8, 24);
    lay->addWidget(listEmpty_);

    list_ = new WalletList();
    connect(list_, &WalletList::activated, this, &MainWindow::activate);
    lay->addWidget(scrollFor(list_), 1);
    lay->addSpacing(14);

    // The action dock: create, import, watch, lock.
    auto* dock = new Dock();
    connect(dock->add(Icon::Plus, "New wallet  (Ctrl+N)", true), &QPushButton::clicked, this, &MainWindow::newWallet);
    connect(dock->add(Icon::Import, "Import wallet  (Ctrl+I)"), &QPushButton::clicked, this, &MainWindow::importWallet);
    connect(dock->add(Icon::Eye, "Watch an address"), &QPushButton::clicked, this, &MainWindow::watchAddress);
    connect(dock->add(Icon::Lock, "Lock  (Ctrl+L)"), &QPushButton::clicked, this, &MainWindow::lockApp);
    lay->addWidget(dock, 0, Qt::AlignHCenter);
    return side;
}

QWidget* MainWindow::buildEmptyState() {
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(40, 40, 40, 40);
    v->setSpacing(0);
    v->addStretch(3);

    auto* artRow = new QHBoxLayout();
    artRow->addStretch();
    artRow->addWidget(new EmptyArt());
    artRow->addStretch();
    v->addLayout(artRow);
    v->addSpacing(18);

    auto* t = ui::label("No wallets yet", 26, QFont::Medium);
    t->setAlignment(Qt::AlignCenter);
    v->addWidget(t);
    v->addSpacing(8);

    auto* s = ui::label("Create a wallet, import one from a key or recovery phrase, or watch any public address.",
                        14, QFont::Normal, "faint");
    s->setAlignment(Qt::AlignCenter);
    s->setWordWrap(true);
    s->setFixedWidth(420);
    auto* sRow = new QHBoxLayout();
    sRow->addStretch();
    sRow->addWidget(s);
    sRow->addStretch();
    v->addLayout(sRow);
    v->addSpacing(28);

    auto* btns = new QHBoxLayout();
    btns->setSpacing(10);
    btns->addStretch();
    auto* create = new Button("Create wallet", Button::Primary);
    create->setIconId(Icon::Plus);
    auto* imp = new Button("Import", Button::Secondary);
    imp->setIconId(Icon::Import);
    auto* watch = new Button("Watch address", Button::Secondary);
    watch->setIconId(Icon::Eye);
    for (Button* b : {create, imp, watch}) {
        b->setFixedHeight(44);
        btns->addWidget(b);
    }
    connect(create, &QPushButton::clicked, this, &MainWindow::newWallet);
    connect(imp, &QPushButton::clicked, this, &MainWindow::importWallet);
    connect(watch, &QPushButton::clicked, this, &MainWindow::watchAddress);
    btns->addStretch();
    v->addLayout(btns);
    v->addStretch(4);
    return w;
}

QWidget* MainWindow::buildWalletPage() {
    page_ = new QWidget();
    pageScroll_ = scrollFor(page_);
    auto* outer = new QVBoxLayout(page_);
    outer->setContentsMargins(40, 26, 40, 40);
    outer->setSpacing(0);

    // Header: identity + square icon actions.
    auto* header = new QHBoxLayout();
    header->setSpacing(8);
    avatar_ = new Avatar(44);
    header->addWidget(avatar_);
    header->addSpacing(4);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(1);
    name_ = ui::label("", 16, QFont::Medium);
    meta_ = ui::label("", 12, QFont::Normal, "faint");
    titles->addWidget(name_);
    titles->addWidget(meta_);
    header->addLayout(titles, 1);

    sendBtn_ = new Button("Send", Button::Primary);
    sendBtn_->setIconId(Icon::Send);
    sendBtn_->setFixedHeight(44);
    sendBtn_->setToolTip("Send funds to another address");
    header->addWidget(sendBtn_);
    header->addSpacing(2);
    refreshBtn_ = new Button("", Button::Square);
    refreshBtn_->setIconId(Icon::Refresh);
    refreshBtn_->setToolTip("Refresh balances  (F5)");
    revealBtn_ = new Button("", Button::Square);
    revealBtn_->setIconId(Icon::Key);
    revealBtn_->setToolTip("Reveal secret keys");
    deleteBtn_ = new Button("", Button::Danger);
    deleteBtn_->setIconId(Icon::Trash);
    deleteBtn_->setToolTip("Delete wallet");
    for (Button* b : {refreshBtn_, revealBtn_, deleteBtn_}) {
        b->setFixedSize(44, 44);
        header->addWidget(b);
    }
    connect(sendBtn_, &QPushButton::clicked, this, [this] {
        if (current_.name.isEmpty() || !current_.encrypted) return;
        if (dlg::sendFunds(this, model_, current_)) QTimer::singleShot(1500, this, &MainWindow::refresh);
    });
    connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(revealBtn_, &QPushButton::clicked, this, [this] {
        if (!current_.name.isEmpty()) dlg::revealSecret(this, model_, current_.name);
    });
    connect(deleteBtn_, &QPushButton::clicked, this, [this] {
        if (current_.name.isEmpty()) return;
        const QString n = current_.name;
        if (!dlg::confirmDelete(this, model_, current_)) return;
        current_ = WalletView{};
        reloadList();
        Toast::notify(this, "Deleted " + quoted(n), Icon::Trash);
    });
    outer->addLayout(header);
    outer->addSpacing(36);

    hero_ = new BalanceHero();
    outer->addWidget(hero_);
    outer->addSpacing(44);

    assetsMeta_ = ui::label("", 13, QFont::Normal, "faint");
    outer->addLayout(sectionHeader("Assets", assetsMeta_));
    outer->addSpacing(16);
    assetGrid_ = new QGridLayout();
    assetGrid_->setSpacing(12);
    for (Chain c : kCoins) tiles_.push_back(new AssetTile(c));
    outer->addLayout(assetGrid_);

    stableBox_ = new QWidget();
    auto* sv = new QVBoxLayout(stableBox_);
    sv->setContentsMargins(0, 24, 0, 0);
    sv->setSpacing(12);
    sv->addWidget(ui::caption("Stablecoins"));
    stableGrid_ = new QGridLayout();
    stableGrid_->setSpacing(12);
    for (Chain c : kStables) tiles_.push_back(new AssetTile(c));
    sv->addLayout(stableGrid_);
    outer->addWidget(stableBox_);
    outer->addSpacing(44);

    outer->addLayout(sectionHeader("Receive", ui::label("Public addresses, safe to share", 13, QFont::Normal, "faint")));
    outer->addSpacing(16);
    addrGrid_ = new QGridLayout();
    addrGrid_->setSpacing(12);
    addrTiles_ = {new AddressTile("EVM address", QStringLiteral("ETH · BNB · ERC-20 tokens"), Chain::Eth),
                  new AddressTile("Bitcoin", "Native SegWit (bc1)", Chain::Btc),
                  new AddressTile("Tron", QStringLiteral("TRX · TRC-20 tokens"), Chain::Trx),
                  new AddressTile("Solana", QStringLiteral("SOL · SPL tokens"), Chain::Sol),
                  new AddressTile("Bitcoin legacy", QStringLiteral("P2PKH (1…)"), Chain::Btc)};
    outer->addLayout(addrGrid_);
    outer->addStretch(1);
    relayoutGrids();
    return pageScroll_;
}

// Flows the tile grids to the available width.
void MainWindow::relayoutGrids() {
    if (!assetGrid_) return;
    const int avail = width() - kGutter - kAsideW - 80;
    auto place = [](QGridLayout* g, const QVector<QWidget*>& all, const QVector<QWidget*>& shown, int cols) {
        for (QWidget* w : all) g->removeWidget(w);
        for (int i = 0; i < shown.size(); ++i) g->addWidget(shown[i], i / cols, i % cols);
        for (int c = 0; c < 3; ++c) g->setColumnStretch(c, c < cols ? 1 : 0);
    };
    QVector<QWidget*> coins, coinsShown, stables, stablesShown;
    for (AssetTile* t : tiles_) {
        const bool token = wv::isToken(t->chain());
        const bool show = showsAsset(t->chain());
        t->setVisible(show);
        (token ? stables : coins).push_back(t);
        if (show) (token ? stablesShown : coinsShown).push_back(t);
    }
    const int cols = avail >= 860 ? 3 : 2;
    place(assetGrid_, coins, coinsShown, cols);
    place(stableGrid_, stables, stablesShown, cols);
    stableBox_->setVisible(!stablesShown.isEmpty());

    const QString addrs[] = {current_.eth, current_.btcSegwit, current_.tron, current_.sol, current_.btcLegacy};
    QVector<QWidget*> all, shown;
    for (int i = 0; i < addrTiles_.size(); ++i) {
        all.push_back(addrTiles_[i]);
        if (current_.name.isEmpty() || !addrs[i].isEmpty()) shown.push_back(addrTiles_[i]);
    }
    const int n = std::max(1, int(shown.size()));
    place(addrGrid_, all, shown, std::min(n, avail >= 1000 ? 3 : (avail >= 620 ? 2 : 1)));
}

// A wallet holding keys shows every asset (a missing address then reads "No
// address"); a watch-only wallet shows just the chains of the address it watches.
bool MainWindow::showsAsset(Chain c) const {
    if (current_.name.isEmpty() || current_.encrypted) return true;
    return !wv::addressFor(current_, c).isEmpty();
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// True when the wallet UI is showing and no sheet is open (sheets disable it).
bool MainWindow::unlocked() const {
    return root_->currentWidget() == shell_ && root_->isEnabled() && model_->isUnlocked();
}

void MainWindow::onUnlocked(int action) {
    current_ = WalletView{};
    search_->setText({});
    reloadList();
    root_->setCurrentWidget(shell_);
    shell_->setFocus();
    ui::fadeIn(shell_, 420);
    // Create / Import chosen on the lock screen: open that sheet once the shell is in.
    if (action == LockScreen::Create) QTimer::singleShot(360, this, [this] { newWallet(); });
    else if (action == LockScreen::Import) QTimer::singleShot(360, this, [this] { importWallet(); });
}

void MainWindow::lockApp() {
    model_->lock();
    ++gen_;  // drop in-flight balance results
    pending_ = 0;
    current_ = WalletView{};
    refreshBtn_->setLoading(false);
    list_->setWallets({}, {});
    detail_->setCurrentIndex(0);
    lock_->reset();
    root_->setCurrentWidget(lock_);
    ui::fadeIn(lock_, 320);
}

void MainWindow::reloadList(const QString& select) {
    QVector<WalletView> all;
    try {
        all = model_->list();
    } catch (const std::exception& e) {
        Toast::notify(this, QString::fromUtf8(e.what()), Icon::Alert, true);
    }
    QString target = select.isEmpty() ? current_.name : select;
    const bool found = std::any_of(all.begin(), all.end(), [&](const WalletView& w) { return w.name == target; });
    if (!found) target = all.isEmpty() ? QString() : all.first().name;

    list_->setWallets(all, target);
    listCount_->setText(QString::number(all.size()));
    updateListState();

    if (target.isEmpty()) {
        ++gen_;
        pending_ = 0;
        current_ = WalletView{};
        refreshBtn_->setLoading(false);
        if (detail_->currentIndex() != 0) {
            detail_->setCurrentIndex(0);
            ui::fadeIn(detail_->currentWidget(), 320);
        }
        return;
    }
    if (target == current_.name) return;
    for (const auto& w : all)
        if (w.name == target) {
            showWallet(w);
            break;
        }
}

void MainWindow::activate(const QString& name) {
    if (name == current_.name) return;
    try {
        for (const auto& w : model_->list())
            if (w.name == name) {
                showWallet(w);
                return;
            }
    } catch (const std::exception& e) {
        Toast::notify(this, QString::fromUtf8(e.what()), Icon::Alert, true);
    }
}

void MainWindow::showWallet(const WalletView& w) {
    const bool switching = w.name != current_.name;
    current_ = w;
    primary_ = Chain::Eth;
    for (Chain c : kPrimaryOrder)
        if (!wv::addressFor(w, c).isEmpty()) {
            primary_ = c;
            break;
        }

    avatar_->setLabel(w.name);
    name_->setText(w.name);
    meta_->setText(wv::typeLabel(w) + (w.encrypted ? QStringLiteral(" · Encrypted on this device")
                                                   : QStringLiteral(" · No keys stored")));
    revealBtn_->setVisible(w.encrypted);
    sendBtn_->setVisible(w.encrypted);  // watch-only wallets hold no key to sign with
    hero_->setWallet(w, primary_);

    const QString addrs[] = {w.eth, w.btcSegwit, w.tron, w.sol, w.btcLegacy};
    for (int i = 0; i < addrTiles_.size(); ++i) {
        addrTiles_[i]->setAddress(addrs[i]);
        addrTiles_[i]->setVisible(!addrs[i].isEmpty());
    }
    relayoutGrids();

    for (auto* t : tiles_) t->reset();
    const bool fromEmpty = detail_->currentIndex() != 1;
    detail_->setCurrentIndex(1);
    if (switching || fromEmpty) {
        pageScroll_->verticalScrollBar()->setValue(0);
        ui::fadeIn(page_, 260);
        for (int i = 0; i < tiles_.size(); ++i) tiles_[i]->reveal(60 + 45 * i);
    }
    refresh();
}

void MainWindow::refresh() {
    if (current_.name.isEmpty()) return;
    ++gen_;
    pending_ = 0;
    model_->refreshPrices(gen_);  // market prices are independent of the wallet's balances
    for (auto* t : tiles_) {
        if (!wv::addressFor(current_, t->chain()).isEmpty()) {
            t->setLoading();
            ++pending_;
        } else {
            t->reset();
            if (wv::networkOf(t->chain()) == Chain::Sol && current_.type == "IMPORTED_PK")
                t->setToolTip("Solana addresses come from a recovery phrase. "
                              "This wallet was imported from a private key, so it has none.");
        }
    }
    applyPrices();  // reset() cleared tile values; re-show any cached prices right away
    if (pending_ == 0) {
        hero_->setStatus({}, false);
        return;
    }
    hero_->setLoading();
    hero_->setStatus("Refreshing", true);
    refreshBtn_->setLoading(true);
    assetsMeta_->setText(QStringLiteral("Updating balances…"));
    model_->refreshBalances(current_, gen_);
}

void MainWindow::onPriceReady(quint64 gen, int chain, double usd, double, double change7d) {
    if (gen != gen_) return;
    prices_.insert(chain, {usd, change7d});
    for (auto* t : tiles_)
        if ((int)t->chain() == chain) t->setPrice(usd, change7d);
    if (chain == (int)primary_) hero_->setPrice(usd, change7d);
}

void MainWindow::applyPrices() {
    for (auto* t : tiles_)
        if (auto it = prices_.constFind((int)t->chain()); it != prices_.constEnd())
            t->setPrice(it->usd, it->change7d);
    if (auto it = prices_.constFind((int)primary_); it != prices_.constEnd()) hero_->setPrice(it->usd, it->change7d);
}

void MainWindow::onBalanceReady(quint64 gen, int chain, QString value) {
    if (gen != gen_) return;
    const Chain c = Chain(chain);
    const bool ok = wv::isAmount(value);  // some checkers report errors as text
    for (auto* t : tiles_)
        if (t->chain() == c) ok ? t->setValue(value) : t->setFailed(value);
    if (c == primary_) ok ? hero_->setValue(value) : hero_->setFailed();
    settleOne();
}

void MainWindow::onBalanceFailed(quint64 gen, int chain, QString error) {
    if (gen != gen_) return;
    const Chain c = Chain(chain);
    for (auto* t : tiles_)
        if (t->chain() == c) t->setFailed(error);
    if (c == primary_) hero_->setFailed();
    settleOne();
}

void MainWindow::settleOne() {
    if (--pending_ > 0) return;
    pending_ = 0;
    refreshBtn_->setLoading(false);
    hero_->setStatus("Updated " + QTime::currentTime().toString("HH:mm"), false);
    int tracked = 0, funded = 0;
    for (auto* t : tiles_) {
        tracked += !wv::addressFor(current_, t->chain()).isEmpty();
        funded += t->hasPositive();
    }
    assetsMeta_->setText(QString("%1 of %2 with a balance").arg(funded).arg(tracked));
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::newWallet() {
    if (!unlocked()) return;
    const QString n = dlg::createWallet(this, model_);
    if (n.isEmpty()) return;
    reloadList(n);
    Toast::notify(this, "Wallet " + quoted(n) + " created");
}

void MainWindow::importWallet() {
    if (!unlocked()) return;
    const QString n = dlg::importWallet(this, model_);
    if (n.isEmpty()) return;
    reloadList(n);
    Toast::notify(this, "Wallet " + quoted(n) + " imported");
}

void MainWindow::watchAddress() {
    if (!unlocked()) return;
    const QString n = dlg::watchAddress(this, model_);
    if (n.isEmpty()) return;
    reloadList(n);
    Toast::notify(this, "Now watching " + quoted(n), Icon::Eye);
}

void MainWindow::updateListState() {
    const bool none = list_->count() == 0;
    const bool filteredOut = !none && list_->visibleCount() == 0;
    listEmpty_->setVisible(none || filteredOut);
    listEmpty_->setText(none ? "No wallets yet." : "No wallets match your search.");
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    relayoutGrids();
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    theme::applyTitleBar(this);
}
