# Plan: GPU discovery, redundant shading and depth/blend work

Written 11-11 after the WatermelonDS comparison (PERF-NOTES "WatermelonDS Vulkan vs ours"). At 8x on the Fold, White 2's title cost 4.9 M GPU cycles per frame on our OpenGL, 10.5 M on Compute and 2.5 M on WatermelonDS's Vulkan. The plan was to measure where the cycles go, then try cheap experiments behind debug switches. Everything that paid off is now on by default and merged into `material3`; numbers are in PERF-NOTES from "GPU discovery Phase 0" on, switches in `docs/debug-switches.md`.

Code facts below come from a read of both repos on 11-11 (ours at `71608319`, WatermelonDS `9bd6a0db`). Paths are in `melonDS-android-lib/src`.

## What the code read found (11-11)

**Our OpenGL (`GPU3D_OpenGL.cpp`, `GPU3D_OpenGL_shaders.h`)**
- Early-Z can't work. Opaque and translucent share one program with `discard` compiled in (mode is a uniform), and the W-buffer program writes `gl_FragDepth` on every fragment.
- The clear is a full-screen quad writing color, attr, depth and stencil, not `glClear`. Nothing is ever invalidated, so on Adreno's tiled GPU every target gets loaded and stored each frame.
- Every opaque fragment writes the attr target, even when no edge, fog or capture pass reads it.
- Fog and edge are separate full-screen quads, and the depth attachment is detached and reattached mid-frame for them.
- Textures are decoded per fragment from raw VRAM with several `texelFetch` calls. There's no texture cache on this path (Compute has one).
- Polygons draw in DS order, with no scissor, no depth prepass and no occlusion. Translucent polygons draw twice when clear alpha is 0, and "need-opaque" translucent ones draw twice as single-polygon draws.
- Edge marking was broken (same in upstream melonDS): nothing ever wrote the edge flag in attr G, the `GL_LINES` pass that set it was commented out, and the neighbor offset was 1 hi-res pixel instead of one DS pixel. So a game with DISP3DCNT bit 5 paid for a full-screen edge pass that changed nothing. Fixed by E1.

**Our Compute:** moved to [plan-compute-efficiency.md](plan-compute-efficiency.md) (Phase 2, "Why" and "From the GPU discovery plan"). This plan covers OpenGL and the compositor.

**WatermelonDS Vulkan (`W/GPU3D_Vulkan.cpp`)**, what it does that we didn't:
- loadOp CLEAR, and DONT_CARE stores for attr and depth when no edge, fog or capture needs them.
- "NoAttr" pipelines skip attr writes in those frames.
- No `gl_FragDepth` on opaque draws. W-buffer depth is written only for textured, non-decal, non-linear draws. Opaque textures with no transparent texels and polygon alpha 31 skip `discard`.
- A per-draw scissor, a depth prepass for big opaque W-buffer polygons, and reverse-order stencil occlusion when there are no shadows and no alpha blending.
- Edge and fog combined into one full-screen pass, scissored, and skipped when unused.
- A pre-decoded texture cache sampled in hardware.

It manages this while also paying for a second render of the scene at 1x every frame.

## Phase 0: instruments and first run (done 11-11)

Built behind debug switches: `gltimers` (GL per-pass GPU timers, a once-a-second `GLTIME` line), `scenestats` (DISPCNT bits, W vs Z, polygon counts, overdraw), `hash3d` (a per-frame hash of the 3D, aligned like HASH2D, which every experiment is checked with), and a debug-only Diagnostics text field that sets `debug.melonade.*` values at game start, so A/Bs work from APK + logs without adb.

The Fold run at 8x (title and overworld) set the order below: translucent polygons were shaded about twice (41% of the title frame, T1), the compositor was 17–20% of GL's cycles (K1), the dead edge pass was 2–3.6% (E2), the clear quad 4–7% of 3D (D1), and R3 was ruled out since coverage is already 1.0. White 2's overworld has edge marking on in every frame, so it's the E1 test game.

## Phase 1: experiments

Each is a debug switch, A/B'd with `gpu_mcycles_per_frame`, battery and `hash3d`. GL changes need a Fold run before they count: the RTX 5090 and fake GL have missed an Adreno-only bug before.

**Edge marking fix (GL, correctness first)**

| # | Change | Status |
|---|---|---|
| E1 | Make edge marking work. **Done 11-15 (`gledgemark`, default 1, the Video setting "Edge marking" turns it off):** mode 1 = per-polygon outline test in the opaque FS from an edge texture, mode 2 = all opaque pixels flagged. Correct one-DS-pixel outlines on the desktop, Pixel 8 and OnePlus; costs +27–29% GPU cycles in the W2 town on the Pixel, edge pass 1.5 / 2.2 ms on the OnePlus (PERF-NOTES "E1"). Speedup `gledgefast=2` (default): edge lines as varyings for 1–4-edge polygons, OnePlus opaque pass −11%, Pixel neutral at 8x and −9% at 16x; discarding unflagged pixels (bit 0) gained nothing (PERF-NOTES "E1 speedups"). Committed (core 2dc01d4f, 2e98367c). | done |
| E2 | Skip the edge pass and its depth detach for frames whose polygons have no edge flags. **Done 11-12 (`gledgeskip`, default 1):** exact; removes the 0.6 ms edge pass in the W2 overworld. With E1 on (`gledgemark` nonzero) the edge pass always runs. | done |

E1 changes what games look like (it's what real hardware and our software renderer draw). It shipped on by default after the phone checks (11-15), with a Video setting to turn it off.

**Depth/blend and bandwidth (GL)**

| # | Experiment | Status |
|---|---|---|
| D1 | `glClear` instead of the clear quad (bitmap clear stays a draw), and `glInvalidateFramebuffer` on attr and depth when nothing reads them after the frame. **Done 11-12 (`glclear`, default 3):** exact; −4 to −7% GPU at 12x/16x on the Fold and Pixel, −4% at 8x on the Pixel, noise at 8x on the Fold. | done |
| D2 | NoAttr: a program variant that doesn't write attr when no edge, fog or capture pass needs it. Likely ~0 now, since D1 invalidates attr. | small, not tried |
| D3 | One combined edge+fog pass. **Shelved 11-15** ([shelved/d3-edgefog.md](shelved/d3-edgefog.md)): draws the OnePlus's 3D wrong, Mali lacks dual-source blending, and only ~0.26 ms of the ~1.5 ms edge pass was the detach. | shelved |

**Redundant shading (GL)**

| # | Experiment | Status |
|---|---|---|
| T1 | Stop shading translucent polygons twice: skip dead need-opaque redraws, and scan A3I5/A5I3 textures to skip all-opaque or all-translucent draws. **Done 11-11 (`gltrans` bits 0–1, default 3):** exact in every scene tried on both phones, −4 to −15% on the Fold and OnePlus (PERF-NOTES "GL translucent skips, T1", "T1 wider check and T1c"). | done |
| T1c | Remove the clear-alpha-0 pass with framebuffer fetch (`GL_EXT_shader_framebuffer_fetch`). **Kept as an off-by-default switch (`gltrans` bit 2):** mixed, −7% on Dark Dawn to +8% on the Fold's White 2 title, since the fetch translucent pass costs nearly as much as the pass it removes; its hash differences are blend rounding. Mali's ARM variant isn't supported. Untested ideas: fetch only for frames with few translucent fragments, find out why the fetch pass costs more per fragment on Adreno, quantize src alpha to 8 bits, use fetch for shadows over alpha-0 pixels. | off by default |
| R1 | Opaque programs without `discard` for polygons that can't have transparent texels, in Z-buffer frames. **Done 11-13 (`glnodiscard=3`, default):** step 1 (untextured, decal, palettes with color 0 opaque) gave the Pixel −23 to −27% in W2 town, −6% Platinum, Adreno −1 to −8%; step 2 (scanned direct / 4x4 / unused-color-0 textures, scan cached on texture VRAM dirty chunks) adds −1 to −2% in Contra 4 only. Exact on the desktop and all three phones. See "R1 results". | done |
| R2 | Write `gl_FragDepth` only where W-buffer interpolation really needs it. **Shelved 11-11** ([shelved/r2-fragdepth.md](shelved/r2-fragdepth.md)): exact, but almost nothing qualifies. Since `GL_EXT_conservative_depth` isn't on any of our phones, W-buffer frames can't get exact early-Z, so R1 only matters for Z-buffer scenes. | shelved |
| R3 | Per-draw scissor. **Ruled out 11-11:** coverage is already 1.0. | ruled out |
| R4 | Depth prepass or reverse-order occlusion for big opaque polygons, only if early-Z works and overdraw is still high | large, not tried |

**Compositor (both renderers)**

| # | Experiment | Status |
|---|---|---|
| K1 | Time the compositor's parts with `gltimers`. **Done 11-11:** the main draw is a fixed 0.84 Mcycles (19–26%), hi-res capture 0.41 at the title, the filter ~4%, the rest ~0. | done |
| K2a | `glClear` the compositor's output before the main draw. **Done 11-11 (`compclear`, off): no effect on the Fold or Mali, so the draw isn't load-bound.** | off by default |
| K2b | Skip a screen half whose back buffer (frame N−2) already has the right image: hash that half's planes, control words, hi-res capture and bank state, wing hold and shadow lines, plus Scale and ViewWidth, and only for halves with no 3D or no 3D re-render. A still touch screen would skip most frames. | medium, not tried |
| K2c | Draw the 2D-only screen at 1x and let the frontend upscale it with nearest. **Done 11-11 (`comp1x=1`, default):** exact on the Fold, −6% title / −8% overworld, with the None filter and Smooth 2D edges off. | done |
| K3 | Cheaper Smooth 2D edges (`video_filter_2d`). **Done 11-12 (`filter2drows=1`, default):** the map redraws only changed rows, and 2D-only filtered screens draw at the map's 4x (`comp1x`). Exact; −9.9% title, −14% overworld with the filter on. Untried: skip the map fetch on lines with display mode ≥2 or hi-res lines. | done |
| K4 | Cap the output at the screen's scale ("WatermelonDS at 16x" below). **Done 11-13 (`outcap=1`, `outcap3d=3`)**, design and leftovers in [plan-k4-output-cap.md](plan-k4-output-cap.md). | done |
| T2 | Texture cache: decode DS textures once instead of per fragment. **Done 11-14 (`gltexcache=449`, default)**, design in [plan-t2-texcache.md](plan-t2-texcache.md). | done |

## R1 results (11-13)

Counted first (`GLOPAQUE` under `scenestats`, desktop): Pokemon overworlds (White 2 outdoors, Platinum, SoulSilver) are Z-buffer frames with 2–3 screens of opaque overdraw, and 53–85% of it can drop `discard`, mostly palette textures with color 0 opaque. The title screens, interiors, Dark Dawn, Mario Kart and Zelda are W-buffer. No visible CPU cost from the extra ~14 draws and 20–60 program switches. What's left is mostly `color0` cut-outs that really discard. Numbers in PERF-NOTES "R1 count", "R1 step 1" and "R1 step 2".

The `GLOPAQUE` counters need the debug-tools desktop build: `cmake -G Ninja -S desktop -B build-desktop/dbg -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH=/c/msys64/mingw64 -DCMAKE_CXX_FLAGS=-DMELONADE_DEBUG_TOOLS -DCMAKE_C_FLAGS=-DMELONADE_DEBUG_TOOLS` with `/c/msys64/mingw64/bin` on PATH.

## WatermelonDS at 16x: what they do differently (11-12, code reading)

Read from `D:\Dev\WatermelonDS`, not measured. Their "16x" really rasterizes 3D at 4096x3072, but **everything after the 3D pass runs at the screen's size**: the app caps the output scale at min(ceil(surfaceW/256), ceil(surfaceH/192)), halving while above it (their `MelonInstance.cpp:3256-3284`). The 3D is linear-blitted once to an output-size snapshot (`VulkanOutput.cpp:9651-9722`), then 2D, compositing, hi-res capture and the frame image all run at the output size. That's why their cost only doubles from 8x to 16x. That's what K4 copied.

Their 3D frame is otherwise close to ours: RGBA8 color and attr, D24S8, no MSAA, no transient attachments, one render pass with CLEAR loads and DONT_CARE stores for attr/depth when nothing reads them (what D1 does now), edge+fog in one scissored pass only when needed.

## GPU gap, again (11-14, Fold inner, W2 title, 8x)

We were at 9.8 Mcyc a frame against WatermelonDS Vulkan's 4.5. The ~6.9 Mcyc outside GLTIME was the frontend Deband pre-pass, run over the whole 3408x3088 frame texture every new frame: deband off gave 3.53. Ruled out on the way: widepolys, scenestats, SurfaceFlinger/HWC, the UI layer, the tilt-shift mod, clock inflation of the 3D passes. The fixes are in [plan-deband-outcap.md](plan-deband-outcap.md) and both are done (11-15): A, deband in the screen shader (Fold inner 8x 9.79 → 6.34 Mcyc, Pixel 8.09 → 6.45, looks the same); B, integer output scales that don't divide the 3D scale (8x → 6x), on only while deband is on (Fold 6.31 → 5.71). Deband is now the single "Smooth gradients" setting, which also turns on Compute's dithering. With deband off, 256 view: GL 8x 2.43 vs WatermelonDS 4.52, GL 16x 6.29 vs 7.10, Compute 8x 7.68. PERF-NOTES "GPU gap vs WatermelonDS, again". Use the 256 view for fair comparisons.

Not explained: the last ~0.8 Mcyc untimed, and graphics memory at 588 MB in the 426 view / 525 in the 256 view vs 396 on 11-11 (EGL mtrack 139 MB; deband doesn't move it).

## Later: memory

WatermelonDS's Vulkan runs 16x, which is the bar. On the Fold (11-11) their VK 8x used 620 / 1441 MB (graphics / total PSS) and VK 16x 1072 / 1887 MB, against our OpenGL 8x 396 / 1280 MB and Compute 8x 864 / 1750 MB. Before K4 we used ~1.9 GB PSS / ~1.0 GB graphics at 16x on the Fold; with the cap, White 2 at 16x is ~1.3 GB / ~0.5 GB (Smooth 2D on: ~2.1 / ~1.2). Remaining big items: the GL 3D targets (~250 MB at 16x) and Compute's work buffers (805 MB at 16x, why Compute is capped at 8x), the latter in [plan-compute-efficiency.md](plan-compute-efficiency.md), stages 2 and 5. Not started.

## Left open

- **Next 3D lever**, from the Pixel counters: the shader's CVT/integer math (73–83% of core cycles), and varyings at 16x (91%). What's left of the texture cost after T2: 4x4 and direct textures (record path) and the compositor. Next for Compute: Phase 2 in plan-compute-efficiency (visibility pass), not started.
- **E1:** not checked on the Fold, in a W-buffer edge game, or the depth-equal unbias on a real scene; wireframe polygons are still not flagged.
- **R1:** a Platinum overworld state on a phone (the desktop showed 0.44 screens of new no-discard area there).
- **T2:** see [plan-t2-texcache.md](plan-t2-texcache.md) section 11.
- **WideMelon `widepolys`:** Shroom Ridge needs a Grand Prix race to show drops.
- **K4:** the leftovers are in plan-k4-output-cap.md "Not done". Not there: nobody read the `OUTCAP:` lines or looked for GL errors during a live fold/unfold with Smooth 2D on (the picture came back each time; that path covers the GPU clear of new frame textures and the Smooth 2D map kept across Sout changes). With Sout below the map scale (e.g. 4x 3D shown at 2x) every pixel takes the exact per-sub-pixel Smooth 2D path, about the uncapped cost.
- **Per-mask stencil clears:** the one difference from WatermelonDS's 16x frame (11-12 code reading) not tried yet, maybe a few percent.
- **The Fold's GLTIME total** stays ~11 ms in White 2 at 16x while the compositor gets 1.5 ms cheaper with the cap.
- **Highp compositor on the Fold** (PERF-NOTES "Highp compositor"): above 8x the compositor's plain program is built highp. The panel shows ≤ ~5.7x, so a precision cap above that can't be seen (or ruled out) without a framebuffer readback; the bottom-screen fp16 suspicion stays inconclusive for the same reason.
- **The Pixel's app logs** have a CRASH_NATIVE exit report from 05:43 on 11-12, from before the benchmark runs, not looked into.

## Known, left alone

- In pass 2, a matching-ID shadow turns blending on, and the next non-needopaque translucent polygon in pass 2 then blends when it shouldn't. Pre-existing; T1 keeps the same behavior.
- Pass 3 can blend an earlier translucent polygon over a later needopaque polygon's opaque pixel when the earlier one doesn't write depth (GL draws pass 2's results first). Pre-existing, not checked against software. Both are gone with `gltrans` bit 2 on a GPU with framebuffer fetch.
- Frames with a nonzero clear alpha still clear stencil to 0xFF, so translucent ID 63 can't draw over the clear plane there. `CountCoveredPixels`' `NOTEQUAL 0x7F` relies on that value. D1's `glClearBuffer` keeps the same stencil ref, so T1c's 0xBF and `CountCoveredPixels`' 0xFF still hold.
- 12x and 16x: both renderers fall back to the largest scale that fits the GPU limits (`FitScale`, logs a warning), and Compute is capped at 8x in `RendererConfiguration.kt`. The user wants the options left as they are, even though the UI shows the setting rather than the scale used (Compute "16x" runs 8x).
- Desktop: Mesa GLES contexts (Intel/AMD) may list the EXT framebuffer-fetch extension, so the desktop build isn't always on the fallback. The startup log line shows it.

## Phone test notes

- Phone runs go to the `phone-tester` agent (quick recipe by default).
- Pixel 8 (Mali-G715) is in every benchmark, at 8x. GPU cycles: `adb shell gpu_counter_producer -f &`, then perfetto `gpu.counters` with explicit `counter_ids` (1 = any queue active cycles; parser `perf_logs/2026-11-12-pixel8-scale16/scripts/gpuff.py`). GL holds 60 at 8x, so pair it with fast-forward at unlimited speed when fps matters. GLTIME doesn't work on Mali (disjoint queries). The Pixel's ROMs are in `Documents/2DS`.
- The Fold's and Pixel's real prefs have `enable_perf_logging`/`enable_debug_log` off, so perf runs must set and restore them.
- Before pushing any app file to a phone (rom_data.json, prefs), force-stop the app, or it writes its own copy back. A K4 run once lost White 2's `wideViewWidth` 342 on the Fold that way.
- Loading a state overwrites the real `.sav`: back it up and restore it around every run.
- Never use `cmd device_state` on the Fold: an override that wasn't reset on 11-13 left the inner screen black until the user folded and unfolded it by hand. If a run needs a fold or unfold, ask the user to do it.
- Scratch left from the 11-12 to 11-15 runs (deletes were blocked): scripts, prefs backups, screenshots and logs in `/data/local/tmp` on all three phones, `.pftrace` files in `/data/misc/perfetto-traces`, and many `perf_202611*` logs in the Fold's Documents/2DS/Logs and the Pixel's `files/perf_logs`. Script quirks if they're reused: `foldrun.sh` doesn't set the resolution (the Fold's saved 16x leaks in), `pxrun.sh` doesn't add a missing `debug_switches` key like `oprun3.sh` does, and the chain scripts' retry hits LABEL_USED. On the PC, safe to delete: `logs/nsmb-flicker/` (~1.4 GB of dumps), `perf_logs/2026-11-14-t2-step4b/oneplus/pl/` (250 MB) and `pixel/pxs_tmp/` (106 MB), `perf_logs/2026-11-13-t2-diag/fold/logs_all/` (240 MB), `perf_logs/2026-11-13-r1-step2/sav/` (4.6 MB), `perf_logs/2026-11-14-gpu-gap/fold/ztmp`.
