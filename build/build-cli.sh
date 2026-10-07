#!/usr/bin/env bash
set -euo pipefail

# Builds the Ratrix Wallet CLI (app/main.cpp) -> ratrix(.exe) in the repo root.

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
SRC="app/main.cpp"

UNAME="$(uname -s 2>/dev/null || echo Unknown)"
case "$UNAME" in
  MINGW*|MSYS*|CYGWIN*)
    [[ -d /c/msys64/mingw64/bin ]] && export PATH="/c/msys64/mingw64/bin:$PATH"
    OUT="$ROOT/ratrix.exe"
    LIBS="-lcurl -lssl -lcrypto -lws2_32 -lcrypt32 -lwldap32 -lz"
    ;;
  *)
    OUT="$ROOT/ratrix"
    LIBS="-lcurl -lssl -lcrypto -lz -pthread"
    ;;
esac

echo "|| Compiling $SRC -> $OUT"
g++ -std=c++17 -O2 "$SRC" -o "$OUT" $LIBS
echo "|| Built: $OUT"
