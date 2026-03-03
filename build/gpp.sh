#!/usr/bin/env bash
set -euo pipefail

# Cross-platform g++ build helper for Windows (MSYS2/MinGW) + Linux
# Usage:
#   ./build/gpp.sh <source1.cpp> [source2.cpp ...] <output_name> [--debug]
#   ./build/gpp.sh <source.cpp> --debug              (build+run temp binary)

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <source1.cpp> [source2.cpp ...] [output_name|--debug]"
  exit 1
fi

# ---------- Parse args ----------
DEBUG_MODE=false
SRC_ARRAY=()

for arg in "$@"; do
  if [[ "$arg" == "--debug" ]]; then
    DEBUG_MODE=true
  else
    SRC_ARRAY+=("$arg")
  fi
done

if [[ "${#SRC_ARRAY[@]}" -lt 1 ]]; then
  echo "Error: No sources provided."
  exit 1
fi

if [[ "$DEBUG_MODE" == true ]]; then
  OUT="/tmp/debug_$(basename "${SRC_ARRAY[0]}" .cpp)_$$"
  SRC=("${SRC_ARRAY[@]}")
else
  if [[ "${#SRC_ARRAY[@]}" -lt 2 ]]; then
    echo "Error: Provide an output name as the last argument (or use --debug)."
    exit 1
  fi
  OUT="${SRC_ARRAY[-1]}"
  SRC=("${SRC_ARRAY[@]:0:${#SRC_ARRAY[@]}-1}")
fi

# ---------- OS detection ----------
UNAME="$(uname -s 2>/dev/null || echo Unknown)"
IS_WINDOWS=false
case "$UNAME" in
  MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=true ;;
esac

# ---------- Helpers ----------
have_pkg() { pkg-config --exists "$1" 2>/dev/null; }

pkg_flags_for() {
  # Print flags for packages that exist (ignore missing)
  local pkgs=("$@")
  local existing=()
  for p in "${pkgs[@]}"; do
    if have_pkg "$p"; then existing+=("$p"); fi
  done
  if ((${#existing[@]})); then
    pkg-config --cflags --libs "${existing[@]}" 2>/dev/null
  fi
}

# ---------- Build flags ----------
# Packages you *might* have installed. We only use those present.
PKG_FLAGS="$(
  pkg_flags_for \
    Qt6Widgets Qt6Gui Qt6Core Qt6Network Qt6Multimedia Qt6MultimediaWidgets Qt6Sql \
    libcurl openssl protobuf grpc++ grpc absl_base absl_log_internal_message
)"

# Extra include/lib paths only needed for MSYS2/MinGW setups.
EXTRA_FLAGS=""
MANUAL_LIBS=""

if [[ "$IS_WINDOWS" == true ]]; then
  # Windows (MSYS2/MinGW) names/paths
  EXTRA_FLAGS="-I/mingw64/include/boost -I/mingw64/include/grpcpp -L/mingw64/lib"
  MANUAL_LIBS="-lboost_thread-mt -lboost_filesystem-mt -lgrpc++ -lgrpc -lgrpc++_reflection -lpcre2-8 -pthread -lshell32 -lsqlite3 -lws2_32 -luser32"
  # Common output extension for Windows if user didn't add one
  if [[ "$DEBUG_MODE" == false && "$OUT" != *.exe ]]; then
    OUT="${OUT}.exe"
  fi
else
  # Linux (and most Unix) names/paths
  # No mingw64 includes, no shell32/user32
  EXTRA_FLAGS=""
  MANUAL_LIBS="-lboost_thread -lboost_filesystem -lgrpc++ -lgrpc -lgrpc++_reflection -lpcre2-8 -pthread -lsqlite3"
fi

# Debug
if [[ "$DEBUG_MODE" == true ]]; then
  EXTRA_FLAGS="$EXTRA_FLAGS -g"
  echo "==============================================================================="
  echo "|| 🐛 Debug mode enabled"
fi

echo "|| 🖥️  OS: $UNAME"
echo "|| 🔧 Compiling sources: ${SRC[*]}"
echo "|| ➡️ Output: $OUT"

# ---------- Compile ----------
# Note: If you don't have some libs installed, remove them from MANUAL_LIBS or install dev packages.
set +e
g++ -std=c++17 "${SRC[@]}" -o "$OUT" $PKG_FLAGS $EXTRA_FLAGS $MANUAL_LIBS
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "❌ Build failed"
  echo
  echo "Tips:"
  if [[ "$IS_WINDOWS" == true ]]; then
    echo "  - Ensure MSYS2 packages exist: mingw-w64-x86_64-boost, mingw-w64-x86_64-curl, mingw-w64-x86_64-openssl, mingw-w64-x86_64-grpc, mingw-w64-x86_64-protobuf, etc."
  else
    echo "  - Install dev packages (Ubuntu/Debian):"
    echo "      sudo apt update && sudo apt install -y build-essential pkg-config libcurl4-openssl-dev libssl-dev libboost-thread-dev libboost-filesystem-dev libpcre2-dev libsqlite3-dev"
    echo "  - If you don't use grpc/protobuf/qt in this build, remove them from MANUAL_LIBS / PKG_FLAGS."
  fi
  exit 1
fi

echo "|| ✅ Build successful: $OUT"

# ---------- Run in debug mode ----------
if [[ "$DEBUG_MODE" == true ]]; then
  echo "|| 🚀 Running in debug mode..."
  echo "==============================================================================="
  echo ""
  "$OUT"
  EXIT_CODE=$?
  rm -f "$OUT"
  echo ""
  echo "🏁 Program exited with code: $EXIT_CODE"
  exit $EXIT_CODE
fi