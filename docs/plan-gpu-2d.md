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

### Results (OnePlus 7 Pro, 11-07, uncommitted on `gpu-2d`)

Built, reviewed and phone-tested, with no bugs found. The Fold hasn't run it.

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

Compute 4x on White 2 went from 29.7 to 25.6 fps. On OpenGL the GPU stayed near 267 of about 574 M/s, so it's not simply out of GPU. The tester's guess, not measured: the next frame's capture readback waits behind the heavier compositor. Data: `perf_logs/2026-11-07-filter2d/`.

Next for this step: find where White 2's time goes (`capreads` timing on vs off), then make it cheaper. One option is to skip the passes when the 2D planes didn't change. Another is a cheaper second pass.

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
