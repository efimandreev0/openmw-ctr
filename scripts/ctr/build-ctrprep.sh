#!/bin/bash
# Builds the PC preprocessor (apps/ctrprep) for Linux and, with MinGW-w64 installed, for Windows.
#   scripts/ctr/build-ctrprep.sh [output folder]
# On Windows itself: cmake -S apps/ctrprep -B build && cmake --build build --config Release
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build-ctrprep}"
mkdir -p "$OUT"

cmake -S "$ROOT/apps/ctrprep" -B "$OUT/linux" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$OUT/linux" -j"$(nproc)"
cp "$OUT/linux/ctrprep" "$OUT/ctrprep"
strip "$OUT/ctrprep"

# The posix thread model is needed for std::thread.
MINGW=$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++ || true)
if [ -n "$MINGW" ]; then
    cmake -S "$ROOT/apps/ctrprep" -B "$OUT/windows" -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER="$MINGW" > /dev/null
    cmake --build "$OUT/windows" -j"$(nproc)"
    cp "$OUT/windows/ctrprep.exe" "$OUT/ctrprep.exe"
    "${MINGW%g++*}strip" "$OUT/ctrprep.exe" 2>/dev/null || x86_64-w64-mingw32-strip "$OUT/ctrprep.exe"
else
    echo "MinGW-w64 not found: Windows build skipped"
fi
ls -la "$OUT"/ctrprep*
