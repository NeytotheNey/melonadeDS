#!/bin/sh
# Builds melonade-desktop with MSYS2's MinGW-w64 toolchain into build-desktop/ at the repo root (BUILD_DIR to build elsewhere, for example
# when the exe there is running), then puts a ready-to-zip folder in BUILD_DIR/release/melonadeDS-<version>/: the exe, SDL2.dll,
# gamecontrollerdb.txt and licenses/ (every library inside the exe and SDL2.dll). It runs from there on a PC without MSYS2.
# Needs (once): pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2, plus CMake and Ninja
set -e
cd "$(dirname "$0")/.."
MINGW="${MINGW:-/c/msys64/mingw64}"
BUILD="${BUILD_DIR:-build-desktop}"
export PATH="$MINGW/bin:$PATH"
cmake -G Ninja -S desktop -B "$BUILD" -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH="$MINGW"
cmake --build "$BUILD"
echo "built $BUILD/melonade-desktop.exe"

# the release folder, named for the app's versionName (buildSrc AppConfig.kt, as the exe's version info)
VERSION=$(sed -n 's/.*versionName = "\([0-9.]*\)".*/\1/p' buildSrc/src/main/kotlin/AppConfig.kt)
OUT="$BUILD/release/melonadeDS-$VERSION"
mkdir -p "$OUT/licenses"
cp "$BUILD/melonade-desktop.exe" "$BUILD/SDL2.dll" desktop/third_party/gamecontrollerdb/gamecontrollerdb.txt "$OUT/"
L="$OUT/licenses"
cp LICENSE "$L/melonadeDS.txt"
cp melonDS-android-lib/LICENSE "$L/melonDS.txt"
cp "$MINGW/share/licenses/SDL2/LICENSE.txt" "$L/SDL2.txt"
cp desktop/third_party/gamecontrollerdb/LICENSE "$L/SDL_GameControllerDB.txt"
cp desktop/third_party/imgui/LICENSE.txt "$L/Dear-ImGui.txt"
cp desktop/third_party/picojson/LICENSE "$L/picojson.txt"
cp desktop/third_party/sqlite/PUBLIC-DOMAIN.txt "$L/SQLite.txt"
cp desktop/third_party/miniz/LICENSE "$L/miniz.txt"
cp desktop/third_party/lzma/lzma-sdk.txt "$L/LZMA-SDK.txt"
cp desktop/third_party/fonts/roboto/OFL.txt "$L/Roboto.txt"
cp desktop/third_party/fonts/material-symbols/LICENSE "$L/Material-Symbols.txt"
cp app/src/main/cpp/lua/LICENSE "$L/Lua.txt"
cp app/src/main/cpp/rcheevos/LICENSE "$L/rcheevos.txt"
cp app/src/main/cpp/enet/LICENSE "$L/ENet.txt"
cp melonDS-android-lib/src/net/libslirp/COPYRIGHT "$L/libslirp.txt"
cp "$MINGW/share/licenses/winpthreads/COPYING" "$L/winpthreads.txt"
echo "release folder $OUT"
