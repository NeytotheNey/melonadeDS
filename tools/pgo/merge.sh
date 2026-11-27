#!/bin/bash
# Pulls the PGO .profraw files from a phone running a -PnativePgo=generate build and merges them into a profile (tools/pgo/README.md).
#   tools/pgo/merge.sh <adb serial>                       debug build (io.github.neytotheney.melonadeds.dev) into app/pgo/arm64-v8a.profdata
#   tools/pgo/merge.sh <adb serial> --release             release build (io.github.neytotheney.melonadeds.pgo) into app/pgo/arm64-v8a-release.profdata
#   tools/pgo/merge.sh <adb serial> [--release] --clear   the same, then delete the pulled .profraw files from the phone once the merge worked
#   tools/pgo/merge.sh --local <dir> [--release]          merge .profraw files already on the PC (no adb)
# Files come from the external files dir (files/pgo/ under /sdcard/Android/data/<package>, plain adb) and, for a debuggable build, the internal
# one through run-as (older generate builds wrote there). The raw files are kept in logs/pgo-raw/<time>/. PKG=<package> pulls another package,
# OUT=<path> writes the profile somewhere else.
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[ $# -ge 1 ] || usage

SERIAL=""
LOCAL=""
RELEASE=0
CLEAR=0
if [ "$1" = "--local" ]; then
    [ $# -ge 2 ] || usage
    LOCAL="$2"
    shift 2
else
    # A flag here means the serial was left out
    case "$1" in -*) usage ;; esac
    SERIAL="$1"
    shift
fi
for arg in "$@"; do
    case "$arg" in
        --release) RELEASE=1 ;;
        --clear) [ -z "$LOCAL" ] || usage; CLEAR=1 ;;
        *) usage ;;
    esac
done

if [ "$RELEASE" = 1 ]; then
    PKG="${PKG:-io.github.neytotheney.melonadeds.pgo}"
    OUT="${OUT:-$ROOT/app/pgo/arm64-v8a-release.profdata}"
else
    PKG="${PKG:-io.github.neytotheney.melonadeds.dev}"
    OUT="${OUT:-$ROOT/app/pgo/arm64-v8a.profdata}"
fi

SDK="$(cygpath -u "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-C:/Tools/android-sdk}}")"
ADB="$SDK/platform-tools/adb.exe"
# llvm-profdata has to come from the NDK the app builds with, or the profile format may not match its clang
NDK_VERSION="$(sed -n 's/.*ndkVersion = "\(.*\)".*/\1/p' buildSrc/src/main/kotlin/AppConfig.kt)"
PROFDATA="$(ls "$SDK"/ndk/"$NDK_VERSION"/toolchains/llvm/prebuilt/*/bin/llvm-profdata* 2>/dev/null | head -n 1 || true)"
[ -n "$PROFDATA" ] || { echo "no llvm-profdata in $SDK/ndk/$NDK_VERSION"; exit 1; }

count() { if [ -n "$1" ]; then echo "$1" | wc -l; else echo 0; fi; }
EXT_DIR="/sdcard/Android/data/$PKG/files/pgo"
EXT_FILES=""
INT_FILES=""
if [ -n "$LOCAL" ]; then
    RAW="$(cd "$LOCAL" && pwd)"
else
    A=("$ADB" -s "$SERIAL")
    RAW="$ROOT/logs/pgo-raw/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$RAW"

    EXT_FILES="$("${A[@]}" shell ls "$EXT_DIR" 2>/dev/null | tr -d '\r' | grep '\.profraw$' || true)"
    for f in $EXT_FILES; do
        "${A[@]}" exec-out cat "$EXT_DIR/$f" > "$RAW/$f"
    done
    # run-as only works on a debuggable build, so this finds nothing for a release one
    if "${A[@]}" shell run-as "$PKG" true >/dev/null 2>&1; then
        INT_FILES="$("${A[@]}" shell run-as "$PKG" ls files/pgo 2>/dev/null | tr -d '\r' | grep '\.profraw$' || true)"
        for f in $INT_FILES; do
            "${A[@]}" exec-out run-as "$PKG" cat "files/pgo/$f" > "$RAW/$f"
        done
    fi
    [ -n "$EXT_FILES$INT_FILES" ] || { rmdir "$RAW"; echo "no .profraw files for $PKG in $EXT_DIR or its internal files/pgo (is the generate build installed and has it paused or stopped once?)"; exit 1; }
    echo "pulled $(count "$EXT_FILES") files from $EXT_DIR and $(count "$INT_FILES") through run-as to logs/pgo-raw/$(basename "$RAW")"
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
    for f in $EXT_FILES; do
        "${A[@]}" shell rm -f "$EXT_DIR/$f"
    done
    for f in $INT_FILES; do
        "${A[@]}" shell run-as "$PKG" rm -f "files/pgo/$f"
    done
    echo "cleared the $(( $(count "$EXT_FILES") + $(count "$INT_FILES") )) pulled .profraw files from the phone"
fi
