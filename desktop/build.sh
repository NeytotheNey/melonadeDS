#!/bin/sh
# Builds melonade-desktop with MSYS2's MinGW-w64 toolchain into build-desktop/ at the repo root.
# Needs (once): pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2, plus CMake and Ninja
set -e
cd "$(dirname "$0")/.."
MINGW="${MINGW:-/c/msys64/mingw64}"
export PATH="$MINGW/bin:$PATH"
cmake -G Ninja -S desktop -B build-desktop -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH="$MINGW"
cmake --build build-desktop
echo "built build-desktop/melonade-desktop.exe"
