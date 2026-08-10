#!/usr/bin/env bash
set -euo pipefail

# Builds the Yasmarang weak-entropy scanner and KEEPS the binary on disk.
#
# Unlike ./build/gpp.sh --debug, this never deletes the output: the binary is
# written next to the project root as threads-choose(.exe) and stays there.
#
# Usage:
#   ./build/threads-choose.sh            # build only
#   ./build/threads-choose.sh --run      # build, then run it
#   ./build/threads-choose.sh --force    # rebuild even if the binary is current

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

SRC="BruteForce/BruteForce-Ratrix-Examples/threads-choose.cpp"
RUN_AFTER=false
FORCE=false

for arg in "$@"; do
  case "$arg" in
    --run)   RUN_AFTER=true ;;
    --force) FORCE=true ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

# ---------- OS detection ----------
UNAME="$(uname -s 2>/dev/null || echo Unknown)"
IS_WINDOWS=false
case "$UNAME" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=true ;;
esac

if [[ "$IS_WINDOWS" == true ]]; then
  OUT="$ROOT/threads-choose.exe"
  # MSYS2 toolchain is not always on PATH when invoked from VS Code / plain bash
  if [[ -d /c/msys64/mingw64/bin ]]; then
    export PATH="/c/msys64/mingw64/bin:$PATH"
  fi
  LIBS="-lcurl -lssl -lcrypto -lws2_32 -lcrypt32 -lwldap32 -lz -pthread"
else
  OUT="$ROOT/threads-choose"
  LIBS="-lcurl -lssl -lcrypto -lz -pthread"
fi

# ---------- Skip the (slow) rebuild when nothing changed ----------
# targets.h is 3.6 MB of string literals, so a full compile takes a minute or two.
if [[ "$FORCE" == false && -f "$OUT" ]]; then
  NEWER="$(find "$SRC" RTX_LIBS.h Libs BruteForce/targetLists.h BruteForce/targets.h \
             -newer "$OUT" -print -quit 2>/dev/null || true)"
  if [[ -z "$NEWER" ]]; then
    echo "|| ✅ $OUT is up to date (use --force to rebuild)"
    [[ "$RUN_AFTER" == true ]] && exec "$OUT"
    exit 0
  fi
fi

# A still-running copy holds a lock on the binary and the linker fails with a
# bare "Permission denied", so name the real problem instead.
if [[ "$IS_WINDOWS" == true && -f "$OUT" ]]; then
  if tasklist //FI "IMAGENAME eq threads-choose.exe" 2>/dev/null | grep -qi "threads-choose.exe"; then
    echo "❌ threads-choose.exe is still running - close it first:"
    echo "     taskkill //IM threads-choose.exe //F"
    exit 1
  fi
fi

echo "|| 🖥️  OS: $UNAME"
echo "|| 🔧 Compiling: $SRC"
echo "|| ➡️  Output: $OUT"
echo "|| ⏳ This takes 1-2 minutes (targets.h is ~78k string literals)."

# No -O2 on purpose: cc1plus runs out of memory optimizing targets.h, and the
# runtime is dominated by PBKDF2 inside OpenSSL anyway.
g++ -std=c++17 "$SRC" -o "$OUT" $LIBS

echo "|| ✅ Build successful, binary kept at: $OUT"

if [[ "$RUN_AFTER" == true ]]; then
  echo "|| 🚀 Running..."
  echo ""
  "$OUT"
fi
