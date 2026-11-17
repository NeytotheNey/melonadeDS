# GPU 2D: filtered, hi-res and wide 2D

The goal is visuals: filtered 2D, hi-res 2D and more 2D in WideMelon's wings. There's no performance case for it: the OnePlus 7 Pro profile on 11-06 found compute 4x GPU-limited and OpenGL 4x limited by other CPU work (PERF-NOTES "OnePlus 7 Pro: what limits the capture scene"). Upstream's renderer isn't being ported ([blackmagic3.md](blackmagic3.md)); we only borrow ideas from it.

Status: steps 1 and 2 are done and in `material3` / core `melonade` (the `gpu-2d` branches are merged and gone). Step 3 is in progress in [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md). Step 4 waits for it.

## Decisions (user, 11-06)

- **The GPU renderer is a shadow copy.** CPU 2D keeps drawing every line and stays the real result for capture, HASH2D and software 3D. The GPU image is display-only, and the compositor picks GPU or CPU per line. This is the model from [hires-capture.md](hires-capture.md): any doubt means native.
- **Order:** counters, then filtered 2D, then the shader renderer, then wide 2D.
- **Filter look:** a pixel-art filter that keeps edges crisp (MMPX or xBR), not plain bilinear.

## What each goal needs

Plain BGs and sprites look the same at 4x unless they're filtered, because the source art is 1x. Drawing at higher resolution only sharpens rotated or scaled layers. So filtering is the change most 2D games will show, and hi-res helps a limited set of scenes. The counters measure how limited.

## Step 1: counters (done)

`debug.melonade.gpu2dstats`, debug only, changes nothing about emulation (core `src/GPU_2DStats.h` plus hooks in `GPU.cpp`/`GPU.h`). A once-a-second `GPU2DSTATS` line per engine: BG types per line (text, affine, bitmap), sprite features, mosaic, windows, blending, capture lines, and what changes during the visible lines (VRAM, palette, OAM, backdrop color only, DISPCNT/BGCNT). It doesn't need a GL renderer, so the AVD can run it.

### Results (AVD, 11-07)

12 games, 129–153 s each, mostly title screens and menus (only Advance Wars and New Super Mario Bros were confirmed in gameplay), so this is not a gameplay survey; step 3's phase 0 did gameplay. Raw logs are in the git-ignored `logs/emulator/gpu2dstats/`. HASH2D on New Super Mario Bros with the switch on matched runs with it off.

| Game, engine | Lines with a rotated layer % | Frames with mid-frame changes % | Upload avg / max KB |
|---|---:|---:|---:|
| Golden Sun Dark Dawn B | 48.8 | 46 | 88 / 268 |
| Metal Slug 7 B | 38.8 | 0 | 153 / 448 |
| Golden Sun Dark Dawn A | 28.8 | 26 | 28 / 107 |
| Mario Kart DS B | 28.9 | 0 | 1 / 414 |
| Advance Wars B | 8.6 | 0 | 1 / 296 |
| Advance Wars A | 3.8 | 1 | 1 / 208 |
| Kirby Super Star Ultra A | 0.9 | 16 | 31 / 384 |
| Kirby Super Star Ultra B | 0.8 | 5 | 9 / 334 |
| New Super Mario Bros A | 0.3 | 0 | 3 / 292 |
| Contra 4 B | 0 | 14 | 25 / 286 |
| Pokemon White 2 A | 0 | 9 | 11 / 528 |

Every other engine in the set showed 0 rotated lines and at most 2% of frames with changes: Contra 4 A, Metal Slug 7 A, TWEWY, New Super Mario Bros B, White 2 B, Mario Kart A, Phantom Hourglass, Platinum and Taiko.

What this means:

- **Filtering is the right first step.** Most scenes have little or no rotated content, so hi-res alone wouldn't show.
- **Mosaic can stay on CPU at first.** BG and OBJ mosaic were 0 in every run. Rotated sprites are rare too (at most 2.3% of lines).
- **The backdrop dirty bit isn't worth doing yet.** Backdrop-only changes averaged 0.
- **Upload spikes are real.** Averages are small, but single frames reach 192–528 KB, so uploads should be dirty-range only.

## Step 2: filtered 2D (done)

The compositor already gets the CPU's finished lines, so it can filter them and put the hi-res 3D on top without blurring it. Today's video filters (`VideoFilterPipeline`) run after 3D and 2D are merged, so they can't separate the two.

**The filter picks pixels, it doesn't make colors.** MMPX only ever selects a neighboring source pixel, never blends. So a pre-pass in the core's compositor outputs a map: for each sub-pixel, which native pixel to show. The compositor then reads planes 0–2 through that map and runs its existing `CompositePixel` untouched, with 3D still fetched at the fragment's real position. Blending, 3D-on-top, fades and master brightness stay exact without special cases, and 2D edges over 3D get smooth because the map chooses between a 2D pixel and a 3D-slot pixel.

- **Key image:** the pre-pass compares a per-pixel key. For a mode-1 pixel with compmode 7 it's plane 0. For the others it's plane 1 plus a flag marking a 3D slot, so 2D/3D edges are detected and 2D showing through transparent 3D still gets filtered. Other lines use plane 0. Hi-res capture lines, native mode-2 lines and mode-3 lines stay identity.
- **Scale:** 2x, with a second pass to 4x at scale 4 and up (8x reuses the 4x map). Off at scale 1, where there's no extra resolution.
- **Control word:** still read from the real row. A map entry that crosses into a line with another display mode is ignored, because planes 1 and 2 aren't written on those lines.
- **Filter:** MMPX (MIT), as the app's filter list already has it. xBR blends, so it doesn't fit this scheme. MMPX has small quirks on fine detail, like a hollow diamond on Advance Wars' lion; that's how MMPX looks.
- **Skip:** when a CPU check of the 2D planes finds nothing changed (`UpdateFilter2DKeys`), the key and MMPX passes are skipped and the map is reused (core `0de067ee`; the FILTER2D log shows `skipped=` and `checkus=`). The check is pure overhead in scenes that never skip (Advance Wars, about 0.1–0.35 ms a frame); stopping it after a run of changed frames is an option if that matters.

Display capture, hi-res capture, CPU 2D and threaded 2D aren't touched. The filter sees flattened 2D, so sprite and BG edges get smoothed together. Hi-res capture's pass doesn't go through the map, so 2D inside a captured image stays native.

Code: core `GPU_OpenGL.cpp/.h` (`PrepareFilter2D`, `RunFilter2D`, `FreeFilter2D*`), `GPU_OpenGL_shaders.h` (key and MMPX passes, `FILTER2D` compositor variant), `GPU.h` (`Filter2D`, `Filter2DLogging`). App: the "Smooth 2D edges" setting, plumbed like "Upscaled display capture", and `filter2d` in `DebugProperties`.

Switches: Settings → Display → "Smooth 2D edges" (OpenGL and compute, default off), and `debug.melonade.filter2d`: `0` off, `1` on, `2` only the 2x pass at every scale, `3` all passes with identity output (must match off exactly). Later cost work (partial map redraws, `filter2drows`) is PERF-NOTES "K3".

### Results

Phone-tested on the OnePlus and the Fold (11-07), OpenGL and compute, with no bugs found:

- 2D edges smoothed on both screens, including the lower rows (the suspected Adreno sign bug didn't show), and 3D stays sharp. Filter on adds no new colors. White 2's title art is already anti-aliased, so it barely changes.
- Identity mode 3 matched off: 0 differing pixels at 2x and 4x on exact frame pairs; at 8x and with WideMelon, still pixels matched and only animated areas differed, as much as an off-vs-off pair.
- The setting is hidden on software, survives a renderer switch and follows scale changes. Scale 1 matches off.

Cost at OpenGL 4x, GPU Mclk/frame off → full: White 2 title 3.5 → 7.3 and Advance Wars menu 1.1 → 4.0 on the OnePlus; 2.8 → 3.4 and 1.5 → 2.1 on the Fold (kgsl busy × clock, so only compare within one phone). The Fold (cover screen, plugged in) held 60 throughout. On the OnePlus, White 2's title dropped from 53.6 to 36.8 fps even though the GPU sat near 270 of about 574 M/s, which led to the next two findings.

**Deband, not the filter, was maxing the OnePlus GPU** (11-07, `perf_logs/2026-11-07-filter2d/baseline/`). Deband adds about 6.5 Mclk/frame at White 2's title, almost twice what the full filter adds (3.8), costs about 10 fps of 1% low and runs the GPU about 15 °C hotter. So judge the filter against the Deband-off baseline. Deband now checks its rings farthest first and stops at the first that passes (same output, fewer reads); its later rework is in [plan-deband-outcap.md](plan-deband-outcap.md).

**The filter's cost on the OnePlus was a sync stall, not GPU throughput** (`perf_logs/2026-11-07-filter2d/paced/`, `capreads/`). It's a serial loop: the emulator thread works ~12 ms on the gold cores, then sleeps in White 2's line-192 capture readback map until the GPU catches up. Neither side is maxed: the GPU sits near its lowest step and the governor never steps up because the GPU idles while the CPU runs.

| Filter (OnePlus, Deband off) | fps | map wait ms/frame | GPU clock | emu CPU ms/frame @ GHz |
|---|---|---|---|---|
| off | 54.5 | 3.0 | 257 MHz | 14.0 @ 1.96 |
| on | 37.9 | 6.9 | 345 MHz | 19.7 @ 1.45 |

Of the filter's +8 ms a frame, about 4 ms is the longer wait (its GPU work sits ahead of the readback at a low clock) and about 5.7 ms is the gold cores clocking down for the same work. Why the clock drops isn't known: there's no thermal cap, and ADPF isn't supported. Qualcomm's perf HAL tags the app as a heavy game, which is the only lead. The skip alone made it worse on the OnePlus: while frames are skipped the GPU drops to its lowest clock, so the frames that still run the filter took about 37 ms (19% of frames over 33 ms). The fix was taking the filter off the readback's critical path, below.

### Blit after the readback (on in release since 11-16)

The GPU runs work in the order it's sent. In a capture frame the compositor and filter for frame N went in at line 192, ahead of the 3D render of N+1 (line 215) and its readback (line 0), so the map at the next line 192 waited behind them. The present of frame N could also get ahead of the readback, which is probably why Deband made the wait longer.

`debug.melonade.blitafterread` (core `GPU::BlitAfterReadback`, OpenGL `7516a617`, compute `e5c7c29a`, default true since `76547da9`) changes the order in frames that hold capture lines:

- **line 192:** nothing is sent. The lines are drawn, the bank write masks are saved, and the capture pass's destination bank is copied (`HiresCaptureRecord::DstNative`), since the CPU can write it in VBlank.
- **line 215:** the 3D render of N+1 goes into a second color buffer, so frame N's 3D stays alive. Then the readback of N+1 goes into its own PBO, then a flush, then the Blit of N from the kept buffer, then a flush.
- **line 0:** if this frame captures 3D and nothing changed the 3D since 215, the early PBO is swapped in. Otherwise line 0 reads back as before, and the main PBO is left as the old path would leave it, so a capture armed late reads the same data either way.

Display latency is the same: the Blit still runs inside `RunFrame`, before the frontend's fence. A `Sync2D` between 192 and 215 runs the Blit early, except for LCDC and POWCNT writes, which the compositor doesn't read. A resize at 215 keeps the old order. It costs one more color texture (3 MB at 4x, up to about 17 MB at 8x with WideMelon 342) and 128 KB per hi-res capture record. On compute the final pass writes a spare image so frame N's 3D stays alive, and the early readback has its own pixel buffer and fence, created before the flush ahead of the Blit. Core files: `GPU.cpp/.h`, `GPU3D.h`, `GPU3D_OpenGL.cpp/.h`, `GPU3D_Compute.cpp/.h`, `GPU2D_Soft.cpp`, `GPU_OpenGL.cpp/.h`, `GPU_CaptureReads.h`.

OnePlus, White 2 title, OpenGL 4x, Deband off, on battery (11-07, `perf_logs/2026-11-07-filter2d/blitafter/`):

| blitafterread / filter2d | fps | 1% low | map wait ms/frame | GPU MHz | emu CPU ms @ GHz |
|---|---|---|---|---|---|
| 0 / off | 54.2 | 40.0 | 3.0 | 257 | 14.1 @ 1.95 |
| 1 / off | 59.9 | 56.2 | 0.1 | 308 | 13.0 @ 2.18 |
| 0 / on | 37.9 | 28.6 | 6.9 | 345 | 19.8 @ 1.45 |
| 1 / on | 59.9 | 56.1 | 0.1 | 500 | 13.0 @ 2.03 |

- **Compute on the OnePlus** gains much less (24.8 → 28.3 fps with the filter on, 29.7 → 30.9 off), because it's GPU-bound (585 MHz, its top step, 98% shaders busy) and the map still waits about 15 ms a frame.
- **Fold inner screen:** OpenGL with the filter on went from 56.7 fps (1% low 34.9) to 60 (59.6). Busy × clock rises 10–13% with the new order, probably the governor stepping up, not more work.
- **With the filter skip on top** (11-08, `perf_logs/2026-11-08-filterskip/`) the OnePlus stutter is gone (nothing over 20 ms) and the Fold's GPU work at White 2's title dropped 16%.
- **Release check** (11-16, OnePlus plugged in, bnohold on, filter off, White 2, Dark Dawn and Phantom Hourglass titles, `perf_logs/2026-11-16-bnohold-oneplus-w2/blit/`): with bnohold on, OpenGL already holds 60 with blit 0, so the new order is close to neutral there; compute gains 1–3 fps.
- **Pictures** matched the old path on exact frames in every run (0 differing pixels, including the 3D Kyurem and the captured fire), with no GL errors, crashes, black frames or `.sav` changes. Not frame-exact: a HASH2D per-frame check wasn't run. Kyurem's grey feet below the floor line in the big-3D phase are the title's own effect; they show on the old path too.

### Still open for step 2

- The quarter-size second pass (512x768 writing four results, picked by sub-pixel parity) if the GPU gets tight.
- Optional: a long Fold run plugged in, 20–30 min of gameplay with the filter on. The 70 s runs never got hot enough to throttle.

## Step 3: shadow renderer

Planned and in progress in [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md): shaders decode tiles and sprites from VRAM and composite at output resolution, for the lines they can prove match CPU 2D. Opt-in. On compute it adds load to a GPU that's already maxed out on the OnePlus.

## Step 4: wide 2D

The shader draws X outside 0..255. Today wings are 3D only, and 2D-only lines are black. Two problems have no safe automatic rule: tilemaps wrap and show junk in the wings, and sprite X is 9-bit, so a sprite parked offscreen looks the same as one in a wing. Expect a per-game opt-in, possibly from Lua ([MODS.md](../MODS.md)). Capture stays at the middle 256. The detailed wide 2D plan is [plan-widemelon.md](plan-widemelon.md) (steps 5 to 7: edge repeat, full-outline carry, a Lua API, HG/SS battles first). Wide 2D waits for step 3, which must draw any layer over an X range wider than 0..255 from the start.

## Later, if it earns it

Skipping CPU lines to save time. That's a separate decision, once GPU output has proven it matches. It would need the consumers of the CPU pixels handled: `DoCapture`, the hi-res stream, HASH2D, software 3D, the wing bit's window mask, and plane 1 carried between engines.
