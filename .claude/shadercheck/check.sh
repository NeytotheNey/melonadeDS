#!/usr/bin/env bash
# Validate the core's GL and compute shaders as GLSL ES 3.20 without a device.
# Usage: bash .claude/shadercheck/check.sh [path to melonDS-android-lib]
# Needs g++ and glslangValidator (Ubuntu: apt-get install glslang-tools).
set -e
CORE=${1:-$(dirname "$0")/../../melonDS-android-lib}
OUT=$(mktemp -d)
HERE=$(cd "$(dirname "$0")" && pwd)
# The compute shaders' assembly and define lists are cut from ComputeRenderer (GPU3D_Compute.cpp), so they can't drift
SRC="$CORE/src/GPU3D_Compute.cpp"
sed -n '/shaderSource += "#version/,/shaderSource += source;/p' "$SRC" > "$OUT/compute_assembly.inc"
grep -oE 'CompileShader\([^,]+, ComputeRendererShaders::[A-Za-z0-9_]+, \{[^}]*\}\)' "$SRC" | sed -E 's/^CompileShader\([^,]+, /COMPILE(/' > "$OUT/compute_shaders.inc"
[ -s "$OUT/compute_assembly.inc" ] && [ -s "$OUT/compute_shaders.inc" ] || { echo "no CompileShader assembly or calls found in $SRC"; exit 1; }
# Every call must be on one line in the expected shape; the definition (ComputeRenderer::CompileShader) isn't counted
calls=$(grep -oE '(^|[^:A-Za-z0-9_])CompileShader\(' "$SRC" | wc -l)
got=$(wc -l < "$OUT/compute_shaders.inc")
[ "$calls" -eq "$got" ] || { echo "found $calls CompileShader calls in $SRC but only extracted $got; check the grep in $0"; exit 1; }
g++ -std=c++17 -I"$CORE/src" -I"$OUT" "$HERE/dump.cpp" -o "$OUT/dump"
cd "$OUT" && ./dump
rc=0
for f in *.vert *.frag *.comp; do
  if ! glslangValidator "$f" >"$f.log" 2>&1; then echo "FAIL $f"; cat "$f.log"; rc=1; else echo "ok   $f"; fi
done
for p in RenderZ RenderW ClearBitmap Clear Compositor CompositorWide HiresCapture Filter2DKey Filter2DMmpx CompositorFilter2D CompositorWideFilter2D Shadow2D Shadow2DWide Shadow2DPlanes Shadow2DPlanesWide Shadow2DObj; do
  v=$p.vert; f=$p.frag; [ "$p" = Clear ] && v=ClearVS.vert && f=ClearFS.frag
  [ "$p" = ClearBitmap ] && v=ClearBitmapVS.vert && f=ClearBitmapFS.frag
  [ "$p" = Compositor ] && v=CompositorVS.vert && f=CompositorFS_Nearest.frag
  [ "$p" = CompositorWide ] && v=CompositorWideVS.vert && f=CompositorWideFS_Nearest.frag
  [ "$p" = HiresCapture ] && v=HiresCaptureVS.vert && f=HiresCaptureFS.frag
  [ "$p" = Filter2DKey ] && v=HiresCaptureVS.vert && f=Filter2DKeyFS.frag
  [ "$p" = Filter2DMmpx ] && v=HiresCaptureVS.vert && f=Filter2DMmpxFS.frag
  [ "$p" = CompositorFilter2D ] && v=CompositorVS.vert && f=CompositorFilter2DFS.frag
  [ "$p" = CompositorWideFilter2D ] && v=CompositorWideVS.vert && f=CompositorWideFilter2DFS.frag
  [ "$p" = Shadow2D ] && v=Shadow2DVS.vert && f=Shadow2DFS.frag
  [ "$p" = Shadow2DWide ] && v=Shadow2DVS.vert && f=Shadow2DWideFS.frag
  [ "$p" = Shadow2DPlanes ] && v=Shadow2DVS.vert && f=Shadow2DPlanesFS.frag
  [ "$p" = Shadow2DPlanesWide ] && v=Shadow2DVS.vert && f=Shadow2DPlanesWideFS.frag
  [ "$p" = Shadow2DObj ] && v=Shadow2DObjVS.vert && f=Shadow2DObjFS.frag
  if ! glslangValidator -l "$v" "$f" >"$p.link.log" 2>&1; then echo "FAIL link $p"; cat "$p.link.log"; rc=1; else echo "ok   link $p"; fi
done
echo "outputs in $OUT"
exit $rc
