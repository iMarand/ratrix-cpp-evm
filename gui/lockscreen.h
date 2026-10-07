#pragma once
#include <QWidget>

class Button;
class QLabel;
class StrengthMeter;
class TextField;
class WalletModel;

// Full-window gate shown at startup and after "Lock": first run sets the one
// app passphrase, later runs verify it. A single centered column, no chrome.
// "Create wallet" / "Import wallet" unlock with the typed passphrase and go
// straight to that flow; with the field empty they offer a passphrase reset
// (for a forgotten passphrase) first.
class LockScreen : public QWidget {
    Q_OBJECT
public:
    enum Action { None = 0, Create = 1, Import = 2 };
    explicit LockScreen(WalletModel* m, QWidget* parent = nullptr);
    void reset();  // re-read first-run state, clear fields, focus

signals:
    void unlocked(int action);  // Action to run once the wallet UI is showing

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    void submit(int action);
    void showError(const QString& msg);
    Button* activeGo() const;

    WalletModel* m_;
    bool firstRun_ = false;
    QWidget* column_;
    QLabel* title_;
    QLabel* subtitle_;
    QLabel* error_;
    QLabel* hint_;
    TextField* pw_;
    TextField* pw2_;
    Button* go_;   // arrow inside the passphrase field
    Button* go2_;  // arrow inside the confirm field (first run)
    Button* create_;
    Button* import_;
    QWidget* setupBox_;
    StrengthMeter* meter_;
};
