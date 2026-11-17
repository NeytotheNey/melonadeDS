#!/bin/bash
# Pulls the PGO .profraw files from a phone running the -PnativePgo=generate build and merges them into app/pgo/arm64-v8a.profdata
# (tools/pgo/README.md).
#   tools/pgo/merge.sh <adb serial>             pull everything in files/pgo/ and merge it
#   tools/pgo/merge.sh <adb serial> --clear     the same, then delete the phone's .profraw files once the merge worked
#   tools/pgo/merge.sh --local <dir>            merge .profraw files already on the PC (no adb)
# The raw files are kept in logs/pgo-raw/<time>/. OUT=<path> writes the profile somewhere else.
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"

SDK="$(cygpath -u "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-C:/Tools/android-sdk}}")"
ADB="$SDK/platform-tools/adb.exe"
PKG="${PKG:-me.magnum.melonds.dev}"
# llvm-profdata has to come from the NDK the app builds with, or the profile format may not match its clang
NDK_VERSION="$(sed -n 's/.*ndkVersion = "\(.*\)".*/\1/p' buildSrc/src/main/kotlin/AppConfig.kt)"
PROFDATA="$(ls "$SDK"/ndk/"$NDK_VERSION"/toolchains/llvm/prebuilt/*/bin/llvm-profdata* 2>/dev/null | head -n 1 || true)"
[ -n "$PROFDATA" ] || { echo "no llvm-profdata in $SDK/ndk/$NDK_VERSION"; exit 1; }
OUT="${OUT:-$ROOT/app/pgo/arm64-v8a.profdata}"

usage() { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[ $# -ge 1 ] || usage

CLEAR=0
if [ "$1" = "--local" ]; then
    [ $# -eq 2 ] || usage
    RAW="$(cd "$2" && pwd)"
else
    SERIAL="$1"
    [ "${2:-}" = "--clear" ] && CLEAR=1
    A=("$ADB" -s "$SERIAL")
    RAW="$ROOT/logs/pgo-raw/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$RAW"

    FILES="$("${A[@]}" shell run-as "$PKG" ls files/pgo 2>/dev/null | tr -d '\r' | grep '\.profraw$' || true)"
    [ -n "$FILES" ] || { echo "no .profraw files in $PKG's files/pgo (is the generate build installed and has it paused or stopped once?)"; exit 1; }
    for f in $FILES; do
        "${A[@]}" exec-out run-as "$PKG" cat "files/pgo/$f" > "$RAW/$f"
    done
    echo "pulled $(echo "$FILES" | wc -l) files to logs/pgo-raw/$(basename "$RAW")"
fi

shopt -s nullglob
INPUTS=("$RAW"/*.profraw)
[ ${#INPUTS[@]} -gt 0 ] || { echo "no .profraw files in $RAW"; exit 1; }
WIN_INPUTS=()
for f in "${INPUTS[@]}"; do
    # An empty file is a dump that was cut short (the process was killed mid-write); llvm-profdata rejects it
    if [ -s "$f" ]; then WIN_INPUTS+=("$(cygpath -w "$f")"); else echo "skipping empty $(basename "$f")"; fi
done
[ ${#WIN_INPUTS[@]} -gt 0 ] || { echo "every .profraw file is empty"; exit 1; }

mkdir -p "$(dirname "$OUT")"
# --failure-mode=all skips a truncated file with a warning instead of failing the whole merge
"$PROFDATA" merge --failure-mode=all -o "$(cygpath -w "$OUT")" "${WIN_INPUTS[@]}"
echo "merged ${#WIN_INPUTS[@]} files into $OUT (less any a warning above says were skipped)"
"$PROFDATA" show "$(cygpath -w "$OUT")" | tail -n 3

if [ "$CLEAR" = 1 ]; then
    # Only the files pulled above, so a dump written during the merge stays for the next one
    for f in $FILES; do
        "${A[@]}" shell run-as "$PKG" rm -f "files/pgo/$f"
    done
    echo "cleared the $(echo "$FILES" | wc -l) pulled .profraw files from the phone"
fi
