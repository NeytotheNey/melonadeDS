# Plan: native-counted polygon RAM and a bigger wide list (WideMelon)

## Why

With the wide view on, the geometry engine keeps polygons the native 256 view would clip away (they draw in the wings), and they share the game's polygon RAM. The 2048-polygon limit then hits while the native scene is still under it, and polygons submitted late in the frame are dropped.

- **NSMB title intro** (11-14, desktop and Fold inner screen at 426): 137–244 polygons dropped per frame from the lightning through the kidnap and again while Bowser Jr runs past, all of which would fit natively. The 3D flowers vanish in exactly those stretches. Before the lightning the wide view holds ~1370 polygons against ~710 native; the lightning pushes native to ~1900. Vertices peak ~4100 of 6144, so the polygon count is what binds. Logs: `logs/nsmb-flicker/`, `perf_logs/2026-11-14-nsmb-flicker/`.
- **Mario Kart DS, Shroom Ridge start** (11-08): 2,212 dropped, 94% would fit natively.

Chosen over the full shadow polygon list (`plan-widemelon.md` idea 1) because it needs no second geometry pass on the emulator thread, which is the OnePlus's limit.

## Approach

Most of it is already in `SubmitPolygon`: culling uses the pre-squeeze X (`TempVertexNativeX`), `NativeClipVertices` gives the native post-clip vertex count (0 when the native view clips the polygon away), `NativeZeroDot` redoes the zero-dot check natively after the RAM check, and `NativeNumPolygons`/`NativeNumVertices` already count the native view including strip reuse. Today `fitsnative` folds "native RAM full" into `nativeverts`; the change splits those and adds a second limit.

- **Native limits, 2048 polygons / 6144 vertices,** on the native counts. Only these set DISP3DCNT bit 13 and feed the count registers 0x04000604/606.
- **Wide list, 4096 / 12288 a bank:** what's actually stored and drawn.
- **Wing budget, 2048 / 6144,** reserved for polygons only the wide view keeps, so they can never take room from native ones.

Gated by a latched `WideListOn = (ViewWidth != 256) && GPU::WidePolys`. Off runs today's code unchanged, so the 256 view and the wide view with the switch off are exactly as now.

Submit rules (replacing GPU3D.cpp ~1260–1281):
- **Native polygon** (native clip count > 0): dropped iff the native counts are full, as on hardware, and then from the wide view too, so the middle 256 never shows a polygon the DS didn't draw. Sets bit 13.
- **Wing-only polygon:** kept only while the wing budget and the whole list have room *and* native RAM could still take a polygon of its size. That puts the wing cutoff at the same point in submit order as the middle's, so an object can't lose its middle half and keep its wing half.
- **Native polygon with a full wide list** (only if native polygons' wide vertex counts pass 12288, theoretical): dropped, counted as `nativefit`.
- A native-visible polygon that fails the native zero-dot check is charged to the wing budget. Polygon timing still comes from `nativeverts`.

## What grows

CPU arrays go static at the larger size (unused tails are never touched; growing them on demand would invalidate `Polygon::Vertices`, `RenderPolygonRAM` and `LastStripPolygon`).

| Where | Change | Memory |
|---|---|---|
| `GPU3D.h` | `PolygonRAM[4096*2]`, `VertexRAM[12288*2]`, `RenderPolygonRAM` 4096 (a `RenderList` typedef and Max constants); bank offsets in GPU3D.cpp 589–590, 3059–3060, HalfCamera.cpp 1200–1201 | +0.9 MB and +0.8 MB per GPU3D (main, plus HalfCamera's shadow when on) |
| `GPU3D_HalfCamera` | `RealList`, `GetRealRenderList` on the 4096 type | +16 KB |
| `GPU3D_Soft.h` | `PolygonList[4096]` | small |
| `GPU3D_OpenGL.h` | `PolygonList`, `TransAlphaClass` 4096; `VertexBuffer[24576*7]` (V + 3P; upstream's 10240 can already overflow in theory); `IndexBuffer[4096*40]`, `EdgeIndicesOffset = 4096*30`; u16 indices still fit | ~+0.65 MB CPU, ~+0.57 MB GL |
| `GPU3D_Compute` | `MaxPolygons` (4096 on wide targets with the switch, else 2048) as a shader define, next to `MaxWorkTiles`; `BinStride = MaxPolygons/32`, coarse words looped (shader ~944, ~1802); work-desc polygon index 11 → 12 bits (shader ~1135, 1137, 1226, 1230, 1274 and the CPU `RunCheck` decode ~2052, ~2102), `maxWorkTiles` capped to fit; `RenderPolygons`, `YSpanSetups` (limit stays 12288 at native), `YSpanIndices`, BinResult sized by `MaxPolygons`; clamp `numPolygons` on the frame the width changes | 16x wide: ~+12–20 MB GPU, ~+18 MB CPU; native targets unchanged |

## Savestates

- **GP3D keeps its exact format,** written in upstream's logical layout (2048 polygons / 6144 vertices a bank). Three range-for loops must become explicit logical loops or the format changes silently: `for (Vertex& vtx : VertexRAM)` (~491), `for (const Polygon* p : *renderList)` (~607), `RenderPolygonRAM.size()` (~616); the loop at ~496 needs the mapping too. Counts written are capped at 2048/6144, `NumOpaquePolygons` is recomputed over the saved polygons, unmappable pointers are written as the bank's vertex 0 (an old build sees one garbage frame, never a null).
- **New optional section `GP3W`,** written only while `WideListOn` (or when counts exceed the base), read only if present (like NDSCart's `NDCF`): a version, native counts and strip state, wing counts, `TempVertexNativeX`; and, only when a bank exceeds 2048/6144, the extra polygons and vertices, physical vertex indices, the render list and `LastStripPolygon`.
- Old states load as today; new states load in old builds and upstream (unknown sections are skipped); with the wide view off a saved state is byte-identical to today's.

## Switch and counters

- `bool GPU::WidePolys`, latched with `ViewWidth` at the swap, the idle-VBlank latch (~3076) and Reset; HalfCamera's `ResetShadow` copies it. Desktop `--set WidePolys=N` and a `--widedrops` flag; app `debug.melonade.widepolys`; swi-harness `--wide-polys 0|1`. Off = today's code exactly, count registers and bit 13 included.
- Count registers (GPU3D.cpp ~3416, 3419, 3456) return the native counts while `WideListOn`.
- HalfCamera::Replay (~1297) also rejects on a new `WideListOverflow` (not serialized), since bit 13 no longer covers holes in the wings.
- WIDEDROPS line: `drops` total, `native` (the DS would drop them too), `wing`, `nativefit` (caused only by the wide view; should be 0), `maxscene`, `peaknpoly`/`peaknvert` (of 2048/6144), `peakpoly`/`peakvert` (of 4096/12288), `peakwing`.

## Size

| File | Lines |
|---|---|
| GPU3D.cpp (submit ~60, savestate ~160, VBlank/Reset/latch/WIDEDROPS ~30, registers ~6) | ~260 |
| GPU3D.h | ~35 |
| GPU3D_Compute.h/.cpp + shaders | ~105 |
| swi-harness WideCheck + main | ~45 |
| desktop main.cpp (switch, `--widedrops`, `--at F:save=FILE`) | ~30 |
| GPU.h, HalfCamera, OpenGL.h, Soft.h, VertexId.h, app DebugProperties | ~35 |
| docs | ~30 |
| **Total** | **~550** |

## Verification

1. **Wide view off, desktop:** HASH3D on soft, GL and Compute over the T2 corpus (`logs/t2-step4b/run.sh`, `cmp.py`) matches the old build line for line; swi-harness `--hash3d` unchanged; a state saved at frame N is byte-identical.
2. **`--wide 426 --set WidePolys=0`:** HASH3D matches the old build.
3. **Wide check** with the switch on: bit 13 becomes a hard compare (drop `Disp3DOverflow` from the mask, WideCheck.cpp ~169), `nativeMisses` becomes a fail, and a new `WideCheckCountHash` of the 0x604/606 reads must match between the runs (a game with count reads > 0). `wide_check.sh` at 256, 342, 426, and with `--timing native`.
4. **NSMB intro at 426,** GL and Compute: frames ~1226–1612 and ~2051–2193 show `drops=0 nativefit=0`, and `logs/nsmb-flicker/missing.py` against the software 256 dump reports no missing flowers.
5. **Mario Kart Shroom Ridge start:** `nativefit=0`, `native` equal to the 256 run's drops, checked by eye.
6. **Savestate round trips:** old state into the new build (wide on and off); a wide state saved with a bank over 2048 (Shroom Ridge) reloads to the same HASH3D and WIDEDROPS for k frames as the straight run; that state loads in the old build without crashing.
7. **Phones,** A/B with `widepolys`: Fold inner screen (NSMB intro by screenrecord, Shroom Ridge; GL and Compute), Pixel the same, OnePlus emulator-thread time and fps (more kept polygons means more render work; Compute at 16x has the BinResult growth against the 512 MB storage-block limit).

## Risks

- The savestate mapping (the range-for traps, `NumOpaquePolygons` for old loaders).
- The Compute work-desc repack must agree in all five shader sites and the CPU checker; a forced debug tile size of 8 at high scale may no longer fit and fall back.
- Compute's 256-variant limit could truncate busy wide frames (watch COMPWHY, LogSlow).
- More polygons drawn means more GPU and emulator-thread work in exactly the scenes that dropped before.
- A big wide DSi scene adds ~1–2 MB to `GP3W`, against the rewind buffer's fixed size.

## Decisions (defaults unless the user says otherwise)

1. Wing-only polygons stop when native RAM is full (keeps the middle and wings consistent).
2. A reserved 2048/6144 wing budget rather than one shared 4096/12288 pool.
3. The count-register and bit-13 change sit behind `WidePolys`, for a clean A/B.
4. Default on only after the phone runs. Done: on since 11-14 (Fold, Pixel and OnePlus: NSMB intro drops to 0, flowers stay, no measurable cost).
5. `GP3W` only in the wide view; an old build shows one garbage frame after loading a big wide state.
6. Once built, `plan-widemelon.md` idea 1 is partly covered (the polygon limit and counts stop showing to the game; BoxTest stays visible), and the RetroAchievements caveat gets updated.
