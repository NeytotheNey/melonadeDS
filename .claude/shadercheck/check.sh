#!/usr/bin/env bash
# Validate the core's GL and compute shaders as GLSL ES 3.20 without a device.
# Usage: bash .claude/shadercheck/check.sh [path to melonDS-android-lib]
# Needs g++ and glslangValidator (Ubuntu: apt-get install glslang-tools).
set -e
CORE=${1:-$(dirname "$0")/../../melonDS-android-lib}
OUT=$(mktemp -d)
HERE=$(cd "$(dirname "$0")" && pwd)
g++ -std=c++17 -I"$CORE/src" "$HERE/gl_dump.cpp" -o "$OUT/gl_dump"
g++ -std=c++17 -I"$CORE/src" "$HERE/compute_dump.cpp" -o "$OUT/compute_dump"
cd "$OUT" && ./gl_dump && ./compute_dump
rc=0
for f in *.vert *.frag *.comp; do
  if ! glslangValidator "$f" >"$f.log" 2>&1; then echo "FAIL $f"; cat "$f.log"; rc=1; else echo "ok   $f"; fi
done
for p in RenderZ RenderW ClearBitmap Clear Compositor CompositorWide HiresCapture; do
  v=$p.vert; f=$p.frag; [ "$p" = Clear ] && v=ClearVS.vert && f=ClearFS.frag
  [ "$p" = ClearBitmap ] && v=ClearBitmapVS.vert && f=ClearBitmapFS.frag
  [ "$p" = Compositor ] && v=CompositorVS.vert && f=CompositorFS_Nearest.frag
  [ "$p" = CompositorWide ] && v=CompositorWideVS.vert && f=CompositorWideFS_Nearest.frag
  [ "$p" = HiresCapture ] && v=HiresCaptureVS.vert && f=HiresCaptureFS.frag
  if ! glslangValidator -l "$v" "$f" >"$p.link.log" 2>&1; then echo "FAIL link $p"; cat "$p.link.log"; rc=1; else echo "ok   link $p"; fi
done
echo "outputs in $OUT"
exit $rc
