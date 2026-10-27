#!/bin/sh
# ========================================================================================
# RetroSaveConverter — cross-build script (Linux / GitHub Actions)
# Produces a static RetroSaveConverter.exe compatible with Windows 7 x64 and later.
#
# Requires: g++-mingw-w64-x86-64 and binutils-mingw-w64-x86-64
#   Debian/Ubuntu: sudo apt-get install g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
# ========================================================================================
set -e

CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
OUT="${1:-RetroSaveConverter.exe}"

cd "$(dirname "$0")"
mkdir -p build

echo "[1/3] compiling resources..."
"$WINDRES" src/app.rc -O coff -o build/app.res -I src

echo "[2/3] compiling sources..."
"$CXX" -std=c++17 -O2 -Wall -Wextra \
    -DUNICODE -D_UNICODE \
    -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600 \
    -municode -mwindows \
    -static -static-libgcc -static-libstdc++ \
    src/main.cpp src/converter.cpp build/app.res \
    -o "$OUT" \
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luxtheme

echo "[3/3] stripping..."
"$WINDRES" --version > /dev/null
strip="${CXX%-g++-posix}-strip"
if command -v "$strip" > /dev/null 2>&1; then
    "$strip" "$OUT"
fi

echo "Done: $OUT"
