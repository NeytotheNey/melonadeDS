# Plan: GPU discovery, redundant shading and depth/blend work

Written 11-11 after the WatermelonDS comparison (PERF-NOTES "WatermelonDS Vulkan vs ours"). At 8x on the Fold, White 2's title costs 4.9 M GPU cycles per frame on our OpenGL, 10.5 M on Compute and 2.5 M on WatermelonDS's Vulkan. Compute also draws 2.3x OpenGL's battery. This session is discovery: measure where the cycles go, then try cheap experiments behind debug switches. Nothing ships from it directly.

Code facts below come from a read of both repos on 11-11 (ours `compute-efficiency` at `71608319`, WatermelonDS `9bd6a0db`). Paths are in `melonDS-android-lib/src`.

## What the code read found

**Our OpenGL (`GPU3D_OpenGL.cpp`, `GPU3D_OpenGL_shaders.h`)**
- Early-Z can't work. Opaque and translucent share one program with `discard` compiled in (mode is a uniform), and the W-buffer program writes `gl_FragDepth` on every fragment.
- The clear is a full-screen quad writing color, attr, depth and stencil, not `glClear`. Nothing is ever invalidated, so on Adreno's tiled GPU every target gets loaded and stored each frame.
- Every opaque fragment writes the attr target, even when no edge, fog or capture pass reads it.
- Fog and edge are separate full-screen quads, and the depth attachment is detached and reattached mid-frame for them (likely a tile resolve on Adreno, unmeasured).
- Textures are decoded per fragment from raw VRAM with several `texelFetch` calls. There's no texture cache on this path (Compute has one).
- Polygons draw in DS order, with no scissor, no depth prepass and no occlusion. Translucent polygons draw twice when clear alpha is 0, and "need-opaque" translucent ones draw twice as single-polygon draws.

**Our Compute (`GPU3D_Compute.cpp`, `GPU3D_Compute_shaders.h`)**
- Rasterise shades every pixel of every (polygon, tile) work tile, including the texture sample, with no depth rejection. That's where the 5.3 invocations per screen pixel come from.
- DepthBlend walks every polygon binned to its tile, per pixel. It reads color first, then depth only if the color is non-zero, so it reads color even for pixels that fail the depth test.
- A depth prepass was named as the big lever in `docs/plan-compute-efficiency.md` and never tried.

**WatermelonDS Vulkan (`W/GPU3D_Vulkan.cpp`)**, what it does that we don't:
- loadOp CLEAR, and DONT_CARE stores for attr and depth when no edge, fog or capture needs them.
- "NoAttr" pipelines skip attr writes in those frames.
- No `gl_FragDepth` on opaque draws. W-buffer depth is written only for textured, non-decal, non-linear draws. Opaque textures with no transparent texels and polygon alpha 31 skip `discard`.
- A per-draw scissor, a depth prepass for big opaque W-buffer polygons, and reverse-order stencil occlusion when there are no shadows and no alpha blending.
- Edge and fog combined into one full-screen pass, scissored, and skipped when unused.
- A pre-decoded texture cache sampled in hardware.

It manages this while also paying for a second render of the scene at 1x every frame.

**Edge marking is broken on GL (confirmed 11-11, same in upstream melonDS).** Nothing ever writes the edge flag: `oAttr.g` is 0 in the clear, bitmap clear and opaque shaders (`GPU3D_OpenGL_shaders.h:54, :117, :792`), the translucent and mask passes mask attr G off, and the only pass that set it, a `GL_LINES` pass with `glColorMaski(1,F,T,F,F)`, is commented out (`GPU3D_OpenGL.cpp` ~:1111–1138). Its edge indices are still built every frame (~:838–856) and never drawn. The edge shader returns 0 unless `attr.g != 0` (shaders :189), so when a game sets DISP3DCNT bit 5 we pay for a full-screen pass, two fetches and a blend per pixel, plus the depth detach, for no change. Its neighbor offset is also fixed at 1 hi-res pixel (:181) instead of one DS pixel. Upstream: `3DRenderFS.glsl:109`, `3DFinalPassEdgeFS.glsl:51`, the line pass commented out at `GPU3D_OpenGL.cpp:940–963`. Software marks edges from polygon edge flags (`GPU3D_Soft.cpp:1470–1478`). The fix is E1 below; until then, Phase 0's GL baseline includes the dead pass.

## Phase 0: instruments (one build, device-free checks)

1. **GL per-pass GPU timers.** `BeginGPUTimer` is a no-op on the GL renderer. Wire it around clear, opaque, translucent, edge, fog, capture and the compositor, logging a once-a-second `GLTIME` line. Timer queries read zero for compute on the Adreno 640; graphics passes on the Fold are untested. If they read zero too, fall back to shares with a `glFinish` per pass, like `computesync`.
2. **Scene stats per second, both renderers:** DISPCNT bits (fog, edge, alpha blend, AA), W vs Z, opaque and translucent polygon counts, and screen-area sum ÷ screen pixels (rasterized overdraw, from the CPU's vertex data). Then use `GL_SAMPLES_PASSED` per pass to count fragments that survive, which gives rasterized vs kept fragments.
3. **Compute:** try `computetimers` on the Fold first (only the 640 is known broken). Add a DepthBlend counter for polygons visited per pixel and how many fail the depth test.
4. **3D pixel check:** a `hash3d` switch that hashes the 256x192 downscale (GL) or the low-res capture image (Compute) each frame, aligned the way HASH2D is. Every experiment needs it, since `HASH2D` only covers 2D.
5. **Remote switches.** `setprop` needs adb, and the user often tests remotely. Add a debug-only text field in Diagnostics that sets the same `debug.melonade.*` values at game start, so A/Bs work from APK + logs alone.

Size: about 300–500 lines in the core and app, all behind debug switches. Device-free checks: build, unit tests, the PC harness for `hash3d` if it builds there.

## Phase 0 phone run

Fold, plugged in (labeled), GL 8x and Compute 8x, perf logger plus `GLTIME`/`COMPTIME` and scene stats:
- White 2 title (cold boot, no state load);
- White 2 overworld;
- the heavy Pokémon menu where WatermelonDS felt smoother, played by hand.

If the user is home with adb, add one perfetto `gpu.counters` pass per scene (shader ALU busy, texture busy, GMEM and system memory read/write bytes). That answers whether we're bound by ALU (shading) or bandwidth (depth, blend, loads and stores), which picks the order below. Without adb, the per-pass times have to stand in for it.

Output: a table per scene of ms or share per pass and overdraw (rasterized vs kept), written up in PERF-NOTES.

## Phase 0 results (11-11)

Done on the Fold at 8x, title and overworld (the heavy menu wasn't reached). The numbers are in PERF-NOTES "GPU discovery Phase 0". What they say about the list below:

- **New, biggest lever: T1 below.** On GL, translucent polygons are shaded about twice, and with clear alpha 0 the whole translucent pass runs again. Together that's 41% of the title frame.
- **R2 before R1.** (Later corrected: only White 2's title and interiors are W-buffer; Pokemon overworlds are Z-buffer, see "R1 results".) The W-buffer is on in every frame, so `gl_FragDepth` blocks early-Z everywhere. R1 gains nothing until R2 lands. Opaque shades 2.1 fragments per covered pixel, so the upper bound is about half the opaque pass (~12% of the title frame).
- **New: K1 below.** The compositor is 0.8–0.9 Mcycles per frame in every scene, 17–20% of GL's cycles.
- **Compute:** C2 and C3 are favored, since 27–38% of the pixels DepthBlend visits fail depth and only 39% contribute. C1 is about neutral: it saves color reads but adds depth reads for pixels with no color.
- **E2 is worth doing now.** In a fog area (extra run) the dead edge pass is ~3.6% of GPU cycles and fog ~4.9%; in the plain overworld edge is ~2%. D3's extra gain over E2 is small (about 1–3%, unmeasured) until E1 makes the edge pass real, when merging saves a full-screen read. So: E2 first, D3 after E1.
- **R2 isn't universal.** The fog area uses the Z-buffer, so early-Z there is blocked only by `discard` (R1), not `gl_FragDepth`. R1 and R2 both matter, depending on the scene.
- **Small:** D1 (clear quad, 4–7% of 3D). R3 is ruled out, since coverage is already 1.0.
- **No verdict on D2 or the texture cache:** cost tracks shaded fragments evenly, which looks shading-bound. Telling texture decode from ALU needs perfetto counters, which need adb.
- **E1 test game:** White 2's overworld has edge marking on in every frame.
- COMPTIME doesn't work on the Fold; compute pass shares need `computesync`.

## Phase 1: experiments

Each is a debug switch, A/B'd with `gpu_mcycles_per_frame`, battery and `hash3d`. GL changes need a Fold run before they count: the RTX 5090 and fake GL have missed an Adreno-only bug before. Ordered by size; Phase 0's results can reorder them.

**Edge marking fix (GL, correctness first)**

| # | Change | Size |
|---|---|---|
| E1 | Make edge marking work. Write an edge flag into attr G on opaque polygon outlines, either by bringing the line pass back (an edge mode in the render shader writing G=1, depth LEQUAL instead of ALWAYS, lines about ScaleFactor wide) or with a per-fragment outline test (needs each polygon's outer edges, since its triangles' inner diagonals mustn't count). Pass the neighbor offset as a uniform equal to the scale factor (screen width ÷ 256 is wrong with WideMelon). Check against software with `hash3d` on a game that uses edge marking (find one with `scenestats`' `edge=` field), and time it with `gltimers`. | medium |
| E2 | While E1 is open, or for frames whose polygons have no edge flags: skip the edge pass and its depth detach. Folds into D3 if that lands first. **Done 11-12 (`gledgeskip`, default 1):** exact; removes the 0.6 ms edge pass in the W2 overworld. | done |

E1 changes what games look like (it's what real hardware and our software renderer draw), so it ships as a fix behind a debug switch first, then a phone check of the edge-marking game and the White 2 title for cost.

**Depth/blend and bandwidth (GL)**

| # | Experiment | Size |
|---|---|---|
| D1 | `glClear` instead of the clear quad (bitmap clear stays a draw), and `glInvalidateFramebuffer` on attr and depth when nothing reads them after the frame. **Done 11-12 (`glclear`, default 3):** exact; −4 to −7% GPU at 12x/16x on the Fold and Pixel, −4% at 8x on the Pixel, noise at 8x on the Fold. | done |
| D2 | NoAttr: a program variant that doesn't write attr when no edge, fog or capture pass needs it | small |
| D3 | One combined edge+fog pass, scissored to the drawn area, skipped when unused, with no mid-frame depth detach (what WatermelonDS does, see "WatermelonDS at 16x") | medium |

**Redundant shading (GL)**

| # | Experiment | Size |
|---|---|---|
| T1 | Stop shading translucent polygons twice. Collapse the need-opaque double draw (opaque then translucent, `GPU3D_OpenGL.cpp` ~:1412–1427) and the clear-alpha-0 repeat of the translucent pass (~:1157–1262) into fewer passes, keeping software's output (check with `hash3d` against today's GL and with screenshots against software). Phase 0: 41% of the title frame. **Done 11-11 (`gltrans` bits 0–1, default 3):** exact, −4 to −15% on the Fold and OnePlus (see "T1 results"). | medium |
| T1c | Remove the clear-alpha-0 pass: with framebuffer fetch (`GL_EXT_shader_framebuffer_fetch`, which the Fold has), the translucent shader reads the destination and skips blending where its alpha is 0, so pass 3 alone gives pass 2's result. Shadows over the clear plane and the stencil marks pass 2 leaves are the hard part. Fallback to today's two passes without the extension. Check with `hash3d` against `gltrans=3`. **Kept as an off-by-default switch (`gltrans` bit 2):** mixed, −7% to +8% (see "T1 results"). | medium |
| R1 | Compile separate opaque programs without `discard` for polygons that can't have transparent texels (untextured, or a texture with no alpha-0 texels and polygon alpha 31) **Step 1 done 11-13 (`glnodiscard`, default 1):** untextured, decal and palette-without-transparent-color-0 polygons in Z-buffer frames; exact on the desktop and three phones, Pixel −6 to −27% GPU cycles, Adreno −1 to −8% (see "R1 results"). **Step 2 done 11-13 (`glnodiscard=3`, the default since 11-13):** direct, 4x4 and unused-color-0 palettes, scan cached on texture VRAM dirty chunks; exact on the desktop and in 6 phone pairs, Contra 4 −1 to −2% (its opaque pass −15% on the Fold), nothing measurable elsewhere. | medium (needs per-texture opacity) |
| R2 | Write `gl_FragDepth` only where W-buffer interpolation really needs it (WatermelonDS's rule), checked against software with `hash3d` **Shelved 11-11** (`docs/shelved/r2-fragdepth.md`): exact, but almost nothing qualifies (see "R2 results"). | small to medium |
| R3 | Per-draw scissor **Ruled out 11-11:** coverage is already 1.0 (Phase 0). | small |
| R4 | Depth prepass or reverse-order occlusion for big opaque polygons, only if R1 and R2 show early-Z is working and overdraw is still high | large |

**Compute**

| # | Experiment | Size |
|---|---|---|
| C1 | DepthBlend reads depth first and skips the color read when the depth test fails | small |
| C2 | Per-tile Hi-Z: min/max opaque depth per tile from a cheap pass, so Rasterise skips pixels behind it | medium to large |
| C3 | Per-tile occlusion from binning: skip work tiles fully covered by a later opaque polygon | medium |

**Compositor (both renderers)**

| # | Experiment | Size |
|---|---|---|
| K1 | Time the compositor's parts (screen shadow, filter 2D, hi-res capture, wing hold, the capture variant) with `gltimers` and cut what's redundant. Phase 0: 0.8–0.9 Mcycles per frame everywhere, plus 1.3 ms for the capture variant at the title. **Done 11-11:** the main draw is a fixed 0.84 Mcycles (19–26%), hi-res capture 0.41 at the title, the filter ~4%, the rest ~0. | done |
| K2a | `glClear` (or invalidate) the compositor's output before the main draw, so Adreno doesn't reload ~30 MB of tiles a frame. Redo the padding rows in the same clear. Exact by construction. **Done 11-11 (`compclear`): no effect on the Fold (within 0.5%), so the draw isn't load-bound.** Stays an off-by-default switch for Mali. | done |
| K2b | Skip a screen half whose back buffer (frame N−2) already has the right image: hash that half's planes, control words, hi-res capture and bank state, wing hold and shadow lines, plus Scale and ViewWidth, and only for halves with no 3D or no 3D re-render. A still touch screen would skip most frames. | medium |
| K2c | Draw the 2D-only screen at 1x and let the frontend upscale it with nearest (bit-identical, since the middle is exactly Scale pixels per DS pixel). Only when that screen has no hi-res lines, 3D or wing lines, filter or shadow2D. Needs frontend changes; a blit inside the compositor would still pay the fill. Up to half the main draw. **Done 11-11 (`comp1x`): exact on the Fold, −6% title / −8% overworld, but only with the None filter and Smooth 2D edges off.** | done |
| K3 | Make Smooth 2D edges (`video_filter_2d`) cheaper. It costs ~15% of the GPU at the White 2 title: its passes (~0.18 Mcyc) plus a main draw 0.37 Mcyc heavier than without it. Time its sampling in the main draw and see what can be cached or done at 1x. **Done 11-12:** the passes were ~0.15 Mcyc and the main draw's filter path ~0.42 (identity mode costs the same, so it's per pixel). Now the map redraws only changed rows (`filter2drows`) and 2D-only filtered screens draw at the map's 4x (`comp1x`). Exact; −9.9% title, −14% overworld with the filter on. | done |

**Outside the two themes, but likely big:** a pre-decoded texture cache for GL (Compute already has `GPU3D_TexcacheOpenGL`). Per-fragment VRAM decoding is shading cost. It was skipped in the blackmagic3 review because the GPU was idling at 160 MHz then. At 8x that's no longer true.

## T1 results (11-11)

T1a and T1b are in as `debug.melonade.gltrans` bits 0 and 1. On the Fold at the White 2 title they're pixel-exact (HASH3D matched every frame) and cut GPU cycles per frame from 5.00 to 4.25 (15%): bit 0 10%, bit 1 5%. Numbers are in PERF-NOTES "GL translucent skips, T1". Next: T1c, since the clear0 pass is now the largest remaining piece (~16%). Not yet tested: the overworld and other games, and the OnePlus.

Wider check (PERF-NOTES "T1 wider check and T1c"): T1 is pixel-exact in every scene tried on both phones and saves 4–15%, so it should become the default. T1c (bit 2, `gltrans=7`) works on both phones but doesn't pay off reliably: −7% on Dark Dawn, −1% on the OnePlus title, +8% on the Fold's White 2 title, since the fetch translucent pass costs nearly as much as the clear0 pass it removes. It stays an off-by-default switch.

## R2 results (11-11)

R2 is exact (`gldepth=1`, constant-depth polygons via `glDepthRangef(d,d)`) but has nothing to work on: 0 qualifying polygons in White 2, and under 2% of opaque area in the other games tried, also under WatermelonDS's looser rule. No GPU change on the Fold. `GL_EXT_conservative_depth` isn't on any of our phones, so W-buffer frames can't get exact early-Z, and R1 only matters for Z-buffer scenes. That moves R1 down and makes K1 (compositor, 17–29% of GL cycles in every scene measured), E2 and D1 the next candidates. Numbers in PERF-NOTES "R2". The code is shelved as a patch: `docs/shelved/r2-fragdepth.md`.

## E2 and D1 results (11-12)

Both are exact on the Fold (hash3d on every aligned frame in the White 2 title, overworld and Dark Dawn's fog title) and on the desktop, but close to free on Adreno: E2 removes the 0.6 ms edge pass in the overworld, D1's clear gets 5x cheaper while the opaque pass absorbs the difference, and the invalidate's −2 to −3% is at noise level. The Pixel 8 (Mali) is also exact; its fps holds 60 either way, but its GPU counters (found later through the vendor counter producer) show the cost. After the 12x/16x runs (−4 to −7% GPU on both phones, −4% at 8x on the Pixel) both became the default. Numbers in PERF-NOTES "E2 and D1" and "E2 and D1 on the Pixel 8".

## R1 results (11-13)

Counted first this time (`GLOPAQUE` under `scenestats`, desktop): Pokemon overworlds (White 2 outdoors, Platinum, SoulSilver) are Z-buffer frames with 2–3 screens of opaque overdraw, and 53–85% of it can drop `discard`, mostly palette textures with color 0 opaque. The title screens, interiors, Dark Dawn, Mario Kart and Zelda are W-buffer. Step 1 (`glnodiscard`) covers the classes that need no texture scan and is the default since 11-13: hash3d-exact in 7 phone pairs (Fold, Pixel, OnePlus), Pixel −23 to −27% GPU cycles in White 2's town, −6% Platinum, 0% SoulSilver; Adreno −1 to −8% (the opaque pass itself −6 to −26%); no visible CPU cost from the extra ~14 draws and 20–60 program switches. Numbers in PERF-NOTES "R1 count" and "R1 step 1". Step 2 (`glnodiscard=3`, scanned textures, cached) is exact too but small: Contra 4 −1 to −2%, Platinum's desktop overworld gains 0.44 screens of no-discard area but the phone states don't reach that scene (PERF-NOTES "R1 step 2"). What's left is mostly `color0` cut-outs that really discard.

## WatermelonDS at 16x: what they do differently (11-12, code reading)

Read from `C:\Dev\WatermelonDS`, not measured. Their "16x" really rasterizes 3D at 4096x3072, but **everything after the 3D pass runs at the screen's size**: the app caps the output scale at min(ceil(surfaceW/256), ceil(surfaceH/192)), halving while above it (their `MelonInstance.cpp:3256-3284`). On the Fold the output is 8x for both 8x and 16x (on the cover screen, 4x). The 3D is linear-blitted once to an output-size snapshot (`VulkanOutput.cpp:9651-9722`), then 2D, compositing, hi-res capture and the frame image all run at the output size. That's why their cost only doubles from 8x to 16x.

Ours at 16x runs the compositor's main draw at the full 4096x6176 (25 Mpx), hi-res capture at 16x, 101 MB frame textures, and the frontend samples a 4096-wide texture with `GL_LINEAR` and no mipmaps onto a ~1–2k px screen (which also aliases).

Their 3D frame is otherwise close to ours: RGBA8 color and attr, D24S8, no MSAA, no transient attachments, one render pass with CLEAR loads and DONT_CARE stores for attr/depth when nothing reads them (what D1 does now), edge+fog in one scissored pass only when needed. Left over on our side: the fog pass's depth detach and the per-mask stencil clears, maybe a few percent. Their per-pixel 3D may also be cheaper (pre-decoded texture cache vs our per-fragment VRAM decode, NoAttr/color-only pipelines); that's a guess.

| | Change | Est. saving at 16x | Notes |
|---|---|---|---|
| K4 | Cap the output (compositor, hi-res capture, frame queue, frontend texture) at the screen's scale; downscale the 3D once (2:1 `glBlitFramebuffer` linear, or one bilinear tap per 2x2 block in the compositor, an exact average) **Done 11-13 (`outcap=1`, `outcap3d=3`), phases 1–3** incl. wide view, hi-res capture and Smooth 2D; leftovers are in the handoff. | ~3.5–4 of 12.7 Mcyc (~30%), ~75 MB per frame texture | WideMelon and `comp1x` must follow the cap; a linear blit isn't alpha-weighted like their box filter; hi-res capture of 3D must still read the full 3D where a game captures it. **First pass 11-13 (`outcap`, off by default):** 256 view only (off in a wide view, with hi-res capture, Smooth 2D or gpu2d). Exact mode (`outcap3d=0`, per-sub-sample composite) −9% Pixel GPU at 16x, −740 MB graphics on the Fold; bilinear mode (`outcap3d=1`, inexact) −15% Pixel, Fold compositor 3.03 → 1.29 ms. PERF-NOTES "K4". **Second pass 11-13:** wide view and hi-res capture ported, edge-aware `outcap3d=3`, both on by default; Fold 16x compositor 3.1 → 1.6 ms, graphics −750 MB; Pixel −14%. PERF-NOTES "K4 second pass". **Phase 3 11-13:** Smooth 2D runs under the cap too (only its identity mode and gpu2d turn it off); at 16x with Smooth 2D, Pixel −17% GPU cycles in White 2, Fold compositor −41%. PERF-NOTES "K4 phase 3". |
| T2 | Texture cache: decode DS textures once into GL textures instead of per fragment | unknown, likely the biggest 3D item | large |

## Later: memory

The plan's goal is efficiency, and that includes memory. WatermelonDS's Vulkan runs 16x, which is the bar to beat. On the Fold (11-11, PERF-NOTES "WatermelonDS Vulkan vs ours", graphics / total PSS at the end of the run) their VK 8x used 620 / 1441 MB and VK 16x 1072 / 1887 MB, against our OpenGL 8x 396 / 1280 MB and Compute 8x 864 / 1750 MB. Our 16x hasn't been measured on a phone yet (desktop, 11-12: OpenGL 675 MB, Compute 1882 MB). Known big items once 12x/16x exist: the app's frame queue (up to 9 frame textures, ~101 MB each at 16x, plus a 101 MB zeroed CPU vector on each resize, `FrameQueue.cpp:200`), the GL 3D targets (~250 MB at 16x), and Compute's work buffers (805 MB at 16x, why Compute is capped at 8x in the app). Not started.

## Done looks like

Phase 0's per-pass table for three scenes, the ALU-vs-bandwidth answer if adb is available, and each tried experiment's cycles, battery and `hash3d` result in PERF-NOTES, with the ones that pay off listed for a real implementation.
