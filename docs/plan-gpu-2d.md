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

On the OnePlus, Deband's gain comes mostly from the shorter readback wait (3.2 → 0.9 ms per frame), not from the 10% less GPU work. The skip helps the Fold, but on the OnePlus it makes White 2 stutter. While frames are skipped, the GPU drops to its lowest clock, so the 1 in 5 frames that still run the filter take about 37 ms. 19% of frames go over 33 ms (1% before), and p99 goes from 33.6 to 47 ms. Taking the filter off the readback's critical path should fix both the cost and the pacing.

Next for this step: make the filter cheaper (skip the passes when the 2D planes didn't change, then a cheaper second pass). Deband's shader is separately worth a look, since it costs more than the filter.

## Step 3: shadow renderer

Shaders decode tiles and sprites from VRAM and composite at output resolution. Hi-res comes from evaluating the affine matrix per output pixel. Rough size is 2,500–3,500 lines, against about 4,500 upstream.

- **Per-line state** is recorded on the emulator thread when `LineJob` is built, into a texture read with `texelFetch`. Upstream's 30 KB UBO is over GLES's guaranteed 16 KB.
- **Memory:** VRAM as `R8UI` textures, uploaded from the dirty sets the HBlank flat copies already compute (a second tracker can't derive on its own, because `DeriveState` clears the bits). About 1.1 MB at most per frame, plus palette and OAM. Backdrop color changes get their own dirty bit, from upstream.
- **Mid-frame changes:** flats only hold the latest state, so either stop and draw a band or fall back to CPU for the rest of the frame. Step 1's numbers pick the rule.
- **Eligibility per line:** a control-word bit picks GPU or CPU, so features can land one at a time: text BGs and normal sprites first, then affine BGs, affine sprites, windows, blending, mosaic and bitmaps.
- **Checking:** a `check` mode at 1x reads the GPU result back and compares it with the CPU planes, the way `debug.melonade.hirescap check` does. The shaders can't run off-device.
- **Mobile cost:** fewer render target switches than upstream, `glInvalidateFramebuffer`, and no synchronous readback outside `check`. Upstream's OBJ mosaic costs about 48 fetches a pixel, so it can stay on CPU at first.

Opt-in. On compute it adds load to a GPU that's already maxed out on the OnePlus. Hi-res capture [Phase 2](hires-capture.md#phase-2-bitmap-bg-consumers) (bitmap BGs) likely joins this path, since the shader can sample bank images directly.

## Step 4: wide 2D

The shader draws X outside 0..255. Today wings are 3D only, and 2D-only lines are black. Two problems have no safe automatic rule: tilemaps wrap and show junk in the wings, and sprite X is 9-bit, so a sprite parked offscreen looks the same as one in a wing. Expect a per-game opt-in, possibly from Lua ([MODS.md](../MODS.md)). Capture stays at the middle 256.

## Later, if it earns it

Skipping CPU lines to save time. That's a separate decision, once GPU output has proven it matches. It would need the consumers of the CPU pixels handled: `DoCapture`, the hi-res stream, HASH2D, software 3D, the wing bit's window mask, and plane 1 carried between engines.
