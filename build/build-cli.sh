#!/usr/bin/env bash
set -euo pipefail

# Builds the Ratrix Wallet CLI (app/main.cpp) -> ratrix(.exe) in the repo root.
# Goes through CMake so the vendored libsecp256k1 (transaction signing for
# `ratrix send`) is built and linked statically, the same way as for the GUI.

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

UNAME="$(uname -s 2>/dev/null || echo Unknown)"
case "$UNAME" in
  MINGW*|MSYS*|CYGWIN*)
    [[ -d /c/msys64/mingw64/bin ]] && export PATH="/c/msys64/mingw64/bin:$PATH"
    EXE="ratrix.exe"
    ;;
  *)
    EXE="ratrix"
    ;;
esac

GEN=()
if [[ ! -f build-cli/CMakeCache.txt ]] && command -v ninja >/dev/null; then GEN=(-G Ninja); fi

echo "|| Configuring build-cli/"
cmake -S . -B build-cli "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DRATRIX_BUILD_CLI=ON -DRATRIX_BUILD_GUI=OFF >/dev/null
echo "|| Compiling app/main.cpp"
cmake --build build-cli --target ratrix
cp "build-cli/$EXE" "$ROOT/$EXE"
echo "|| Built: $ROOT/$EXE"
