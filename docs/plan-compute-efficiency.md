# Compute renderer efficiency

The goal is less GPU time, less graphics memory and less battery from the Compute 3D renderer, mainly for older Adreno phones. The test case is the OnePlus 7 Pro (Adreno 640) at 4x. 60 FPS there is the aim but not a hard target: any gain counts.

Phase 1 (11-09 to 11-11, branch `compute-efficiency`) is merged into `material3` / `melonade`. Phase 2, a visibility pass, is planned below. Stage 0 (instruments) is done on `feature/compute-vis-stage0` and its results are under "Stage 0 results (11-18)"; stage 1a is in progress on `feature/compute-vis-1a`. The short version of the numbers is in `PERF-NOTES.md` under "Compute efficiency on the OnePlus 7 Pro"; raw logs are in `perf_logs/2026-11-09-compute-timers/` (`summary*.txt`, one folder per run under `runs/`), `perf_logs/2026-11-11-rooms/` and `perf_logs/2026-11-11-warmup/`.

## Phase 1: what was built

**Where it started (11-09):** the OnePlus at Compute 4x was GPU-bound. White 2's title ran at about 31 FPS with the GPU at its top clock step, about 18 Mclk a frame against about 3.4 for the OpenGL renderer. NSMB's map presented 2.1 FPS while emulation ran at 53. Buffers and textures took about 296 MiB at 4x (the Fold showed about 696 MB graphics PSS against about 350 for OpenGL), mostly tile work buffers sized for 16 polygon layers on every tile and a 9-texture frame queue.

- **Render pacing, on by default** (`GPU::ComputePace` = 2). A fence follows each 3D render; when two are pending the render is skipped (the previous 3D image stays), or the emulator waits when it can't skip (capture frames, an active Smooth 30 FPS in-between). A skipped frame that a capture then needs is redrawn at line 0. NSMB's map at 4x had the driver queue 0.6 s deep (render latency 610 ms); with pacing it went from 2.1 to 60.0 presented FPS, Smooth 30 FPS on or off. Wait-only (`computepace 1`) gives 51.6. Code: `GPU3D_Compute.cpp/.h` (`UpdatePace`, `WaitForPace`, `CanSkipForPace`, `RedrawSkippedFrame`, `ResetPace`, `PaceStale`), `GPU.cpp` (the line-0 redraw and the POWCNT redraw), `GPU3D_HalfCamera.cpp` (`CanSkipRender`).
- **`GPU::ComputeOpt`, default 223.** A sum of bits, each one an output-identical saving:
  - 1: skip the software divide for linear spans in Z-buffer programs.
  - 2: load only the span and polygon fields the coverage test needs.
  - 4: skip second-layer stores the final pass doesn't read unless AA or fog is on.
  - 8: a dead branch and a folded constant.
  - 16: a VRAM write only defeats the identical-frame skip when a cached texture really changed. White 2's overworld with Smooth off: 52 → 30 renders a second (22 a second were forced by dirty VRAM alone).
  - 32: **off by default.** Skip a render when a swap repeats the previous scene exactly (`GPU3D::MatchRenderScene`). It halves renders again but stutters (see Open).
  - 64: pacing may skip with Smooth 30 FPS on when HalfCamera has nothing to interpolate. NSMB map, Smooth on: 53.2 → 60.0 FPS.
  - 128: attr's 9 bits (edge flags, coverage) ride in the color word's free bits (6–7, 14–15, 22–23, 29–31), so there are two tile buffers, not three. Rasterise masks color to `0x1F3F3F3F` before packing; DepthBlend unpacks only in the opaque branch. OnePlus title 32.23 → 33.12 FPS, NSMB map render median 7.26 → 5.87 ms; the Fold saves 27–37 MB.
  - 256: **off by default.** Color and depth interleaved in one buffer. Needs 128. No faster than two buffers and no smaller.
  - Bits 1–8 together: title 31.40 → 32.15 FPS, NSMB map render median 8.93 → 7.55 ms.
- **Right-sized work buffers** (`GPU::ComputeGrow`, default on; `computegrow 0` gives the old full sizes). Tile memory starts at 6 of 16 layers and the X spans (plus the setup-index texture buffer) at a 16th. `GrowWorkBuffers` runs after polygon setup and before any upload or dispatch, fed `totalTileBound` and the exact setup-index count; it grows to need + 25%, rounded to a step, capped at full size, grow-only until `ResizeTargets`. No shader change: the GPU never bins outside the CPU's bound. Fold, 4x, graphics settled, grow on / off: title 388 / 483 MB, overworld 428 / 510, NSMB map 312 / 466, FPS 60 everywhere. The OnePlus's meminfo doesn't show these buffers at all.
- **Checked grows.** A failed `glBufferData` is detected (`WorkBufferData`: `glGetError` plus `GL_BUFFER_SIZE`), retried smaller, and a render with no room is skipped with the previous image kept.
- **Remembered rooms** (`computerooms`, `GPU3D_ComputeRooms.cpp`): each game's grown sizes are stored per scale and layout in `cacheDir/compute_rooms.bin` and used from the next start.
- **Memory warm-up** (`computewarm`, all builds): the first frame after `ResizeTargets` zeroes the work buffers with a small compute program, so the GPU's page commit (about 120–130 ms for 508 MB at 8x on the Fold) lands on the black screen. White 2's title hitch at 12.7 s, 8x: 95 → 49–67 ms. Trade-off: the whole room is resident, headroom included.
- **Program binary cache** (`shadercache`): the 33 compiled programs are saved in `cacheDir/compute_programs.bin`, keyed on driver identity and full source. Fold: a cold 4x compile was 400 ms, a cache hit 4.5 ms. Android's own driver cache often hits already. About 160 KB a config.
- **Memory outside the work buffers.** The compositor's own output textures are only made when no frontend texture is set (`GLCompositor::PrepareOutput`), and the app's frame queue reuses finished frames first and frees ones idle for 1800 frames (`FrameQueue.cpp/.h`). It settles at 4–5 textures, not 9.
- **Bug fixes with no switch:** rendering off (or geometry off, reset, load) draws the empty scene once and then skips, in all three renderers (`GPU3D::VBlank`, `SetEnabled`, `RenderedSinceVBlank`); `Texcache::CheckInvalid` wraps its dirty-word index correctly; the compressed-texture palette hash covers every byte a texel reads; `ComputeRenderer::Reset` forces a render.

**Where the cost is:** Rasterise and DepthBlend are 70–77% of compute time in every scene, about equal (`computesync` shares). Both run once per pixel of every work tile; DepthBlend is nearly all memory access. Peak use at 4x: title 31% of the tile work buffer, 14% of the span buffer, on both phones.

**Ruled out, don't retry:** 16 px tiles at 4x (title 31.45 → 28.17: binning halves but Rasterise grows 47%, since every work tile costs a full tile of threads), tile-grouped DepthBlend, GL timer queries for compute passes on Adreno.

### Results at defaults

OnePlus, Compute 4x, Smooth 30 FPS on, plugged in:

| Scene | Before | After |
|---|---|---|
| NSMB map, presented FPS | 2.1 | 60.0 |
| White 2 title, FPS | 31.4 | 32.3 |
| White 2 overworld, FPS | 59.9 | 60.0 |

White 2's title is dual-screen 3D: a new swap every frame with a capture, so nothing can be skipped and it's plainly GPU-bound. 2x holds 60 there. It won't reach 60 at 4x on this phone without a much bigger change (Phase 2).

The Fold (cover screen, plugged in) holds 60.00 FPS at 4x with 0 frames over 16.7 ms in all three scenes, and pacing never skips there. Unlike the OnePlus, it commits the work buffers, so the memory work shows there. The user saw White 2's title hold a stable 60 at Compute 8x (11-10); deband drops it.

HASH2D matched on every overlapping frame for every change, on both phones, and between the phones.

### Debug switches

All `debug.melonade.<name>`, read at game start; the full text is in `DebugProperties.h`. `computepace`, `computeopt` (0 is the old shaders and skips; 95 the old three tile buffers; 479 one tile buffer; 255 turns the repeated-scene skip on), `computegrow`, `computerooms`, `computewarm`, `shadercache`, `computetile N` (force 8, 16 or 32 px tiles), and the logging ones: `computewhy` (`COMPWHY`, why each render wasn't skipped), `computesync` (`COMPSYNC`), `computepeaks` (`COMPPEAK`), `computetimers` (`COMPTIME`). Always logged in debug builds: `COMPTILES` (with `maxblock`, the GPU's storage block limit: 512 MB on the OnePlus, 2 GB on the Fold), `FRAMEQ`, `COMPGROW`, `COMPSLOW` (phase timings of any Compute frame over 30 ms), `COMPGLERR`, and `COMPROOMS`, `COMPWARM` and `COMPSHADER` while `computerooms`, `computewarm` and `shadercache` are on (the default).

- `computetimers` reads zero for compute passes on Adreno (640 and the Fold); only the compositor buckets mean anything.
- `computesync` inflates everything about 2.5x, so read shares only. `computepeaks` syncs every frame; ignore FPS. `computewhy` and `computetimers` cost nothing measurable.
- `computewarm` debug builds glFinish around the warm-up to time it, so debug grows cost a bit more than release; set it to 0 for A/Bs against logs from before 11-11.

### Testing notes

- **Scenes:** White 2's title by cold boot with no input (frame-deterministic, so HASH2D compares frame by frame, but it only sees 3D at 1x through the capture). White 2's overworld and NSMB's world map by continuing the save with taps. No savestate was ever loaded; keep it that way.
- **Quick recipe:** one 30 s window per config, fresh app start, no perfetto.
- **Scripts to start from** (in `perf_logs/2026-11-09-compute-timers/scripts/`): `ct_run_v6.sh` and `ct_chain_v5.sh` (cold start, set renderer, scale and props, reach a scene, pull logs), `ctsum6.py` and `ctsumf.py` (medians), `hashalign5.py` (exact-frame HASH2D compare of cold-boot title runs, handles midnight), `shotdiff.py` (masked screenshot compare), `slowframes.py` and `waitcorr.py` (slow frames against pacing waits). The step 6 versions are in `runs/pack_scripts/` (OnePlus) and `fold-pack/scripts/` (Fold).
- **HASH2D at 60 FPS loses lines:** the core's log limit (50 lines a second per call site) drops about 15% of HASH2D lines when a game runs at 60. A full frame-by-frame check on a fast phone needs that limit raised.
- **Desktop build** (`desktop/`) runs Compute and OpenGL on the PC's GPU; it's a logic check on Nvidia, not a phone result. The PC harness builds without the GL renderer, so it can't check Compute.

### Things to distrust

- Packing masks color to 6/6/6/5 bits. `ClipSegment` truncates `factor_den` to 32 bits, so extreme clip coordinates could push a vertex color out of range; the old code let the stray bits through to the final pass, the new one cuts them off. Both are wrong there, differently. Nobody has found a game that does it.
- A savestate stores `IsShadowMask` and `Attr` separately. If one ever disagreed, packing would render it differently from before.
- `computeopt 95` launches with two tile buffers and rebuilds for three at the first frame, so it costs a second shader compile.
- `TileLayers`' initial value (2) is hard-coded to match `OptMask`'s default.
- Lazy drivers (Adreno, Mali) may never report a failed grow, and a grow still briefly holds old and new storage while earlier renders are in flight. The one-buffer fallback (buffer over the GPU's block limit) never triggered on either phone, so that path is untested.
- The POWCNT redraw runs GL inside an IO write handler, on the assumption a context is always current there.
- Every Compute render does a `glFenceSync` and a `glFlush`, in all builds.
- With `computeopt` bit 4 on, layer 2 of the final tile buffer is stale when AA and fog are off. Nothing reads it today.
- The size `static_assert` guarding `MatchRenderScene`'s field list wouldn't catch a `bool` added into padding.
- Release builds make a handful of empty virtual calls a frame for the stats hooks.
- The desktop Qt build of the lazy compositor outputs has never been built.

### Open

- ~~**The OnePlus:** a 4x run of the current build with graphics PSS.~~ Done 11-18: 156 MB at the 4x title, but meminfo there doesn't count the compute buffers at all (155–164 MB at every scale), so it can't be measured on this phone.
- **The Fold's packing confirm run** of the default-on build (low risk; the OnePlus's passed). Also not done there: a run unplugged, and the main screen (all Phase 1 runs were on the cover screen).
- **The user's eyes on skipped frames.** Pacing leaves the previous 3D image up for about 7 frames a second on NSMB's map (up to 21 in Mario Kart's demo), with 2D still updating. Nobody has judged whether it looks juddery; `screenrecord` is blocked over adb on the OnePlus. If it does, `computepace 1` is the fallback.
- **Why `computeopt` bit 32 stutters.** White 2's overworld, Smooth on: 60 → 30 renders a second and the GPU ends about 9 °C cooler, but FPS drops 59.97 → 58.36 with about 100 long frames per 30 s. The long frames are pacing waits (0.91 correlation with `wait_ms`); with `computepace 0` they become dropped presents instead (55.8 presented). So the GPU falls behind when half the renders are skipped. The guess is its clock drops in the gaps. A perfetto `gpu.counters` trace of `computeopt 127` against 95 would say. It's the remaining battery win for 30 FPS games with Smooth 30 FPS on.
- **The rest of the 12.7 s title hitch** (about 40–60 ms at 8x): 26 ms is texture decode and upload on the first textured frame (`texget` in `COMPSLOW`); the rest is outside RenderFrame, probably the emulator waiting on the GPU in the capture readback. Fixing the texture part means decoding off the frame path. Optional; it's one frame at the title.
- **Smaller gaps:** a scene with anti-aliasing or fog for bit 4 (Mario Kart's demo isn't frame-deterministic); a long plugged-in run; a real battery number (the 240 mA from bit 16 was one plugged-in pass, a direction only).
- **16x:** the app's scale setting stops at 8x, and at 16x the buffers run into GPU block and `int` size limits (`TileLayersFor`, `maxblock` in `COMPTILES`). Phase 2's stage 5 removes the reason for the cap.

## Phase 2: a visibility pass (planned 11-15)

### Why

Fold inner, White 2 title, deband off, 256 view, Mcyc per frame (PERF-NOTES "Progress vs WatermelonDS"): Compute 8x 7.68, our OpenGL 8x 2.43, WatermelonDS Vulkan 4.52. Graphics memory 935 MB vs GL's 474. At the 426 view Compute 8x is 15.9 Mcyc and throttles (thermal 2, 54–56 fps). Compute is capped at 8x because tile memory reaches ~805 MB per buffer at 16x. OpenGL is now ahead of Vulkan; Compute is still 1.7x Vulkan and 3x GL.

The cost is the design, not tuning. Rasterise shades every pixel of every (polygon, tile) work tile, texture sample included, before any depth test; DepthBlend then walks every polygon binned to the tile, per pixel, reading the stored colors back. On the Fold at 8x the title has 3.95 visits per pixel, 27% failing depth and only ~39% contributing (overworld 4.26, 38% failing). Rasterise and DepthBlend are 70–77% of compute time, about equal, and DepthBlend is nearly all memory access. Rough estimate: ~50 B per screen pixel through L2/DRAM, ~160 MB a frame at 8x, since compute passes don't get the tiled GPU's on-chip memory. That probably explains part of Compute's 2.3x battery cost.

### The idea

DepthBlend's whole state machine (depth tests, AA push-down, the translucent ID rule, stencil) never looks at a fragment's color, only its alpha class (opaque, translucent, discarded) and the polygon's attributes. So the opaque part can track polygon indices instead of colors and shade only the winners, and stay exact.

- **Visibility pass (V):** per tile, walk the opaque polygons in DS order, compute coverage, edge attr and depth with Rasterise's own functions, run DepthBlend's opaque rules on indices, and keep two layers of {polygon, depth, attr} per pixel. No texturing, except texture alpha for cut-out polygons.
- **Shade the winners:** only the winning polygon of each layer gets its color computed. Shading drops from ~2.6 colored visits per pixel to ~1–1.2 plus translucency.
- **Translucent polygons** keep today's in-order path, starting from the state V leaves, until the later stages fold them in.

What it can't avoid: in W-buffer frames (the title) depth needs the per-pixel divide, so V still does one divide per covered fragment, and the per-pixel walk stays (~4 visits a pixel at the title). The saving is the texture sample, interpolation and color traffic of every non-winning visit.

### Exactness

The bar is today's Compute, bit for bit, not the software renderer. Compute already differs from Soft in places and the new code must copy those, not fix them in the same step: no front-facing `<=` rule over back-facing opaque pixels, the AA push-down not gated by DISP3DCNT bit 4, the second-layer fallback testing `attr & 0x3` (Soft `0xF`), DISP3DCNT bit 3 (alpha blend enable) ignored, the stencil reset per pixel per fragment, and W vs Z picked per frame from polygon 0.

| Rule | How it stays exact |
|---|---|
| Opaque before translucent | The CPU finds the opaque prefix: the longest run from polygon 0 that isn't translucent or mode 3 (shadow and mask polygons with alpha 31 are filed as opaque and can sit among them, so the prefix ends at the first one). Everything after it goes through today's code. |
| Alpha in the prefix | With polygon alpha 31 and texture formats other than 1/6, a fragment is opaque or discarded: discarded only by a cut-out texel or AlphaRef = 31 (a frame flag). A must-be-0 counter checks for alpha 1–30 in the prefix. |
| Depth test, equal test | V runs DepthBlend's comparisons in the same order, so equal-test polygons (±0x200 Z, ±0xFF W) see the depth as it was at their turn. Atomics can't do this (no equal test, no push-down, doesn't fit 32 bits), so V is a sequential walk per pixel, like DepthBlend. |
| Two layers (AA, edge, fog) | Always kept (Compute doesn't gate it by AA): two layers of color, depth and attr, never more. FinalPass is unchanged; V always writes all six Result words. |
| Translucent ID rule, fog AND, depth write, blending | Unchanged code, run in order from V's opaque state. |
| Shadow masks and shadows | All in the translucent group, unchanged. Trap: `prevIsShadowMask` is set by any covered non-mask fragment before its depth test, so skipping a failing fragment must keep that side effect (stage 3's sentinel). Masks are never skipped. |
| Wireframe, lines, clear, bitmap clear, color mask | Coverage shared from Rasterise as one function; clear init copied from DepthBlend with CLEAR and EMPTY index sentinels; winners write `color & TileColorMask` under PackAttr. |
| What the compositor and capture read | Only FinalPass's RGBA8 image and the 1x capture image, unchanged. |

**The check:** today's HASH3D only hashes Compute's 1x capture image (1 pixel in 64 at 8x), so stage 0 adds a full-resolution hash, `HASH3DF`. Every stage: a desktop sweep with `--set ComputeVis=N` against 0 (full-res screenshots and hashes, at 1x, 4x, 8x and 12x so 8, 16 and 32 px tiles are covered, over the PC harness's 21 games), then the Fold, OnePlus and Pixel.

### Stages

All behind a new switch `debug.melonade.computevis` (bits, like `computeopt`), default 0 until all three phones pass.

| Stage | Change | Lines | Expected win |
|---|---|---|---|
| **0. Instruments** | (a) `hash3dfull`: full-res hash pass, logged as `HASH3DF`. (b) `COMPVIS` census once a second: prefix length and share of tile work, cut-out polygons, counts of equal-test, shadow, mask, wireframe, bitmap clear, AlphaRef. (c) COMPDEPTH split by group: prefix visits, colored, failed, winners; translucent fragments behind the final opaque depth (stage 3's yield); the alpha 1–30 counter. (d) `computeprobe` bits (inexact, cost split only): Rasterise without the texture sample, without color/uv interpolation, without the divide. | core ~215, app ~15 | none; decides the order below |
| **1a. Opaque visibility, no textures** | The prefix also ends at the first cut-out polygon. New `OpaqueVis` pass per tile writes Result depth/attr for both layers and a `Winner` buffer (two u16 per pixel). Rasterise gets a shade mode for prefix polygons: non-winners exit before any work. DepthBlend becomes Resolve: starts from Result, skips prefix bits, and doesn't run for tiles with no translucent polygons. Two new programs (Z and W), so 33 → 35; update the hard-coded 33 in `NeedsShaderCompile` and the MetaUniform `static_assert`. | shaders ~220, Compute.cpp/.h ~200 | Removes DepthBlend's prefix visits and prefix shading except winners. Guess −5 to −25% of GPU cycles; Z frames gain more than W frames (the divide). Less bandwidth, no memory change yet. |
| **1b. Cut-outs** | An alpha-only Rasterise mode for prefix cut-out variants, run before V with the real texture and sampler (exact by construction), sets one bit per passing texel in `CutoutBits` (1/32 of a tile layer). The prefix then runs to the first translucent or mode-3 polygon. | shaders ~40, Compute.cpp ~60 | Extends 1a to scenes full of color-0 cut-outs (most of what's left in Pokémon overworlds per R1). |
| **1c. Span depth-bound skip** | In V, skip a fragment's depth (and the W divide) when its row's nearest depth can't beat the pixel's current layers and the polygon isn't equal-test. Exact: in the prefix a failing fragment has no effect. | shaders ~15 | Up to the 27–38% of visits that fail depth. |
| **2. Translucent-only tile memory** | Binning keeps opaque and translucent work in separate regions; only translucent work gets tile memory, sized by the CPU's translucent bound. Room keys get a layout field. | shaders ~50, Compute.cpp ~85 | Memory falls by the prefix share of tile memory; estimate 100–200 MB at the 8x title, from census (b). Less DRAM traffic. |
| **3. Translucent early reject** | Translucent Rasterise reads the final opaque depth bound from V. A fragment behind it (not equal-test, before the first equal-test translucent polygon, not a shadow mask) writes a sentinel instead of shading; Resolve treats it as a failed fragment, side effects included. | shaders ~40, Compute.cpp ~20 | Skips shading translucent fragments hidden behind opaque ones (census (c)). |
| **4. Textures without bindings** | Decoded textures also go into one SSBO (not a texture buffer: Mali can wrap those at 65,536 texels), fetched with integer wrap/mirror/clamp. V reads cut-out alpha from it (drops 1b's pre-pass); a per-pixel Shade pass replaces opaque work tiles, so binning only makes work for translucent polygons. **Gate first:** today's Rasterise sampling through the SSBO instead of `texture()` must be bit-identical on the desktop and all three phones. | Texcache.h ~150, Compute.cpp ~120, shaders ~120 | Removes the 256-thread launches per opaque work tile, the cut-out pre-pass and most sort work. |
| **5. Fused translucent** | Resolve shades translucent fragments inline (all threads of a tile walk the same polygon, so blend-mode branches are uniform). No Rasterise, sort or tile memory left; binning only builds masks. | shaders ~150, Compute.cpp ~120 | Memory becomes per pixel (Result 24 B + Winner 4 B + bins + spans), ~350 MB plus spans at 16x, under the OnePlus's 512 MB per-buffer limit. Removes the reason for the 8x cap (raising it in the app is a separate change). |

Phone steps: stage 0 is ~3 sessions (Fold 8x title, overworld, a fog area and NSMB's map with probes under `computesync`; OnePlus 4x title; Pixel title). Stages 1a–1c ~3 sessions (Fold 4 scenes × 2 arms, OnePlus title, Pixel title and town). Stage 2 ~2 (Fold memory: title, overworld, map; OnePlus). Stage 3 ~1. Stages 4 and 5 ~2 each, plus 16x memory runs for 5.

### Scenes

| Scene | Covers |
|---|---|
| White 2 title (cold boot, deterministic) | W-buffer, AA, blending, clear alpha 0, capture, 3D on both screens |
| White 2 overworld | Z-buffer, edge marking every frame |
| A White 2 fog area or Dark Dawn's fog title | fog + edge, fog's second-layer reads |
| NSMB's map | many polygons and variants, pacing |
| Mario Kart DS (desktop results-replay state) | shadow volumes (expected; confirm with the census), translucency, equal-depth decals |
| Phantom Hourglass | toon + edge (expected; confirm) |
| Platinum / SoulSilver overworld, Contra 4 intro | color-0 cut-outs, the Z path |
| White 2 title at the 426 view | the 4096-polygon wide list |

Wireframe, bitmap clear, AlphaRef and opaque A5I3 texels: find them with census (b) on the desktop sweep; list any no game uses as uncovered.

### GPU constraints

- No atomics in the visibility state; the only new ones are a translucent counter and the cut-out `atomicOr`, on flat `uint` arrays. Never a component of a `uvec4` element (the Mali bug fixed in `4b58305d`). 64-bit atomics aren't available.
- No `barrier()` inside the polygon loop (GLSL ES forbids it in control flow), so V doesn't stage span rows in shared memory; a tile row shares one `XSpanSetup`, so the cache handles it.
- No GPU-written indirect dispatches (Adreno stalls the CPU on them): fixed sizes or the CPU's bound with the existing leftover loop.
- Register pressure: V runs 256 threads at 8x and 1024 above; stage 5 is an uber-shader. Watch compile failures and spills on the Adreno 640 and Mali (Mali's offline compiler reports both on the PC).
- Timers: compute timer queries read zero on the 640 and the Fold, so use `computesync` shares and Fold `gpu_mcycles_per_frame`; on the Pixel, the Mali vendor counters.
- Risky: stage 4's integer wrap/mirror/clamp matching `texture()` for nearest-filtered integer textures. It holds by spec but is only proven per driver by the gate A/B; everything before stage 4 keeps `texture()`.

### Cheaper wins, ranked

1. ~~**`computetile 8` at 8x on the Fold.**~~ Ruled out 11-18: slower on the OnePlus (11.3 vs 11.5 fps) and the Pixel (+3% cycles) at the 8x title, and roughly double the tile memory. The Fold wasn't tried. No code, one phone step. 8x uses 16 px tiles and wastes about a third of its 256 threads (3.95 visits vs ~2.6 colored); fits the work-tile limits at the 256 view.
2. **Stage 0's probes and census**, which decide whether 1a pays in W frames and whether 1a or 1b comes first. If most opaque work comes before the first cut-out polygon, 1a alone is most of the win at ~60% of the lines.
3. **1c**, ~15 lines on top of V.
4. **Stage 3**, ~60 lines, needs V.
5. **Whole-tile occlusion at bin time.** Shortens every later walk, but the exact conditions are tricky (no edge pixels of the occluder in the tile, no equal-test polygon between, no cut-outs). Only if the census shows long walks behind full-tile occluders.

### From the GPU discovery plan (moved 11-15)

`plan-gpu-discovery.md` had its own Compute list from its 11-11 code read and Phase 0. It's tracked here now:

| # | Experiment | Where it went |
|---|---|---|
| C1 | DepthBlend reads depth first and skips the color read when the depth test fails | Not worth it (below): Phase 0 found it about neutral, since it saves color reads for failing pixels but adds depth reads for pixels with no color. |
| C2 | Per-tile Hi-Z: min/max opaque depth per tile from a cheap pass, so Rasterise skips pixels behind it | Covered by V: in the opaque prefix only winners get shaded, so a Hi-Z gains nothing there. What's left of it is 1c (row depth bound inside V) and stage 3 (translucent fragments behind V's opaque depth). |
| C3 | Per-tile occlusion from binning: skip work tiles fully covered by a later opaque polygon | Cheaper win 5 (whole-tile occlusion at bin time), only if the census shows long walks behind full-tile occluders. |

Phase 0's Compute numbers (Fold 8x: 27–38% of DepthBlend visits fail depth, ~39% contribute) are the ones under "Why". COMPTIME reads zero on the Fold too, so pass shares need `computesync` (see "GPU constraints").

Not worth it: splitting texture sampling out of Rasterise (Compute already samples a pre-decoded cache in one fetch); reading depth before color (about neutral in Phase 0); narrowing Result (color, attr and depth need 70 bits a layer).

### Measure first, then decide

1. Rasterise's cost split (divide, interpolation, texture, stores) in a W scene (title) and a Z scene (overworld). If the divide dominates, V costs nearly what Rasterise does in W frames and 1a pays mainly in Z frames.
2. The census: prefix share of visits and work tiles, cut-out share, winners per pixel, translucent fragments behind the opaque bound, and the alpha 1–30 counter (must be 0).
3. `computetile 8` at 8x.
4. On Mali: `GL_MAX_TEXTURE_BUFFER_SIZE`, and whether a DepthBlend-sized shader spills.
5. How much of the 935 MB is tile memory at the 8x title (`COMPROOMS`, `COMPGROW` lines), before stage 2.

### Stage 0 results (11-18)

Numbers are in `PERF-NOTES.md` under "Compute baseline before Phase 2" and "Compute Phase 2 stage 0". Switches (`hash3dfull`, `computevisstats`, `computedepth`'s new fields, `computeprobe`) are in `docs/debug-switches.md`. The Fold wasn't connected, so its scenes (fog area, NSMB's map, the 8x title under `computesync`) are still to do.

- **1a only pays at the title.** The title has no cut-outs, so the 1a prefix is 64% of its tile work. White 2's overworld puts a cut-out at polygon 5–9 every frame, leaving 1a 0.3%; on the desktop Platinum keeps 16% and Mario Kart 0%. So 1b is what makes the pass worth it in gameplay, and 1a and 1b should ship together.
- **Winners are about 1 per pixel** against 2.7 prefix visits at the title and up to 2.9 in the overworld. That's the shading V removes.
- **The divide matters on Mali:** removing it cuts 19% of cycles at the title and 22% in the overworld (Pixel 8x). V still divides per covered fragment, so **1c** (skip depth and the divide for fragments that can't win) moves up: 27% of prefix visits fail depth at the title, 43% in the overworld.
- **On Adreno at the 4x title, Rasterise's math is small** (texture −2.5%, interpolation +3.6%, divide +5.2% fps), so there the gain has to come from fewer visits and less memory traffic, which is V and Resolve skipping the prefix.
- **Stage 3 looks bigger than ranked:** 27–28% of translucent fragments are behind the opaque bound at the title, 66–71% in the overworld (with the full, 1b prefix).
- The alpha 1–30 counter was 0 everywhere. No equal-test, shadow, mask, wireframe, bitmap clear or AlphaRef frames in White 2; Mario Kart has about 9 shadows and 9 masks a frame, all after the prefix.
- **Order now:** 1a, then 1b, then 1c, then 3, then 2.

**Open:** whether any game puts alpha-31 shadow polygons early in the opaque group (it would shorten the prefix); whether V's per-visit cost on Adreno comes in under DepthBlend's memory-bound visit. **Other risks:** particle-heavy translucent scenes gain little before stage 5; `prevIsShadowMask` is easy to break (stage 3, Mario Kart's shadows); 3–8 more programs and the hard-coded counts and keys; the grow-and-skip safety valve stays inexact when it triggers, as today. Code map: `GPU3D_Compute_shaders.h` (Rasterise ~:1240–1561, DepthBlend ~:1563–1837, BinCombined ~:954–1149, FinalPass ~:1839), `GPU3D_Compute.cpp` (RenderFrame ~:2244–3100, ResizeTargets/BuffersFit ~:766–1025, QueueHash3D ~:1731), `GPU3D_Soft.cpp` and `GPU3D.cpp` for the rules and the opaque-first sort.

## Rules

- HASH2D and exact-frame matches must stay identical after every step; hi-res capture, WideMelon, halfcam and dither must keep working.
- GL changes need the phone: a Fold run and a OnePlus run before a step counts as done.
- Perf runs follow `PERF-NOTES.md`'s recipe (fresh start per run, interleaved A/B; back up the `.sav` around any state load).
- If the timers show the cost is spread evenly and 4x stays well under 60 on the OnePlus, a 3x option is the fallback for that phone.
