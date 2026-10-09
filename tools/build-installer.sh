#!/usr/bin/env bash
# Builds the R-Wallet Windows installer(s). Requires a built RatrixWallet.exe
# and NSIS (makensis). Produces:
#   dist/R-Wallet-<ver>-Setup.exe        (all users; needs admin to install)
#   dist/R-Wallet-<ver>-Setup-User.exe   (just me; no admin)   [--peruser/--both]
set -euo pipefail
export PATH=/c/msys64/mingw64/bin:$PATH
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Version: $VERSION if set, else the one in CMakeLists.txt (single source).
VER="${VERSION:-$(sed -n 's/^project(Ratrix VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")}"
[ -n "$VER" ] || { echo "could not read the version from CMakeLists.txt"; exit 1; }
MODE="${1:-both}"   # all | peruser | both
EXE="$REPO/build-dev/gui/RatrixWallet.exe"
DIST="$REPO/dist/RatrixWallet"
OUT="$REPO/dist"

command -v makensis >/dev/null || { echo "makensis (NSIS) not found on PATH"; exit 1; }
bash "$REPO/tools/make-dist.sh" "$EXE" "$DIST"   # refresh the portable folder

cd "$REPO/packaging"
build() { # <suffix> <extra-nsis-flags>
  local out="$OUT/R-Wallet-$VER-Setup$1.exe"
  makensis -V2 -DVERSION="$VER" -DDIST="$DIST" -DOUTFILE="$out" $2 installer.nsi
  echo "built: $out"
}
case "$MODE" in
  all)     build ""      "" ;;
  peruser) build "-User" "-DPERUSER" ;;
  both)    build ""      "";   build "-User" "-DPERUSER" ;;
  *) echo "usage: build-installer.sh [all|peruser|both]"; exit 1 ;;
esac
