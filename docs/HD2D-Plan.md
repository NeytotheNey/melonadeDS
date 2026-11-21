# Plan: an HD-2D look for DS games

Started 11-21. Phase 1 (bloom and vignette) is built and runs on the Fold (below), not committed yet; the rest is plan only. Open decisions are marked **Decide**.

## The goal

HD-2D is the look from Octopath Traveler and the Dragon Quest remakes: crisp pixel-art characters in a 3D world, with depth of field that blurs the near and far parts of the scene, bloom on bright spots, soft light, a vignette and strong color grading. A lot of DS games are already halfway there. The Pokémon field maps are 3D models with flat characters standing in them, and the camera looks down at an angle, so the same post effects should carry over well.

The aim is an opt-in look per game, driven by a Lua mod the way tilt-shift is today, that runs on the phone and the desktop app alike. The game never sees any of it.

Decided 11-21: the first target is the Pokémon White field, since we already know how it draws its maps (below). The reference scene is the city plaza from the user's save (map chunk 10). White 2 and Platinum already have tilt-shift scripts to compare against later.

## What we have already

- **Tilt-shift** (`melon.screen.tilt_shift`, [MODS.md](../MODS.md)): a sharp band across the top screen with a blur that grows towards the edges, plus a saturation and contrast boost. It's two passes after the filter in `VideoFilterPipeline.kt`, on the finished frame, so it blurs 2D text too. This is a fake depth of field: the band is a guess at where the player is, not real depth. Examples are in `docs/examples/lua/tilt_shift_*.lua`.
- **The same passes on the desktop.** Phase 3 step A of [plan-desktop-app.md](plan-desktop-app.md) ported `VideoFilterPipeline` to C++, and `DesktopShaderExportTest` keeps the shaders byte-identical, so new passes written once land in both.
- **A depth buffer.** The OpenGL 3D renderer draws into `DepthBufferTex` (`GL_DEPTH24_STENCIL8`, at the 3D scale) for its edge and fog passes. It's DS depth (Z or W buffer per polygon), not linear distance, and it never leaves the renderer today. Compute keeps depth too: its result buffer (`ResultValue` from `ResultDepthStart` in `GPU3D_Compute_shaders.h`) holds two depth layers per pixel, and the final pass already reads them for edge marking and fog, so it could write a depth image next to `FinalFB` with one more `imageStore`.
- **Hi-res 3D.** The 3D is already drawn at up to 8x, so the world gets sharp while the 2D stays at native resolution, which is half of the HD-2D contrast for free.
- **Pokémon White map knowledge** (from the 11-21 RE session, not written up in the repo yet): the field draws its map chunks and buildings through one display-list send at `0x206dc18`, every send matches a ROM display list by hash, and the overworld runs at 30 fps. That's the hook for swapping in better models later, which is a separate project (see the last phase).

## Phases

### Phase 1: screen-space effects, no depth

Add bloom and a vignette as passes in `VideoFilterPipeline`, next to tilt-shift, with a Lua call like `melon.screen.bloom(strength[, opts])`. Bloom is a bright-pass, a downscaled blur and an add; the vignette is one multiply. Both work on the finished frame like tilt-shift, so 2D gets them too, and they follow the same rules: built on first use, skipped while off, newest script wins, gone when the script stops.

Built 11-21: `melon.screen.bloom()` and `melon.screen.vignette()` ([MODS.md](../MODS.md)), the desktop's `--bloom` flag, and `docs/examples/lua/hd2d_white.lua`. On the desktop it's checked with `--bloom` only, since the desktop doesn't run Lua scripts. Without `--bloom` the desktop's screenshots are byte-identical to before, and with it only the top screen changes.

First look on White's field (OpenGL 4x, the city street from the user's save): the glow, vignette and tilt-shift stack as intended. But a daytime White street has no lamps, and the pale pavement sits around luma 0.8, so a brightness threshold mostly lifts the floor: at 0.6 the street washes out, at 0.8 it's milder but still the floor that glows. The example's threshold default went from 0.7 to 0.8 for that. Picking real light sources (windows, lamps, night scenes) needs more than brightness, which ties into phase 3.

**On the Fold, 11-21** (USB, plugged in; White from the user's save, standing still in Striaton City, WideMelon on at 342 wide; `perf_logs/2026-11-21-hd2d-bloom/`): `hd2d_white.lua` loads with no errors, the top screen gets the glow, vignette and tilt-shift, the bottom screen is pixel-identical on and off, the menu clears it and it fades back. No FPS drop and no missed frames anywhere; the script costs about 0.2 ms of work per frame and a little GPU: +0.6 Mcycles a frame on OpenGL 4x (2.6 → 3.2), +2.3 at 8x (5.7 → 8.0), +0.7 on Compute 4x (5.5 → 6.2), rough numbers since the governor moves clocks. No build or target errors in any run, and the xbr2 and FXAA filters draw it the same way. Not checked yet: the L+R toggle (adb can't hold two touches), a battle, and the vignette on its own during text.

This is cheap to build and shows quickly whether the look is worth the deeper work. Done when a White script with tilt-shift, bloom and vignette runs on the Fold and the desktop with the same picture, and the phone's frame time hasn't moved much (to measure: bloom's blur at 4x and 8x on the Fold).

**Decide:** whether bloom should only touch the 3D layer. On the finished frame it also catches white text boxes, which will glow. Avoiding that needs the 3D layer on its own, which is phase 2's plumbing.

### Phase 2: real depth of field

Swap tilt-shift's fixed band for real depth: the blur follows how far each pixel is from a focus distance. That needs three things:

1. **Depth out of the 3D renderer.** Hand the depth to `GLCompositor` next to the 3D color: OpenGL's `DepthBufferTex` as it is, Compute's from the final pass (above). Both renderers already keep a kept and a spare 3D buffer for the early capture readback, so the depth needs the same pair or it'll belong to the wrong frame.
2. **Depth through to the app.** The app only gets one texture per frame: the compositor's output, both screens stacked, handed over in the frame queue (`PresentFrameWrapper.textureId`). Depth needs a second texture that travels with it, one per frame-queue buffer, laid out the same way (the WideMelon wings and margin rows included). Two things to get right there: with the output cap on (K4) the output is smaller than the 3D, and the compositor's `DOWN3D` programs average the 3D pixels under each output pixel, which is wrong for depth, so depth should take the nearest one instead. And pixels where 2D is on top need a rule: they stay sharp (text, menus) or take the depth of the 3D under them. The compositor knows which layer won each pixel, so it can write that in the same texture.
3. **A focus point.** The Lua script gives the focus, either as a depth or as a screen point to sample, such as the player's position on screen. Whether a mod can find that position for White isn't known yet.

**Checked 11-21: White's field characters are 3D.** On the desktop (OpenGL, booted from the user's save into a city street), the player's pixel edges at 4x land on all four sub-pixel positions, the same as the buildings and trees, where a 2D sprite's would all sit on the 4-pixel grid. So the characters are polygons in the 3D scene, they have depth, and the blur will treat them like the rest of the world. Their texels stay blocky at any scale, which is the pixel-art half of the look already. The bottom screen in the field is all 2D.

Done when the White plaza from the user's save shows a sharp player and nearby buildings with the far edge of the map blurred, and the blur follows the camera as it moves, on OpenGL at least.

### Phase 3: light

The harder half of the look: soft shadows, light shafts and glows on lamps and windows. Some of it can come from depth alone (screen-space ambient occlusion, light shafts from a bright spot), some only from knowing the scene (which polygons are lamps). Not planned yet; phase 2's depth decides what's possible.

### Phase 4 (separate plan): better assets

Swapping the game's map models and textures for higher-detail ones through the display-list hook. This is its own project with its own plan, and HD-2D doesn't need it to look good. Mentioned here so the two don't get mixed up.

## Open questions

- Does the look hold up at 30 fps? The White overworld renders every other frame, so the post effects should run on the new frame only and be reused, or the blur will shimmer with frame interpolation on.
- Where WideMelon is on, the wings are part of the 3D, so depth of field there should just work. Not checked.
- Cost on the slower phones (OnePlus 7 Pro, Shield). Bloom and a depth blur at 8x are full-screen passes; they may need to run at a lower scale.
- Software 3D has no depth texture to hand over, so phase 2 is OpenGL and Compute only.
