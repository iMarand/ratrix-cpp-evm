#!/usr/bin/env bash
# Produces a self-contained portable folder for RatrixWallet: the exe, every Qt
# and mingw DLL it needs, Qt plugins, and the TLS CA bundle. Output: <repo>/dist/RatrixWallet
set -euo pipefail
export PATH=/c/msys64/mingw64/bin:$PATH
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE_SRC="${1:-$REPO/build-dev/gui/RatrixWallet.exe}"
OUT="${2:-$REPO/dist/RatrixWallet}"

[ -f "$EXE_SRC" ] || { echo "exe not found: $EXE_SRC (build it first)"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/core/balance"
cp "$EXE_SRC" "$OUT/RatrixWallet.exe"
cp "$REPO/core/balance/curl-ca-bundle.crt" "$OUT/core/balance/"

# Qt libraries + plugins (platforms, styles, tls, imageformats, ...).
windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$OUT/RatrixWallet.exe" >/dev/null

# Every remaining mingw64 DLL the exe and the deployed plugin DLLs need.
pull() {
  for f in $(ldd "$@" 2>/dev/null | awk '/\/mingw64\//{print $3}' | sort -u); do
    bn="$(basename "$f")"; [ -f "$OUT/$bn" ] || cp "$f" "$OUT/"
  done
}
pull "$OUT/RatrixWallet.exe"
# plugins pull a few extra libs (e.g. the tls backends, image codecs).
for d in platforms styles tls imageformats iconengines networkinformation generic; do
  [ -d "$OUT/$d" ] && pull "$OUT/$d"/*.dll || true
done
pull "$OUT/RatrixWallet.exe"  # second pass: deps of the libs just copied

echo "dist ready: $OUT  ($(du -sh "$OUT" | cut -f1))"
