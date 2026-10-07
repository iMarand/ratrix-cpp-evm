// Ratrix Wallet - Qt6 desktop wallet.
// Shares the exact crypto core and encrypted wallet store with the CLI, so
// wallets created in one show up in the other.

#include <QApplication>
#include <QIcon>

#include <cstdlib>
#include <filesystem>
#include <vector>

#include "mainwindow.h"
#include "theme.h"
#include "walletmodel.h"

namespace fs = std::filesystem;

// Point libcurl at a CA bundle so TLS verification works on dev machines.
static void setupTls(const char* argv0) {
    if (const char* s = std::getenv("RATRIX_CAINFO"); s && *s) return;
    std::vector<fs::path> roots;
    std::error_code ec;
    fs::path exe = fs::absolute(fs::path(argv0 ? argv0 : ""), ec);
    for (fs::path d = exe.parent_path(); !d.empty();) {
        roots.push_back(d);
        fs::path up = d.parent_path();
        if (up == d) break;
        d = up;
    }
    roots.push_back(fs::current_path(ec));
    for (const auto& r : roots) {
        fs::path cand = r / "core" / "balance" / "curl-ca-bundle.crt";
        if (fs::exists(cand)) {
#ifdef _WIN32
            _putenv_s("RATRIX_CAINFO", cand.string().c_str());
#else
            setenv("RATRIX_CAINFO", cand.string().c_str(), 1);
#endif
            return;
        }
    }
}

int main(int argc, char** argv) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setApplicationName("Ratrix Wallet");
    QApplication::setOrganizationName("Ratrix");

    setupTls(argc > 0 ? argv[0] : "");
    theme::apply();

    // Title bar / taskbar icon: the compact mark at tiny sizes, full art above.
    QIcon icon;
    for (int s : {16, 20, 24}) icon.addFile(QString(":/brand/mini-%1.png").arg(s), QSize(s, s));
    for (int s : {32, 48, 64, 128, 256}) icon.addFile(QString(":/brand/logo-%1.png").arg(s), QSize(s, s));
    QApplication::setWindowIcon(icon);

    // Intentionally heap-allocated and never deleted: detached balance threads
    // may finish after the window closes, and this keeps their target alive.
    auto* model = new WalletModel();

    // The window opens on its lock screen: set (first run) or enter the one app
    // passphrase before any wallet is shown.
    MainWindow w(model);
    w.show();
    return app.exec();
}
