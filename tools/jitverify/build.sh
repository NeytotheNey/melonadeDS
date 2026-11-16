#!/bin/sh
# Builds jitverify for Windows x64 (MSYS2 MinGW, as desktop/build.sh) and Android arm64-v8a (the
# app's NDK and CMake) into build-jitverify/windows-x64/ and build-jitverify/android-arm64/.
#   tools/jitverify/build.sh            both
#   tools/jitverify/build.sh windows    or android, just one
# Build output goes to logs/jitverify-build-<platform>.log
set -e
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
WHICH="${1:-all}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
mkdir -p logs

MINGW="${MINGW:-/c/msys64/mingw64}"
SDK="${ANDROID_SDK:-/c/Tools/android-sdk}"
NDK_VERSION="${NDK_VERSION:-28.0.13004108}"   # buildSrc AppConfig.ndkVersion
SDK_CMAKE="$SDK/cmake/3.22.1/bin"

build_windows() {
    log="$ROOT/logs/jitverify-build-windows.log"
    echo "windows x64 -> build-jitverify/windows-x64 (log: logs/jitverify-build-windows.log)"
    (
        export PATH="$MINGW/bin:$PATH"
        cmake -G Ninja -S tools/jitverify -B build-jitverify/windows-x64 -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH="$MINGW" &&
        cmake --build build-jitverify/windows-x64
    ) > "$log" 2>&1 || { tail -n 30 "$log"; echo "windows build failed"; exit 1; }
    echo "built build-jitverify/windows-x64/jitverify.exe"
}

build_android() {
    log="$ROOT/logs/jitverify-build-android.log"
    echo "android arm64-v8a -> build-jitverify/android-arm64 (log: logs/jitverify-build-android.log)"
    (
        "$SDK_CMAKE/cmake.exe" -G Ninja -S tools/jitverify -B build-jitverify/android-arm64 \
            -DCMAKE_MAKE_PROGRAM="$SDK_CMAKE/ninja.exe" \
            -DCMAKE_TOOLCHAIN_FILE="$SDK/ndk/$NDK_VERSION/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" &&
        "$SDK_CMAKE/cmake.exe" --build build-jitverify/android-arm64
    ) > "$log" 2>&1 || { tail -n 30 "$log"; echo "android build failed"; exit 1; }
    # the NDK builds with -g: jitverify keeps the symbols (for tombstones), the stripped copy is the one to push
    "$SDK/ndk/$NDK_VERSION/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" \
        -o build-jitverify/android-arm64/jitverify.stripped build-jitverify/android-arm64/jitverify
    echo "built build-jitverify/android-arm64/jitverify (and jitverify.stripped to push)"
}

case "$WHICH" in
    windows) build_windows ;;
    android) build_android ;;
    all) build_windows; build_android ;;
    *) echo "usage: $0 [windows|android|all]"; exit 1 ;;
esac
