# Compute renderer efficiency

Plan written 11-09 on branch `compute-efficiency` (app and core, both cut from `gpu-2d-shadow`). The goal is less GPU time, less graphics memory and less battery from the Compute 3D renderer, mainly for older Adreno phones. The test case is the OnePlus 7 Pro (Adreno 640) at 4x. 60 FPS there is the aim but not a hard target: any gain counts.

## Where it stands

From `PERF-NOTES.md` and a code read of `GPU3D_Compute.cpp`, `GPU3D_Compute_shaders.h`, `GPU_OpenGL.cpp` and the app's `FrameQueue.cpp`:

- **The OnePlus at Compute 4x is GPU-bound.** White 2's title runs at 24–28 FPS with the GPU at its top clock step, about 18 Mclk a frame against about 3.4 for the OpenGL renderer. The cap at that clock is about 9.5 Mclk a frame for 60 FPS, so it needs about half the work. It isn't thermal or CPU.
- **Nobody knows which pass costs what.** There are no GL timer queries in the core or the app. The perf logger has no GPU field.
- **NSMB's map at Compute 4x** presents 2.2 FPS while emulation runs at 53. Unexplained.
- **Memory at 4x is about 296 MiB of buffers and textures** (the Fold showed about 696 MB graphics PSS against about 350 for OpenGL):

| Buffer | 4x | Why it's that big |
|---|---|---|
| Tile work buffers (color, depth, attr) | 144 MiB | sized for 16 polygon layers on every tile (`MaxWorkTiles = tiles * 16`) |
| Frame queue, 9 textures | 54 MiB | sized for 8x fast-forward; the free list is FIFO so all 9 get used |
| X span setup | 48 MiB | 96 B × a guessed maximum × scale |
| Final tile buffer | 18 MiB | 24 B a pixel |
| Compositor output textures ×2 | 12 MiB | never drawn into on Android: the FBO is re-pointed at the frame queue texture every frame |
| Bin results | 6 MiB | sized for 2048 polygons |
| Everything else | 13 MiB | |

## Suspects for GPU time

Read from the code, not measured:

- **Tile size.** `TileSizeForScale` gives 8 px tiles for 1x–4x and 16 px for 5x–8x, so 4x has 12,288 tiles and 5x has 4,800. Binning is polygons × tiles. Bigger tiles cut binning and work items but waste more rasterizer threads on partly covered tiles.
- **Rasterizer.** A full tile of threads runs per (polygon, tile) pair whatever the coverage. Each pixel thread copies a 96-byte `XSpanSetup` and a 40-byte `Polygon`, and calls `CalcYFactorX` (a software 64/32 divide) before the linear-span test that can make it unused.
- **Work that could be skipped.** `ClearCoarseBinMask`, `DepthBlend` and `FinalPass` run on zero-polygon frames. `VCount215` always calls `RenderFrame`, shown or not. `Texcache.Update` returns true on any texture or palette VRAM write, even when no cached entry changed, which defeats the identical-frame skip.
- **Wide formats.** Attr tiles use 32 bits for 9, depth 32 for 24. The final tile buffer always writes a second-layer depth that only the fog variants read.
- **Both screens are composited at scale**, including the 2D-only one.

## Steps

1. **Per-pass GPU timers.** `GL_EXT_disjoint_timer_query` around each compute pass and the compositor draw, behind `debug.melonade.computetimers`, summed and logged once a second. Debug builds only, off by default, no cost when off. Run on the OnePlus at 4x: White 2 title, White 2 overworld, NSMB's map. This ranks step 4.
2. **Free memory wins.** Don't allocate the compositor output textures when an external output texture is in use. Allocate frame queue textures on first use and reuse the most recently freed one, so normal speed touches about 3 instead of 9. About 50 MiB at 4x; helps the OpenGL renderer too.
3. **Right-size the work buffers.** Log peak work-tile and span use per scene first. Then size the tile buffers and span buffer to a lower starting point and grow when a frame gets near the limit (the existing overflow guards stay as the backstop). The numbers from the peak log decide the starting size.
4. **GPU time, in the order step 1 gives.**
   - 16 px tiles at 3x–4x as a debug-switch A/B (one line in `TileSizeForScale`).
   - Rasterizer: move the divide behind the linear test, read only the span fields a pixel needs.
   - Skip the three passes on zero-polygon frames; don't force a render when a VRAM write invalidated nothing.
   - Narrower tile formats, only if the timers say DepthBlend or Rasterise is bandwidth-bound.
5. **Battery.** Falls out of step 4's skips. Measure with a plugged-in long run after it.
6. **Shader program binary cache.** Stops the synchronous recompile of all 33 programs on a scale or WideMelon width change. A hitch fix, not a frame-rate one.

## Where the plan stands (11-10)

- **Done on the OnePlus:** step 1 (by a different method), step 2, pacing (not in the original plan), the low-risk half of step 4, and the skip work for step 5.
- **Open:**
  - Why bit 32 causes long frames; it's the remaining battery win in 30 FPS games with Smooth 30 FPS on.
  - Step 3 (right-size the buffers): done on both phones (11-10) and committed. See "Step 3: right-sized buffers" below.
  - Packing attr into color: done (11-10); the confirm run of the default-on build is still owed. `computeopt` bit 128, default on. OnePlus title 32.23 → 33.12 FPS, NSMB map render median 7.26 → 5.87 ms; the Fold saves 27–37 MB. One buffer instead of two (bit 256) measured the same, so it's off. Numbers are in `HANDOFF-compute.md`, step 6.
  - Judging pacing's skipped frames by eye on NSMB's map and Mario Kart.
  - A scene with anti-aliasing or fog for `computeopt` bit 4.
  - Step 6 (program binary cache): written and reviewed 11-11, not phone-tested. See `HANDOFF-compute.md`.
- **The title won't reach 60 at 4x on this phone** without a much bigger change (a depth pre-pass so overdrawn layers aren't shaded). 2x holds 60 there (59.67); 3x wasn't measured on the title.

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

1. **`computetile 8` at 8x on the Fold.** No code, one phone step. 8x uses 16 px tiles and wastes about a third of its 256 threads (3.95 visits vs ~2.6 colored); fits the work-tile limits at the 256 view.
2. **Stage 0's probes and census**, which decide whether 1a pays in W frames and whether 1a or 1b comes first. If most opaque work comes before the first cut-out polygon, 1a alone is most of the win at ~60% of the lines.
3. **1c**, ~15 lines on top of V.
4. **Stage 3**, ~60 lines, needs V.
5. **Whole-tile occlusion at bin time.** Shortens every later walk, but the exact conditions are tricky (no edge pixels of the occluder in the tile, no equal-test polygon between, no cut-outs). Only if the census shows long walks behind full-tile occluders.

Not worth it: splitting texture sampling out of Rasterise (Compute already samples a pre-decoded cache in one fetch); reading depth before color (about neutral in Phase 0); narrowing Result (color, attr and depth need 70 bits a layer).

### Measure first, then decide

1. Rasterise's cost split (divide, interpolation, texture, stores) in a W scene (title) and a Z scene (overworld). If the divide dominates, V costs nearly what Rasterise does in W frames and 1a pays mainly in Z frames.
2. The census: prefix share of visits and work tiles, cut-out share, winners per pixel, translucent fragments behind the opaque bound, and the alpha 1–30 counter (must be 0).
3. `computetile 8` at 8x.
4. On Mali: `GL_MAX_TEXTURE_BUFFER_SIZE`, and whether a DepthBlend-sized shader spills.
5. How much of the 935 MB is tile memory at the 8x title (`COMPROOMS`, `COMPGROW` lines), before stage 2.

**Open:** whether any game puts alpha-31 shadow polygons early in the opaque group (it would shorten the prefix); whether V's per-visit cost on Adreno comes in under DepthBlend's memory-bound visit. **Other risks:** particle-heavy translucent scenes gain little before stage 5; `prevIsShadowMask` is easy to break (stage 3, Mario Kart's shadows); 3–8 more programs and the hard-coded counts and keys; the grow-and-skip safety valve stays inexact when it triggers, as today. Code map: `GPU3D_Compute_shaders.h` (Rasterise ~:1240–1561, DepthBlend ~:1563–1837, BinCombined ~:954–1149, FinalPass ~:1839), `GPU3D_Compute.cpp` (RenderFrame ~:2244–3100, ResizeTargets/BuffersFit ~:766–1025, QueueHash3D ~:1731), `GPU3D_Soft.cpp` and `GPU3D.cpp` for the rules and the opaque-first sort.

## Rules

- HASH2D and exact-frame matches must stay identical after every step; hi-res capture, WideMelon, halfcam and dither must keep working.
- GL changes need the phone: a Fold run and a OnePlus run before a step counts as done.
- Perf runs follow `PERF-NOTES.md`'s recipe (fresh start per run, interleaved A/B; back up the `.sav` around any state load).
- If the timers show the cost is spread evenly and 4x stays well under 60 on the OnePlus, a 3x option is the fallback for that phone.

## Results

All on the OnePlus 7 Pro, Compute 4x, unplugged, one 30 s window per config (11-09). Raw logs are in `perf_logs/2026-11-09-compute-timers/`. Nothing here has run on the Fold yet.

### Step 1: timers

- **Timer queries don't work for compute on Adreno 640.** `debug.melonade.computetimers` (`COMPTIME`) reads 0.000–0.004 ms for every compute pass in every scene. Only the compositor buckets look real. The switch costs nothing measurable.
- **`debug.melonade.computesync` replaces it** (`COMPSYNC`): a `glFinish` after each pass, timed by wall clock. It inflates everything about 2.5x (sum × normal FPS comes to 2.2–2.6 s a second), so read it as shares:

| ms per render | interp | bin | rast | depth | final | clear+offs+sort |
|---|---|---|---|---|---|---|
| White 2 title 4x | 5.68 | 6.64 | 25.85 | 24.86 | 4.06 | 2.91 |
| White 2 overworld 4x | 1.87 | 2.00 | 14.84 | 13.17 | 6.15 | 2.17 |
| NSMB map 4x | 2.07 | 3.60 | 19.91 | 18.01 | 3.51 | 2.38 |
| Title 2x | 3.31 | 2.71 | 9.40 | 10.91 | 1.69 | 2.32 |
| Title 5x (16 px tiles) | 5.38 | 3.15 | 40.23 | 25.17 | 4.97 | 2.43 |

- **Rasterise and DepthBlend are 70–77% of compute time in every scene**, about equal. Both run once per pixel of every work tile (title: 65k work tiles × 64 = 4.2M invocations, 5.3 per screen pixel). DepthBlend is nearly all memory access.
- **16 px tiles look cheaper:** from 4x/8 px to 5x/16 px (1.56x the pixels) binning halves and DepthBlend stays flat. `debug.melonade.computetile N` forces a tile size for the A/B.
- **Peak use** (`debug.melonade.computepeaks`, `COMPPEAK`; it syncs every frame, so ignore FPS): title 64,934 of 196,608 work tiles (CPU bound 116,265), 75,095 of 524,288 X spans; overworld 28,790 and 17,549; NSMB map 40,415 and 20,021. No overflow anywhere.
- **Baseline FPS:** title 31.4, overworld 59.9, NSMB map 53.4 emulated but 2.1 presented.

### NSMB's map: a pacing problem

With no capture readback in the frame, nothing holds the emulator thread back, so it submits renders faster than the GPU finishes them. The driver queue sits about 0.6 s deep (render latency 610 ms median) and the frame queue fills to 9/9. `debug.melonade.computepace N` puts a fence after each render:

| pace | emulated FPS | presented FPS | render ms median / p95 | frame queue |
|---|---|---|---|---|
| 0 (old) | 53.44 | 2.1 | 610.61 / 764.19 | 9 |
| 1, wait | 51.65 | 51.5 | 19.11 / 19.65 | 5 |
| 2, skip | 59.93 | 59.9 | 9.15 / 18.14 | 5 (8 peak) |

Mode 2 rendered 51 and skipped 9 frames a second. Neither mode engaged on White 2's title or overworld (31.4 and 59.9 in all three). At 3x the map holds 60 without pacing. Mode 2 is now the default in all builds (`GPU::ComputePace`): it skips when that's safe and waits when it isn't (capture frames, Smooth 30 FPS). A skipped frame that a capture then needs is redrawn at line 0 with the line-215 textures.

With the default and Smooth 30 FPS on, the map waits instead of skipping: 51.7 emulated and 51.6 presented (86% speed), render 19.1 ms, frame queue 4. A 3-minute soak held 51.7 FPS start to end, GPU zone 59 → 63 °C, no GL errors. Whether skipped frames look juddery in motion hasn't been judged by eye (`screenrecord` is blocked for the shell user on this OxygenOS).

### Tile size: 16 px loses at 4x

`computetile 16` against the default 8 px: title 31.45 → 28.17 FPS, overworld and NSMB map unchanged at 60 (map render 9.05 → 13.17 ms). Binning halves (6.64 → 3.06) but Rasterise grows 47% (25.85 → 37.98), because every work tile costs a full tile of threads. DepthBlend stays flat. HASH2D matched on all 1,137 title frames. Bigger tiles and tile-grouped DepthBlend are dropped.

### Shader changes (`computeopt`, default all on)

Four changes meant to be bit-identical, one per bit: skip the unused divide for linear spans in Z-buffer programs (1), load only the span and polygon fields the coverage test needs (2), skip second-layer stores the final pass doesn't read unless AA or fog is on (4), a dead branch and a folded constant (8). All 33 programs compile under the NDK's `glslc` for every mask, and with the mask at 0 they match the old shaders.

- Title 31.40 → 32.15 FPS; NSMB map render median 8.93 → 7.55 ms; overworld unchanged at 60.
- HASH2D matched on all 1,278 title frames (that sees 3D at 1x through the capture). Overworld and NSMB map: screenshots at about the same moment, no differences outside moving sprites and water.
- Not checked: a scene with anti-aliasing or fog (Mario Kart's demo isn't frame-deterministic).

### Identical frames are being re-rendered

White 2 draws 3D at 30 FPS, but with Smooth 30 FPS off the overworld does 52–53 renders a second and the title renders on every frame. `Texcache.Update` returns true on any texture or palette VRAM write, which defeats the identical-frame skip even when no cached texture changed. Bit 16 of `computeopt` (default on) only defeats the skip when a cached entry was really erased. `debug.melonade.computewhy` (`COMPWHY`) counts why each render wasn't skipped.

OnePlus, 4x, plugged in this time (11-10), `computeopt 15` against the default 31:

| Scene | Smooth 30 FPS | renders/s before | after | FPS |
|---|---|---|---|---|
| White 2 overworld | off | 51.89 | 29.97 | 59.94 both |
| White 2 overworld | on | – | 59.99 | 59.94 |
| White 2 title | off | 32.21 | 32.22 | 32.17 / 32.18 |
| NSMB map | off | 52.77 | 52.63 | 59.93 / 59.94 |

- **Overworld:** 22 renders a second were forced by dirty VRAM alone (`texdirty=22/22`); bit 16 removes them. Rough battery read over 60 s: about 240 mA less draw and the GPU zone ending at 50.2 °C instead of 59.4 (plugged in, one run each, so a direction only).
- **Title:** nothing to skip. The game swaps on every frame it runs and no swap repeats (`flush=33/33 flushsame=0 captured=33`). It's GPU-bound, not over-rendered.
- **NSMB map:** the game swaps 60 times a second, none identical; pacing skips 7.
- **What's left in the overworld:** of 30 swaps a second, about 14 repeat the previous scene exactly (`flushsame`) and still render. With Smooth 30 FPS on, HalfCamera adds an in-between render per swap (`halfcam=30/30`), so the fix saves nothing there.
- **Pixels:** HASH2D matched on 1,256 of 1,256 title frames; overworld and map screenshots differ only in moving things.

### Repeated scenes, Smooth 30 FPS and the older bugs (run 6, 11-10, plugged in)

- **Pacing with Smooth 30 FPS on (`computeopt` bit 64, default on).** A pace skip is now allowed when HalfCamera has nothing to interpolate. NSMB map, Smooth on: 53.19 → 59.97 FPS, presented 53.1 → 60.0, render median 18.62 → 7.27 ms, about 7 skips a second.
- **Repeated-scene skip (`computeopt` bit 32, default off).** A swap whose polygons, vertices and registers equal the previous swap's by value doesn't render. Overworld, Smooth off: 29.98 → 16.81 renders a second at the same FPS, but presented drops 60.0 → 59.6 (12 skipped display frames and 62 late presents in 30 s). Overworld, Smooth on: 60.01 → 30.25 renders a second and the GPU zone ends at 50.5 °C instead of 59.4, but FPS goes 59.97 → 58.36 with about 100 frames per 30 s over 16.7 ms of work (max 38.2 ms). Isolating bits put it on bit 32 alone (`computeopt 63`: 58.28; `95`: 59.96). Off until the cause is found.
- **3D off:** Contra 4's 2D intro renders 0 times a second; White 2's bag renders once on entry and then never. The bag's settled screens are pixel-identical between Compute and OpenGL, with no leftover 3D.
- **Pixels:** HASH2D matched on 1,255 title frames against run 5, with Smooth off and with it on. The PC harness matched HEAD on 58,200 software-renderer frames over 21 games (it never reached the engine-off branch; no game there powers 3D off).
- **Mario Kart demo, Smooth on:** 59.96 FPS, pacing skips up to 21 a second, stills look right. Judder not judged.
- **Soak:** 3 minutes in the overworld with bit 32 on held 58.5–58.9 FPS, no GL errors.
- **After run 6:** a `RenderedSinceVBlank` guard (a VCOUNT write that jumps past line 215 can't leave a stale image) and the bit 32 default flip.

### Final build at defaults (11-10, plugged in, Smooth 30 FPS on, no props)

| Scene | FPS | presented | render median ms | frames over 16.7 ms in 30 s |
|---|---|---|---|---|
| White 2 title | 32.29 | 32.3 | 2.12 | all (31 ms a frame) |
| White 2 overworld | 59.97 | 60.0 | 1.73 | 4 |
| NSMB map | 59.97 | 60.0 | 7.14 | 10 |

HASH2D on the title matched run 6 on every frame: 1,256 and 1,257 frames with Smooth on, 1,254 with it off. No error lines in any run.

**Why bit 32 stutters:** it's the pacing wait. Overworld, Smooth on, `computeopt 127`: 84 waits in 26 s (807 ms, about 9 ms each, in bursts with two renders pending), 72 slow frames, render p95 14.5 ms against 2.5. Slow frames per second correlate 0.91 with `wait_ms`. With `computepace 0` the emulator-thread stutter goes (14 slow frames) but presented drops to 55.8 with 125 skipped display frames, so the GPU really does fall behind when half the renders are skipped. Why it falls behind wasn't measured; a dropped GPU clock is the guess (sysfs is denied on this phone).

A review found the bit 16 skip sound, and three older bugs next to it, fixed in run 6's build: turning the 3D engine off doesn't clear the identical flag (a stale image can linger), `CheckInvalid` reads past the dirty array for texture ranges that wrap, and games that never enable 3D draw an empty frame every frame.

### The Fold (11-10, final build, plugged in, cover screen)

Compute 4x at defaults holds 60.00 FPS with 0 frames over 16.7 ms on White 2's title (render median 0.46 ms), White 2's overworld (0.75) and NSMB's map (0.68). Pacing never skips there. HASH2D on the title matched the OnePlus on every overlapping frame (838–860 per pair, Smooth on and off). No GL errors.

Graphics memory on the title, settled: Compute 4x 477–483 MB (GL mtrack 378 MB), OpenGL 4x 205 MB (GL mtrack 118 MB), down from 696 MB on the old build. Unlike the OnePlus, the Fold commits the work buffers, so step 3 saves real memory there. Peak use matches the OnePlus: 31% of the tile work buffer, 14% of the span buffer.

### Step 3: right-sized buffers (11-10)

Tile memory starts at 6 of its 16 layers and the X spans at a 16th, and both grow before a frame's dispatch from numbers the CPU already has: the bounding-box work-tile bound and the exact span count. So no frame ever loses work, and the shaders didn't change. `debug.melonade.computegrow 0` gives the old sizes.

Fold, Compute 4x, Smooth on, plugged in, graphics memory settled:

| Scene | grow on | grow off | saved |
|---|---|---|---|
| White 2 title | 388 MB | 483 MB | 95 MB |
| White 2 overworld | 428 MB | 510 MB | 82 MB |
| NSMB map | 312 MB | 466 MB | 154 MB |

FPS is 60.00 in all of them on the Fold and unchanged on the OnePlus (title 32.24 against 32.22, unplugged). HASH2D matched on every overlapping frame on both phones, and `COMPPEAK`'s `pastroom` and `pastbound` read 0 throughout. The OnePlus's meminfo doesn't show these buffers at all, so the saving is only visible on the Fold.

### Step 2: frame queue

The queue settles at 4–5 textures at normal speed, not the 3 the plan guessed, and went to 9 on NSMB's map before pacing. Graphics memory on the title was 174,944 kB (EGL mtrack 126,280, Gfx dev 47,896); there's no before number, and the OnePlus doesn't seem to charge for buffer pages that are never touched, so step 3 may save little there.
