# Native-counted polygon RAM and a bigger wide list (WideMelon)

Built and on by default since 11-14 (`GPU::WidePolys`, debug switch `widepolys`, 0 for A/Bs). Phones (Fold, Pixel, OnePlus): NSMB's intro drops to 0 polygons and its flowers stay, with no measurable cost (PERF-NOTES, `perf_logs/2026-11-14-widepolys/`). This doc is how it's built and what to watch when touching it.

## Why

With the wide view on, the geometry engine keeps polygons the native 256 view would clip away (they draw in the wings), and they share the game's polygon RAM. The 2048-polygon limit then hits while the native scene is still under it, and polygons submitted late in the frame are dropped.

- **NSMB title intro** (desktop and Fold inner screen at 426): 137–244 polygons dropped per frame from the lightning through the kidnap and again while Bowser Jr runs past, all of which would fit natively. The 3D flowers vanish in exactly those stretches. Vertices peak ~4100 of 6144, so the polygon count is what binds. Logs: `logs/nsmb-flicker/`, `perf_logs/2026-11-14-nsmb-flicker/`.
- **Mario Kart DS, Shroom Ridge start:** 2,212 dropped, 94% would fit natively.

Chosen over the full shadow polygon list (`plan-widemelon.md` idea 1) because it needs no second geometry pass on the emulator thread, which is the OnePlus's limit.

## How it works

`SubmitPolygon` already had the native view: culling uses the pre-squeeze X (`TempVertexNativeX`), `NativeClipVertices` gives the native post-clip vertex count (0 when the native view clips the polygon away), `NativeZeroDot` redoes the zero-dot check natively after the RAM check, and `NativeNumPolygons`/`NativeNumVertices` count the native view including strip reuse. On top of that there are two limits:

- **Native limits, 2048 polygons / 6144 vertices,** on the native counts. Only these set DISP3DCNT bit 13 and feed the count registers 0x04000604/606.
- **Wide list, 4096 / 12288 a bank:** what's actually stored and drawn, with a **wing budget of 2048 / 6144** reserved for polygons only the wide view keeps, so they can never take room from native ones.

Gated by `WideListOn = (ViewWidth != 256) && GPU::WidePolys`, latched with `ViewWidth` at the polygon RAM swap, the idle-VBlank latch and Reset (HalfCamera's `ResetShadow` copies it). Off runs the old code exactly, count registers and bit 13 included.

Submit rules:
- **Native polygon** (native clip count > 0): dropped iff the native counts are full, as on hardware, and then from the wide view too, so the middle 256 never shows a polygon the DS didn't draw. Sets bit 13.
- **Wing-only polygon:** kept only while the wing budget and the whole list have room *and* native RAM could still take a polygon of its size. That puts the wing cutoff at the same point in submit order as the middle's, so an object can't lose its middle half and keep its wing half.
- **Native polygon with a full wide list** (only if native polygons' wide vertex counts pass 12288, theoretical): dropped, counted as `nativefit`.
- A native-visible polygon that fails the native zero-dot check is charged to the wing budget. Polygon timing still comes from `nativeverts`.

HalfCamera::Replay also rejects on `WideListOverflow` (not serialized), since bit 13 no longer covers holes in the wings.

## What grew

CPU arrays are static at the larger size (unused tails are never touched; growing them on demand would invalidate `Polygon::Vertices`, `RenderPolygonRAM` and `LastStripPolygon`).

- `GPU3D.h`: polygon and vertex RAM at twice the DS's size per bank, `RenderPolygonRAM` 4096 (a `RenderList` typedef and Max constants); about +1.7 MB per GPU3D (main, plus HalfCamera's shadow when on). HalfCamera's `RealList` and `GPU3D_Soft`'s `PolygonList` follow.
- `GPU3D_OpenGL.h`: `PolygonList`, `TransAlphaClass` 4096; `VertexBuffer[24576*7]`; `IndexBuffer[4096*40]`, `EdgeIndicesOffset = 4096*30`; u16 indices still fit.
- `GPU3D_Compute`: `MaxPolygons` (4096 on wide targets with the switch, else 2048) is a shader define next to `MaxWorkTiles`; `BinStride = MaxPolygons/32` with the coarse words looped; the work-desc polygon index went from 11 to 12 bits, in five shader sites and the CPU `RunCheck` decode, with `maxWorkTiles` capped to fit; buffers sized by `MaxPolygons`, and `numPolygons` clamped on the frame the width changes. About +12–20 MB GPU and +18 MB CPU at 16x wide; native targets unchanged.

## Savestates

- **GP3D keeps its exact format,** written in upstream's logical layout (2048 polygons / 6144 vertices a bank). Watch for range-for loops over `VertexRAM`, the render list or `RenderPolygonRAM.size()` in the savestate code: they'd change the format silently. Counts written are capped at 2048/6144, `NumOpaquePolygons` is recomputed over the saved polygons, and unmappable pointers are written as the bank's vertex 0 (an old build sees one garbage frame, never a null).
- **Optional section `GP3W`** (`DoWideSavestate`), written only while `WideListOn` (or when counts exceed the base), read only if present (like NDSCart's `NDCF`): a version, native counts and strip state, wing counts, `TempVertexNativeX`; and, only when a bank exceeds 2048/6144, the extra polygons and vertices, physical vertex indices, the render list and `LastStripPolygon`.
- Old states load as before; new states load in old builds and upstream (unknown sections are skipped); with the wide view off a saved state is byte-identical to one from before the change.

## Debugging

- Desktop: `--set WidePolys=N` and `--widedrops`; swi-harness: `--wide-check` with `--wide-polys 0|1` (with 1, bit 13 is a hard compare and the count-register reads must match between the runs).
- WIDEDROPS line (`widedrops` 1): `drops` total, `native` (the DS would drop them too), `wing`, `nativefit` (caused only by the wide view; should be 0), `maxscene`, `peaknpoly`/`peaknvert` (of 2048/6144), `peakpoly`/`peakvert` (of 4096/12288), `peakwing`.
- **Regression check** (`logs/` is git-ignored): with the wide view off, HASH3D on soft, GL and Compute over the T2 corpus (`logs/t2-step4b/run.sh`, `cmp.py`) must match the old build, and a state saved at frame N must be byte-identical. NSMB's intro at 426 on GL and Compute: frames ~1226–1612 and ~2051–2193 show `drops=0 nativefit=0`, and `logs/nsmb-flicker/missing.py` against the software 256 dump finds no missing flowers. A wide state with a bank over 2048 (Shroom Ridge) reloads to the same HASH3D and WIDEDROPS, and loads in an old build.

## Risks still worth knowing

- The Compute work-desc packing must agree in all five shader sites and the CPU checker; a forced debug tile size of 8 at high scale may no longer fit and fall back.
- Compute's 256-variant limit could truncate busy wide frames (watch COMPWHY, LogSlow).
- More polygons drawn means more GPU and emulator-thread work in exactly the scenes that dropped before.
- A big wide DSi scene adds ~1–2 MB to `GP3W`, against the rewind buffer's fixed size.
