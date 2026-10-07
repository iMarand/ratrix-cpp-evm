#pragma once
#include <QString>

class QWidget;
class WalletModel;
struct WalletView;

// Modal flows for wallet actions, presented as animated sheets over the main
// window. Errors are shown inline in the sheet.
namespace dlg {

// Each returns the new wallet's name, or an empty string if cancelled.
QString createWallet(QWidget* parent, WalletModel* m);
QString importWallet(QWidget* parent, WalletModel* m);
QString watchAddress(QWidget* parent, WalletModel* m);

// Forgotten app passphrase: explains the reset, asks for a new passphrase and
// moves the old encrypted wallets to a backup folder. Returns true once the
// model is unlocked with the new passphrase.
bool resetPassphrase(QWidget* parent, WalletModel* m);

// Send funds: pick an asset, enter a recipient + amount, review the fee, then
// hold-to-confirm to sign and broadcast. Returns true if a tx was broadcast.
bool sendFunds(QWidget* parent, WalletModel* m, const WalletView& w);

// Shows the private key / WIF / seed behind a press-and-hold confirmation.
void revealSecret(QWidget* parent, WalletModel* m, const QString& walletName);

// Asks for confirmation and deletes the wallet. Returns true if deleted.
bool confirmDelete(QWidget* parent, WalletModel* m, const WalletView& w);

}  // namespace dlg
