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
  - Step 3 (right-size the buffers): done on both phones (11-10), not committed yet. See "Step 3: right-sized buffers" below.
  - Packing attr into color and one tile buffer instead of three (guess: 10–20% of DepthBlend). Not started.
  - Judging pacing's skipped frames by eye on NSMB's map and Mario Kart.
  - A scene with anti-aliasing or fog for `computeopt` bit 4.
  - Step 6 (program binary cache).
- **The title won't reach 60 at 4x on this phone** without a much bigger change (a depth pre-pass so overdrawn layers aren't shaded). 2x holds 60 there (59.67); 3x wasn't measured on the title.

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
