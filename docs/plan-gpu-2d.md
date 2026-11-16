# GPU 2D: filtered, hi-res and wide 2D

Plan, on branch `gpu-2d` in the app and the core. The goal is visuals: filtered 2D, hi-res 2D and more 2D in WideMelon's wings. There's no performance case for it: the OnePlus 7 Pro profile on 11-06 found compute 4x GPU-limited and OpenGL 4x limited by other CPU work (PERF-NOTES "OnePlus 7 Pro: what limits the capture scene"). Upstream's renderer isn't being ported ([blackmagic3.md](blackmagic3.md)); we only borrow ideas from it.

## Decisions (user, 11-06)

- **The GPU renderer is a shadow copy.** CPU 2D keeps drawing every line and stays the real result for capture, HASH2D and software 3D. The GPU image is display-only, and the compositor picks GPU or CPU per line. This is the model from [hires-capture.md](hires-capture.md): any doubt means native.
- **Order:** counters, then filtered 2D, then the shader renderer, then wide 2D.
- **Filter look:** a pixel-art filter that keeps edges crisp (MMPX or xBR), not plain bilinear.

## What each goal needs

Plain BGs and sprites look the same at 4x unless they're filtered, because the source art is 1x. Drawing at higher resolution only sharpens rotated or scaled layers. So filtering is the change most 2D games will show, and hi-res helps a limited set of scenes. The counters measure how limited.

## Step 1: counters

`debug.melonade.gpu2dstats`, debug only, changes nothing about emulation. A once-a-second `GPU2DSTATS` line per engine: BG types per line (text, affine, bitmap), sprite features, mosaic, windows, blending, capture lines, and what changes during the visible lines (VRAM, palette, OAM, backdrop color only, DISPCNT/BGCNT). It answers how much hi-res would show in each game, and how many times per frame a shadow renderer would have to stop and catch up, or fall back to CPU. It doesn't need a GL renderer, so the AVD can run it. Run it across a spread of the library, including 2D-heavy games (Advance Wars, Contra 4, Metal Slug 7, Kirby Super Star Ultra, New Super Mario Bros, TWEWY) and the capture games (White 2, Dark Dawn).

### Results (AVD, 11-07)

12 games, 129–153 s each at about 60 fps: 60 s with no input, then Start, A and touch taps. Only Advance Wars (mission briefing) and New Super Mario Bros were confirmed by screenshot; the other ten are probably title screens and menus, so this is not a gameplay survey. Raw logs are in the git-ignored `logs/emulator/gpu2dstats/`. HASH2D on New Super Mario Bros with the switch on matched two runs with it off, with 0 differing frames over the 423–487 frames logged in both of a pair (logcat dropped about 15% of the lines).

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

What this means, with the coverage caveat above:

- **Filtering is the right first step.** Most scenes here have little or no rotated content, so hi-res alone wouldn't show.
- **Mosaic can stay on CPU at first.** BG and OBJ mosaic were 0 in every run. Rotated sprites are rare too (at most 2.3% of lines).
- **The backdrop dirty bit isn't worth doing yet.** Backdrop-only changes averaged 0.
- **Most frames need no bands**, so CPU fallback for a changed frame looks enough to start with. The exception matters: Golden Sun has the most rotated layers and the most changes, so a per-frame fallback there could flip between hi-res and native. Decide this at step 3, and measure gameplay first.
- **Upload spikes are real.** Averages are small, but single frames reach 192–528 KB, so uploads should be dirty-range only.

## Step 2: filtered 2D

The compositor already gets the CPU's finished lines, so it can filter them and put the hi-res 3D on top without blurring it. Today's video filters (`VideoFilterPipeline`) run after 3D and 2D are merged, so they can't separate the two.

**The filter picks pixels, it doesn't make colors.** MMPX only ever selects a neighboring source pixel, never blends. So a pre-pass in the core's compositor outputs a map: for each sub-pixel, which native pixel to show. The compositor then reads planes 0–2 through that map and runs its existing `CompositePixel` untouched, with 3D still fetched at the fragment's real position. Blending, 3D-on-top, fades and master brightness stay exact without special cases, and 2D edges over 3D get smooth because the map chooses between a 2D pixel and a 3D-slot pixel.

- **Key image:** the pre-pass compares a per-pixel key. For a mode-1 pixel with compmode 7 it's plane 0. For the others it's plane 1 plus a flag marking a 3D slot, so 2D/3D edges are detected and 2D showing through transparent 3D still gets filtered. Other lines use plane 0. Hi-res capture lines, native mode-2 lines and mode-3 lines stay identity.
- **Scale:** 2x, with a second pass to 4x at scale 4 and up. Off at scale 1, where there's no extra resolution.
- **Control word:** still read from the real row. A map entry that crosses into a line with another display mode is ignored, because planes 1 and 2 aren't written on those lines.
- **Filter:** MMPX (MIT), as the app's filter list already has it. xBR blends, so it doesn't fit this scheme.

Display capture, hi-res capture, CPU 2D and threaded 2D aren't touched. The filter sees flattened 2D, so sprite and BG edges get smoothed together. Hi-res capture's pass doesn't go through the map, so 2D inside a captured image stays native. The AVD has no OpenGL renderer, so this needs phone runs, and a Fold run before it counts as fixed.

Switches: Settings → Display → "Smooth 2D edges" (OpenGL and compute, default off), and `debug.melonade.filter2d`: `0` off, `1` on, `2` only the 2x pass at every scale, `3` all passes with identity output (must match off exactly).

### Results (OnePlus 7 Pro, 11-07)

Built, reviewed and phone-tested, with no bugs found. Committed on `gpu-2d` (core `97341228`, app `87f008eb`), not pushed.

- **It works.** Advance Wars and New Super Mario Bros menus at 4x, on OpenGL and compute: 2D edges smoothed on both screens, including the lower rows (the suspected Adreno sign bug didn't show), and 3D stays sharp. Filter on adds no new colors, so nothing is blended. MMPX has small quirks on fine detail. White 2's title art is already anti-aliased, so it barely changes.
- **Identity mode 3 matches off:** 0 differing pixels at 2x and 4x on exact frame pairs. At 8x and with WideMelon, the still pixels matched; only animated areas differed, and an off-vs-off pair differed as much.
- **Robustness:** the setting is hidden on software, survives a renderer switch, and follows scale changes. Scale 1 matches off. 8x reuses the 4x map.
- **Cost is the open problem.** OpenGL 4x, phone plugged in (unlike the baseline), no throttling:

| Scene | Off | 2x map only | Full |
|---|---|---|---|
| White 2 title, fps | 53.6 | 42.7 | 36.8 |
| White 2 title, GPU Mclk/frame | 3.5 | 4.7 | 7.3 |
| Advance Wars menu, fps | 59.9 | 59.9 | 59.9 |
| Advance Wars menu, GPU Mclk/frame | 1.1 | 2.0 | 4.0 |

Compute 4x on White 2 went from 29.7 to 25.6 fps. On OpenGL the GPU stayed near 267 of about 574 M/s in these runs. Data: `perf_logs/2026-11-07-filter2d/`.

### Results (Fold, 11-07)

Same checks as the OnePlus, all passed: edges smoothed on both screens and in the lower halves, on OpenGL and compute; 3D sharp; no stray rows or columns; mode 3 matched off at 2x, 4x, 8x and with WideMelon (exact frames where the scene was still, otherwise no further from off than off is from itself). The Fold was plugged in and folded (cover screen) for every run, and White 2 ran with the user's own WideMelon 342, Lua scripts and layout.

| Scene, 4x | Off | 2x map only | Full |
|---|---|---|---|
| White 2 title, OpenGL, fps | 60.0 | 60.0 | 60.0 |
| White 2 title, OpenGL, GPU Mclk/frame | 2.8 | 3.0 | 3.4 |
| Advance Wars menu, OpenGL, fps | 60.0 | 60.0 | 60.0 |
| Advance Wars menu, OpenGL, GPU Mclk/frame | 1.5 | 1.75 | 2.1 |
| White 2 title, compute, fps | 60.0 | | 60.0 |

The Fold's GPU numbers are kgsl busy × clock, so compare on vs off within one phone, not across phones. No SIOP throttling; `limitCPUFreq` trimmed the prime cores a little with the filter on and off alike.

### Where White 2's time goes on the OnePlus (`capreads`, 11-07)

OpenGL 4x, unplugged, off/on/off/on. With the filter on, the line-192 capture readback map goes from usually ready (1.3 ms per frame) to waiting nearly every frame (5.5 ms per frame). That's about half of the extra frame time (14.4 → 22.4 ms, 58.4 → 44.6 fps); the rest is probably other GL calls stuck behind the same busy GPU, which `capreads` doesn't time. On the Fold the map never waits.

In this run the OnePlus GPU was maxed out with the filter both off and on (about 575 M/s, 92–94% shaders busy), and the filter-off frame cost 9.7 Mclk against 3.5 in the earlier run. **That was Deband, not the filter** (checked 11-07, plugged in, filter off; data in `perf_logs/2026-11-07-filter2d/baseline/`):

| OnePlus, White 2 title, OpenGL 4x, filter off | fps | 1% low | GPU Mclk/frame | busy % |
|---|---|---|---|---|
| Old prefs (Deband off) | 53.8 | 39.3 | 3.51 | 80 |
| Old prefs + Deband on | 55.4 | 30.8 | 10.23 | 92 |
| Current prefs | 53.8 | 29.6 | 10.54 | 92 |
| Current prefs, Deband off | 52.6 | 40.6 | 3.89 | 80 |
| Current prefs, `capreads` 1 | 54.2 | 30.0 | 10.46 | 92 |

Deband adds about 6.5 Mclk/frame, almost twice what the full filter adds (3.8). It also costs about 10 fps of 1% low and runs the GPU about 15 °C hotter. `capreads` costs nothing measurable. The other pref changes (dithering, the "sharp" filter) add about 0.4 together.

So the "out of GPU" result holds only with Deband on. Judge the filter against the Deband-off baseline (3.5). That reopens run 1's question: with Deband off, the filter took White 2 from 53.6 to 36.8 fps while the GPU sat near 270 M/s. Fps sits near 54 with the filter off too, whatever the GPU load, so something other than GPU throughput is setting the pace. `capreads` timing with Deband off, filter 0 vs 1, is the next measurement.

**What sets the pace (OnePlus, Deband off, plugged in, 11-07; data in `perf_logs/2026-11-07-filter2d/paced/`):** a serial loop. The emulator thread works ~12 ms on the gold cores, then sleeps in White 2's line-192 capture readback map until the GPU catches up. Neither side is maxed: the GPU sits at about 257 MHz (probably its lowest step), and the governor never steps up because the GPU idles while the CPU runs. The prime core isn't used.

| Filter | fps | map wait ms/frame | GPU clock | emu CPU ms/frame @ GHz |
|---|---|---|---|---|
| off | 54.5 | 3.0 | 257 MHz | 14.0 @ 1.96 |
| on | 37.9 | 6.9 | 345 MHz | 19.7 @ 1.45 |

Of the filter's +8 ms per frame, about 4 ms is the longer wait: its GPU work sits ahead of the readback and runs at a low clock. About 5.7 ms is the gold cores clocking down to 1.45 GHz for the same work. Why the clock drops isn't known: there's no thermal cap, and ADPF isn't supported. Qualcomm's perf HAL tags the app as a heavy game, which is the only lead.

So the filter's cost on the OnePlus is a sync stall, not GPU throughput. Cheaper passes help in proportion to the work removed. Moving the filter off the readback's critical path should help more: run the passes after the readback, or flush the readback before compositing. The same stall caps White 2 at ~54 with no filter, so a readback that doesn't block the emulator thread would help every capture game. That needs care, since games read the captured image back.

### Two savings (11-07, uncommitted)

- **Deband** now checks its rings farthest first and stops at the first that passes. The output is the same, with fewer texture reads in flat areas.
- **The filter** skips its key and MMPX passes, and reuses the map, when a CPU check of the 2D planes finds nothing changed (`UpdateFilter2DKeys`; the FILTER2D log shows `skipped=` and `checkus=`).

Both were reviewed, and both gave pictures identical to the old build on both phones: Deband on, filter 3 vs 0 while skipping, filter 1 new vs old, moving 2D, and compute. Data is in `perf_logs/2026-11-07-filter2d/savings/`. OpenGL 4x, old → new:

| | Fold (battery, cover screen) | OnePlus (plugged in) |
|---|---|---|
| White 2, Deband on: fps / 1% low | 60 / 58.2 → 60 / 59.7 | 53.9 / 30.1 → 59.1 / 33.5 |
| White 2, Deband on: GPU Mclk/frame | 2.81 → 2.46 | 10.52 → 9.50 |
| White 2, filter on: fps / 1% low | 60 / 51.1 → 60 / 59.6 | 37.9 / 28.5 → 41.3 / 20.3 |
| White 2, filter on: GPU Mclk/frame | 1.96 → 1.56 | 7.51 → 5.03 |
| White 2 frames skipped | 85% | 80% |
| Advance Wars menu frames skipped | 0% (the map scrolls) | 0% (the background crossfades) |
| Check cost per frame, mean / max | 60 µs / 0.5 ms (W2), 167 µs / 0.9 ms (AW) | 150 µs / 1.3 ms (W2), 355 µs / 1.4 ms (AW) |

On the OnePlus, Deband's gain comes mostly from the shorter readback wait (3.2 → 0.9 ms per frame), not from the 10% less GPU work. The skip helps the Fold, but on the OnePlus it makes White 2 stutter. While frames are skipped, the GPU drops to its lowest clock, so the 1 in 5 frames that still run the filter take about 37 ms. 19% of frames go over 33 ms (1% before), and p99 goes from 33.6 to 47 ms. Taking the filter off the readback's critical path should fix both the cost and the pacing. (It did: see "Filter skip on top of the Blit after the readback".)

Next for this step: make the filter cheaper (skip the passes when the 2D planes didn't change, then a cheaper second pass). Deband's shader is separately worth a look, since it costs more than the filter.

### Blit after the readback (11-07, core `7516a617`, debug builds only)

The GPU runs work in the order it's sent. In a capture frame the compositor and filter for frame N went in at line 192, ahead of the 3D render of N+1 (line 215) and its readback (line 0), so the map at the next line 192 waited behind them. The present of frame N could also get ahead of the readback, which is probably why Deband made the wait longer.

`debug.melonade.blitafterread` (core `GPU::BlitAfterReadback`, on in every build since 11-16 like `bnohold`) changes the order in frames that hold capture lines (OpenGL only):

- **line 192:** nothing is sent. The lines are drawn, the bank write masks are saved, and the capture pass's destination bank is copied (`HiresCaptureRecord::DstNative`), since the CPU can write it in VBlank.
- **line 215:** the 3D render of N+1 goes into a second color buffer, so frame N's 3D stays alive. Then the readback of N+1 goes into its own PBO, then a flush, then the Blit of N from the kept buffer, then a flush.
- **line 0:** if this frame captures 3D and nothing changed the 3D since 215, the early PBO is swapped in. Otherwise line 0 reads back as before, and the main PBO is left as the old path would leave it, so a capture armed late reads the same data either way.

Display latency is the same: the Blit still runs inside `RunFrame`, before the frontend's fence. A `Sync2D` between 192 and 215 runs the Blit early, except for LCDC and POWCNT writes, which the compositor doesn't read. A resize at 215 keeps the old order. It costs one more color texture (3 MB at 4x, up to about 17 MB at 8x with WideMelon 342) and 128 KB per hi-res capture record. Compute still has the old order and would need the same change.

OnePlus, White 2 title, OpenGL 4x, Deband off, **on battery** (the phone wasn't charging), order A B C D twice, averages of the two:

| blitafterread / filter2d | fps | 1% low | p99 ms | map wait ms/frame | GPU MHz | GPU Mclk/frame | emu CPU ms @ GHz |
|---|---|---|---|---|---|---|---|
| 0 / off | 54.2 | 40.0 | 23.7 | 3.0 | 257 | 3.7 | 14.1 @ 1.95 |
| 1 / off | 59.9 | 56.2 | 17.7 | 0.1 | 308 | 3.7 | 13.0 @ 2.18 |
| 0 / on | 37.9 | 28.6 | 33.3 | 6.9 | 345 | 7.5 | 19.8 @ 1.45 |
| 1 / on | 59.9 | 56.1 | 17.5 | 0.1 | 500 | 6.8 | 13.0 @ 2.03 |

The old-path numbers match the plugged-in runs above almost exactly. Every early readback was used. With the new order the gold cores stay near 2 GHz and the emulator thread waits on vsync instead. Filter on is the busiest GPU case (about 405 M/s at 500 MHz) and ran hottest (GPU 52–54 °C against 37–45), with no throttling caps.

Pictures: exact-frame pairs matched with 0 differing pixels for new vs old at filter 0, 1 and 3, and for filter 3 vs 0 on the new path. Those frames include the 3D Kyurem and the captured fire, and still areas never differed. No stutter, crashes, black frames or GL errors over 16 boots, and the saves didn't change. Data: `perf_logs/2026-11-07-filter2d/blitafter/`.

**Compute** got the same change: the final pass writes a spare image so frame N's 3D stays alive, and the early readback has its own pixel buffer and fence, created before the flush ahead of the Blit. It helps much less, because compute on the OnePlus is GPU-bound (585 MHz, its top step, 98% shaders busy). Same scene, on battery, averages of two passes:

| compute, blitafterread / filter2d | fps | 1% low | frames >50 ms (of ~2000) | map wait ms/frame | GPU util % | GPU Mclk/frame |
|---|---|---|---|---|---|---|
| 0 / off | 29.7 | 19.8 | 12 | 15.0 | 89 | 18.2 |
| 1 / off | 30.9 | 20.6 | 7 | 14.5 | 93 | 18.0 |
| 0 / on | 24.8 | 16.4 | 85 | 17.5 | 93 | 22.3 |
| 1 / on | 28.3 | 19.7 | 12 | 15.1 | 100 | 20.7 |

The new order fills some GPU idle time and takes most of the filter's cost away, but the map still waits about 15 ms per frame because the GPU is simply behind. Pictures matched the old path on exact frames (including a full-screen 3D frame with the capture), filter 3 matched 0, and there were no GL errors or tile corruption in about 400 screenshots. Kyurem's grey feet below the floor line in the big-3D phase are the title's own effect; they show on the old path too. Data: `perf_logs/2026-11-07-filter2d/blitafter-compute/`.

**Fold** (11-07, unfolded on the inner screen in landscape, on battery, one 30 s pass each; data in `perf_logs/2026-11-07-filter2d/blitafter-fold/`): on the inner screen the old path doesn't hold 60 with the filter on, and the new order fixes it.

| Fold, 4x | fps | 1% low | map wait ms/frame | GPU busy Mclk/frame |
|---|---|---|---|---|
| OpenGL, blit 0, filter off | 59.9 | 51.7 | 0.37 | 2.11 |
| OpenGL, blit 1, filter off | 60.0 | 59.6 | 0.09 | 2.34 |
| OpenGL, blit 0, filter on | 56.7 | 34.9 | 3.04 | 2.67 |
| OpenGL, blit 1, filter on | 60.0 | 59.6 | 0.02 | 3.01 |
| compute, blit 0, filter on | 60.0 | 59.6 | 0.30 | 5.41 |
| compute, blit 1, filter on | 60.0 | 58.0 | 0.05 | 5.89 |

Busy × clock rises 10–13% with the new order. Busy % stays the same while the clock steps up, so it's probably the governor, not more work. Pictures matched on exact frames (OpenGL filter 0 and 1, compute filter 1), still pixels never differed, and there were no GL errors, black frames or broken 3D. The cover-screen, plugged-in case wasn't rerun.

**OnePlus, plugged in, three games** (11-16, bnohold on by default now, 4x, filter off, cold boots, order 0 1 1 0, no throttling). Means of two runs:

| Game / renderer | fps blit 0 → 1 | 1% low | map wait ms/frame | GPU util % |
|---|---|---|---|---|
| White 2 title, OpenGL | 59.80 → 59.80 | 56.4 → 56.2 | 0.10 → 0.09 | 23 → 23 |
| White 2 title, compute | 29.6 → 31.4 | 22.1 → 21.3 | 14.6 → 13.7 | 86 → 90 |
| Dark Dawn title, OpenGL | 59.73 → 59.65 | 53.3 → 47.9 | 0.09 → 0.08 | 21 → 21 |
| Dark Dawn title, compute | 34.2 → 35.5 | 22.9 → 25.6 | 9.0 → 8.3 | 83 → 86 |
| Phantom Hourglass intro, OpenGL | 59.77 → 59.76 | 47.6 → 49.4 | 0.23 → 0.11 | 22 → 23 |
| Phantom Hourglass intro, compute | 49.0 → 52.1 | 25.0 → 28.4 | 8.1 → 6.6 | 85 → 91 |

With bnohold on, OpenGL with blit 0 already holds 60 and the old 3 ms wait is gone, so the new order is close to neutral there (Dark Dawn's OpenGL 1% low swung 46.6–57.0 between runs). Compute gains 1–3 fps and stays GPU-bound. Every capture used the early readback. Screencap bursts showed no more difference between blit 0 and 1 than between two blit 0 runs, exact pairs matched with 0 differing pixels, still pixels never differed, and there were no GL errors, crashes or `.sav` changes. Not frame-exact: a HASH2D per-frame check wasn't run. Data: `perf_logs/2026-11-16-bnohold-oneplus-w2/blit/` and `pics/`.

Nothing left on the test list, so it's on in release since 11-16 (the core default is now true).

### Filter skip on top of the Blit after the readback (11-08, core `0de067ee`)

The filter skip from "Two savings" was held because it stuttered on the OnePlus behind the readback. It's now cherry-picked onto `gpu-2d` and merged. Nothing in its logic changed. The key check reads the same back buffer and control words as the compositor upload in the same call, so it matches the frame being composited at line 192, with lateblit, and with the Blit deferred to 215. A fresh review found no bugs.

White 2 title, OpenGL 4x, Deband off, filter 1, blitafterread 1, one 30 s pass per build (quick recipe). Baseline is core `e5c7c29a`.

| | OnePlus (battery) base → skip | Fold (plugged in, cover screen) base → skip |
|---|---|---|
| fps / 1% low | 59.8 / 56.7 → 59.9 / 56.5 | 60.0 / 59.7 → 60.0 / 59.3 |
| p99 ms / frames over 33 ms | 17.5 / 0 → 17.6 / 0 | 16.8 / 0 → 16.8 / 0 |
| frames skipped | 73% | 63% |
| check cost, mean / max | 90 / 328 µs | 56 / 150 µs |
| GPU busy × clock | no counters; GPU ~3 °C cooler | 142 → 119 Mclk/s (−16%) |

The stutter is gone: frame times on the OnePlus are flat on both builds, with nothing over 20 ms. On the Fold the GPU sat at its 160 MHz floor the whole run (183 MHz on the baseline), and the saving is 14% while the title's 2D animates at 30 Hz and 24% once it stands still. Pictures: exact-frame matches on both phones, and still areas never differed. The check is pure overhead in scenes that never skip (Advance Wars, 0%), about 0.1–0.35 ms per frame; stopping the check after a run of changed frames is an option if that matters. Data: `perf_logs/2026-11-08-filterskip/` (`oneplus/`, `fold/`).

## Step 3: shadow renderer

The detailed plan (11-08) is [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md): a gameplay counter run first, then seven increments, with a page log of mid-frame VRAM changes instead of bands or a per-frame fallback. The sketch below is the original outline.

Shaders decode tiles and sprites from VRAM and composite at output resolution. Hi-res comes from evaluating the affine matrix per output pixel. Rough size is 2,500–3,500 lines, against about 4,500 upstream.

- **Per-line state** is recorded on the emulator thread when `LineJob` is built, into a texture read with `texelFetch`. Upstream's 30 KB UBO is over GLES's guaranteed 16 KB.
- **Memory:** VRAM as `R8UI` textures, uploaded from the dirty sets the HBlank flat copies already compute (a second tracker can't derive on its own, because `DeriveState` clears the bits). About 1.1 MB at most per frame, plus palette and OAM. Backdrop color changes get their own dirty bit, from upstream.
- **Mid-frame changes:** flats only hold the latest state, so either stop and draw a band or fall back to CPU for the rest of the frame. Step 1's numbers pick the rule.
- **Eligibility per line:** a control-word bit picks GPU or CPU, so features can land one at a time: text BGs and normal sprites first, then affine BGs, affine sprites, windows, blending, mosaic and bitmaps.
- **Checking:** a `check` mode at 1x reads the GPU result back and compares it with the CPU planes, the way `debug.melonade.hirescap check` does. The shaders can't run off-device.
- **Mobile cost:** fewer render target switches than upstream, `glInvalidateFramebuffer`, and no synchronous readback outside `check`. Upstream's OBJ mosaic costs about 48 fetches a pixel, so it can stay on CPU at first.

Opt-in. On compute it adds load to a GPU that's already maxed out on the OnePlus. Hi-res capture [Phase 2](hires-capture.md#phase-2-bitmap-bg-consumers) (bitmap BGs) likely joins this path, since the shader can sample bank images directly.

## Step 4: wide 2D

The shader draws X outside 0..255. Today wings are 3D only, and 2D-only lines are black. Two problems have no safe automatic rule: tilemaps wrap and show junk in the wings, and sprite X is 9-bit, so a sprite parked offscreen looks the same as one in a wing. Expect a per-game opt-in, possibly from Lua ([MODS.md](../MODS.md)). Capture stays at the middle 256. The detailed wide 2D plan is [plan-widemelon.md](plan-widemelon.md) (steps 5 to 7: edge repeat, full-outline carry, a Lua API, HG/SS battles first). Wide 2D waits for step 3, which must draw any layer over an X range wider than 0..255 from the start.

## Later, if it earns it

Skipping CPU lines to save time. That's a separate decision, once GPU output has proven it matches. It would need the consumers of the CPU pixels handled: `DoCapture`, the hi-res stream, HASH2D, software 3D, the wing bit's window mask, and plane 1 carried between engines.
