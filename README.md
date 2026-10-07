# Ratrix Wallet

A multi-chain wallet built on a small, dependency-light C++ crypto core. One
private key produces an **Ethereum/EVM** address (ETH, BNB, ERC-20/BEP-20
tokens) and a **Bitcoin** address (native SegWit + legacy). Ships as a secure
command-line wallet and a Qt6 desktop wallet that share the same core and the
same encrypted wallet files.

The core keeps the project's original trait: **any input** (words, a phrase,
entropy) deterministically derives a key, used as a BIP39-style passphrase.

## Layout

```
core/                 Crypto core (header-only, OpenSSL-based)
  rtx.h               Umbrella: RTX::toSeed / toPrivateKey / toAddress / toBtcAddress …
  bitcoin.h           BTC P2PKH + P2WPKH (bech32) + WIF
  ripemd160.h         Self-contained RIPEMD-160 (for HASH160)
  bip39.h, keccak256.h, seed_to_pk.h, …   derivation primitives
  balance/            Read-only balance lookups (ETH, BNB, BTC, USDT, USDC)
app/                  Secure command-line wallet (ratrix)
  walletstore.h       Encrypted-at-rest wallet files (scrypt + AES-256-GCM)
gui/                  Qt6 desktop wallet (RatrixWallet)
third_party/          nlohmann/json
build/                Build scripts
examples/             Standalone reference snippets
legacy/               Quarantined older code — NOT part of the product, not built
```

## Build

Toolchain: MSYS2 MinGW-w64 (g++ 13+), OpenSSL 3, libcurl, and (for the GUI)
Qt 6 + CMake.

```sh
# CLI only
bash build/build-cli.sh            # -> ./ratrix(.exe)

# CLI + GUI via CMake
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-dev            # -> build-dev/gui/RatrixWallet(.exe), ./ratrix
```

## CLI usage

```
ratrix new <name>                      create a new HD wallet (EVM + BTC)
ratrix import <name> --pk <hex>        import from a private key
ratrix import <name> --seed "<words>"  import from a seed phrase
ratrix import <name> --entropy <hex>   import from entropy
ratrix watch  <name> <address>         track an address (read-only, no keys)
ratrix list                            list wallets
ratrix show   <name> [--secret]        show addresses (optionally reveal secret)
ratrix balance <name|address> [chain]  --eth | --bnb | --btc | --usdt | --usdc [--bsc]
ratrix export <name>                   reveal private key / WIF (asks passphrase)
ratrix remove <name> [--yes]           delete a wallet file
```

Wallets live in `%APPDATA%\Ratrix\wallets` (Windows) or `~/.ratrix/wallets`,
overridable with `RATRIX_WALLET_DIR`. The GUI reads the same files.

## Security

- **Keys encrypted at rest.** Private key, seed phrase and entropy are encrypted
  with a key derived from your passphrase (scrypt N=2¹⁵) using AES-256-GCM.
  Public metadata (addresses) stays in cleartext so balances are viewable
  without unlocking. Watch-only wallets store no secret.
- **CSPRNG.** Entropy/keys come from OpenSSL `RAND_bytes`, not `std::mt19937`.
- **TLS verification on** for all balance lookups. The CLI/GUI locate the bundled
  CA file automatically; set `RATRIX_CAINFO` to override.

> **History note:** earlier commits tracked a `.env` containing a real private
> key and API key, and plaintext wallet JSON files. These are now untracked and
> git-ignored, but they remain in prior git history — rotate those keys and, if
> the repo is shared, scrub history (e.g. `git filter-repo`).

## `legacy/`

Quarantined older code (address-scanning / brute-force experiments, stale
snapshots). It is not compiled, not maintained, and not part of the wallet
product. Don't build on it.
