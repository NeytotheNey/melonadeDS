# Plan: an HD-2D look for DS games

Started 11-21. Phases 1 (bloom and vignette) and 2 (real depth of field, OpenGL only) are done and checked on the Fold (below). Phase 3 (shading and light) is planned in five steps; step A (24-bit depth and flags) is built and checked on the desktop and the Fold. Open decisions are marked **Decide**.

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

**On the Fold, 11-21** (USB, plugged in; White from the user's save, standing still in Striaton City, WideMelon on at 342 wide; `perf_logs/2026-11-21-hd2d-bloom/`): `hd2d_white.lua` loads with no errors, the top screen gets the glow, vignette and tilt-shift, the bottom screen is pixel-identical on and off, the menu clears it and it fades back. No FPS drop and no missed frames anywhere; the script costs about 0.2 ms of work per frame and a little GPU: +0.6 Mcycles a frame on OpenGL 4x (2.6 → 3.2), +2.3 at 8x (5.7 → 8.0), +0.7 on Compute 4x (5.5 → 6.2), rough numbers since the governor moves clocks. No build or target errors in any run, and the xbr2 and FXAA filters draw it the same way. From the user's own states the same day: a town and a route show the look, a battle shows none of it, the hold L, tap R toggle works by hand, and a text box keeps only the vignette. Phase 1 is done.

This is cheap to build and shows quickly whether the look is worth the deeper work. Done when a White script with tilt-shift, bloom and vignette runs on the Fold and the desktop with the same picture, and the phone's frame time hasn't moved much (to measure: bloom's blur at 4x and 8x on the Fold).

**Decide:** whether bloom should only touch the 3D layer. On the finished frame it also catches white text boxes, which will glow. Avoiding that needs to know which pixels are 3D; phase 3 step B does it with the depth's flags.

### Phase 2: real depth of field

Swap tilt-shift's fixed band for real depth: the blur follows how far each pixel is from a focus distance. That needs three things:

1. **Depth out of the 3D renderer.** Hand the depth to `GLCompositor` next to the 3D color: OpenGL's `DepthBufferTex` as it is, Compute's from the final pass (above). Both renderers already keep a kept and a spare 3D buffer for the early capture readback, so the depth needs the same pair or it'll belong to the wrong frame.
2. **Depth through to the app.** The app only gets one texture per frame: the compositor's output, both screens stacked, handed over in the frame queue (`PresentFrameWrapper.textureId`). Depth needs a second texture that travels with it, one per frame-queue buffer, laid out the same way (the WideMelon wings and margin rows included). Two things to get right there: with the output cap on (K4) the output is smaller than the 3D, and the compositor's `DOWN3D` programs average the 3D pixels under each output pixel, which is wrong for depth, so depth should take the nearest one instead. And pixels where 2D is on top need a rule: they stay sharp (text, menus) or take the depth of the 3D under them. The compositor knows which layer won each pixel, so it can write that in the same texture.
3. **A focus point.** The Lua script gives the focus, either as a depth or as a screen point to sample, such as the player's position on screen. Whether a mod can find that position for White isn't known yet.

**Checked 11-21: White's field characters are 3D.** On the desktop (OpenGL, booted from the user's save into a city street), the player's pixel edges at 4x land on all four sub-pixel positions, the same as the buildings and trees, where a 2D sprite's would all sit on the 4-pixel grid. So the characters are polygons in the 3D scene, they have depth, and the blur will treat them like the rest of the world. Their texels stay blocky at any scale, which is the pixel-art half of the look already. The bottom screen in the field is all 2D.

Done when the White plaza from the user's save shows a sharp player and nearby buildings with the far edge of the map blurred, and the blur follows the camera as it moves, on OpenGL at least.

**Built 11-21, OpenGL only** (core branch `feature/hd2d-depth`):

- *Depth out of the renderer.* While a frontend gives a depth texture, `GLRenderer` resolves each render's depth into a texture (RGBA8 since phase 3 step A) right after the frame (`ResolveDepth`), before `GLClearFast` drops it. That texture swaps with the color buffer, kept and spare included, so a late Blit reads the depth of the frame it shows. A frame skipped as identical is drawn again when there's no depth for it yet. With no depth texture given, none of this runs and the renderer frees what it had.
- *Through the compositor.* After the frame's draws, `GLCompositor::RenderDepth` draws the top half's depth into the frame's own depth texture, with the main draw's quad and the same scroll, wing and margin maths (`kCompositorDepthFS`). It takes the nearest 3D pixel, not an average, so a capped output is fine too. Its values (24-bit since step A) are 0 where 2D is on top (compmode 1, 5-7, other display modes, hi-res captured lines), which stays sharp, the largest value where the 3D is transparent, which blurs as the farthest, and the depth otherwise. `OutputLayout::HasDepth` says whether this draw wrote it.
- *To the app.* `FrameQueue` makes a per-frame depth texture (the frame's width by its top half's rows) only while `MelonInstance` wants depth, that is, a script asks for it (`LuaEngine::wantsDepth`) or the desktop's `--dof` does. It reaches Kotlin as `PresentFrameWrapper.depthTextureId` (0 for a frame without depth) and the desktop as `FrameView::DepthTexture`.
- *The blur.* `melon.screen.depth_of_field(strength[, opts])` ([MODS.md](../MODS.md)). It runs two separable passes after the filter's own and before the bloom. Each texel's radius grows with its depth's distance from the focus, and a tap only counts as far as its own blur reaches, so sharp things don't smear over blurred ones. The focus is the depth at a screen point (the center by default; if that's 2D, the first 3D 4 DS pixels around it) or a fixed depth.

**Checked on the desktop, 11-21** (OpenGL 4x, White booted from the user's save and pressed through the title into the city street): with `--dof 1,4` the player and the street around her stay sharp, and the roofs nearest the camera blur. With a tight range (`--dof 1,8,0,0.0005`) everything off the player's depth blurs, which shows the depth is right across the picture. White's field is Z-buffered, and everything on screen is within about 0.002 of the player's depth, so the defaults are range 0.0002 and falloff 0.0015. Walking onto Route 2, the "Route 2" banner (2D over the 3D) stays sharp. At `--wide 342` the wings blur with the rest. The raw frame (`--screenshot`) is byte-identical with the depth of field on and off, so the depth pass doesn't touch the picture. App unit tests: 742, all passing. `hd2d_white.lua` now uses the depth of field, with the tilt-shift kept as an option (off by default) for the other renderers.

**On the Fold, 11-21** (USB, plugged in; White in Striaton City from the user's save, WideMelon 342, sharp filter, `hd2d_white.lua` with depth of field, bloom and vignette; `perf_logs/2026-11-21-hd2d-dof/`). The player stays sharp. The Pokémon Center's roof and the buildings along the top and right edges blur softly, more toward the edges, and a trainer near the camera at the bottom blurs too, as depth says it should. The bottom screen is pixel-identical off and on. A message box stays crisp over the blurred roofs. Walking for 9 s showed no flicker between sharp and blurred frames, though that was judged from a half-resolution recording. There were no GL, depth or shader errors in logcat. Compute 4x runs the script with only the bloom and vignette, as expected. The output cap was on: no cap at 4x (GPU 2D shadow), 6x output for 8x 3D. Every run held 60 FPS with no missed frames, and the work time moved by 0.1 to 0.2 ms. GPU cost per frame, one pass each, so the numbers are rough:

| | off | on | phase 1 (bloom and vignette only) |
|---|---|---|---|
| OpenGL 4x | 2.6 Mcycles | 3.7 | 3.2 |
| OpenGL 8x (6x output) | 5.8 | 9.5 | 8.0 |
| Compute 4x | | 5.7 | 6.2 |

So the depth of field adds about 0.5 Mcycles a frame at 4x and 1.5 at 8x on top of the bloom, with room to spare on the Fold.

**Not checked yet:** the slower phones' cost, the late-Blit path (only frames that capture use it, and White's field doesn't), and frame interpolation. Compute and Software give no depth, and Compute could write it from its final pass. The depth's flags say which frames are W-buffered since step A, but the DOF doesn't use them, so a W-buffered game needs its range tuned differently. The defaults are tuned for White's field only.

**Decide:** whether the depth of field should reuse its result on the frames White doesn't render (the overworld runs at 30 fps). It's filtered once per new frame already, but frame interpolation is untested with it.

### Phase 3: light and shading

Planned 11-21. Step A is built and checked on the desktop and the Fold; the rest is plan only. The harder half of the look: soft shadows where things meet the ground, haze in the distance, and glows on lamps and windows instead of on anything bright. Some of it can come from depth alone, some only from knowing the scene. The steps go from cheap and depth-only to scene knowledge, and each one is worth having on its own.

**Why phase 2's depth wasn't enough.** It was the top 16 bits of the DS's 24-bit depth. White's city street fits in about 0.005 of the range, so the whole screen had only 314 depth values. That's enough to pick a blur radius, but shading from it would come out in bands. The full 24 bits give 68,124 values in the same frame (step A). The app also doesn't know the camera, only raw depth, so anything that needs real distances or a light direction needs the projection too (step D).

#### Step A: the full depth and a flags byte

Change the depth texture from RG8 to RGBA8: RGB is the 24-bit depth, A is flags. Bit 0: 2D on top (stays sharp, as 0 is now). Bit 1: transparent 3D (as 65535 is now). Bit 2: the frame is W-buffered, which the core knows from `FlushAttributes` (phase 2 couldn't tell Z from W). The other bits are kept for step E. The DOF shader decodes to the same 0..1 values, so White's DOF defaults keep working. The core changes `kDepthResolveFS`, `kCompositorDepthFS` and the texture format in `ResolveDepth` and `FrameQueue`; the app and desktop share one decode function in `ShaderProgramSource`.

Done when the DOF looks the same as before on the desktop and the Fold, and a test shows the 24 bits arriving (a smooth floor gives smooth values, not steps).

**Built 11-21** (core branch `feature/hd2d-depth24`, from `feature/hd2d-depth`). As planned, with the flags as 1, 2 and 4 in A. The W-buffer flag comes from the frame's first polygon, as the renderer's own stats take it, and rides in the resolved texture's A, so the kept and spare buffers carry it with their frame. The 0 and largest-value sentinels stay, so a shader that only reads depth needs no flags. The resolve gets back the DS's own 24-bit z exactly (`int(depth * 2^24)`: the renderer writes z / 2^24, which the depth buffer stores one lower from 2^23 up), and the app's `decodeDepth` keeps every real depth below 1.0 however the GPU divides, so an opaque far plane at the largest depth still reads as behind the 3D, as before. The desktop has `--depth-shot FILE.png` (desktop/README.md): the last frame's depth as an image, with how many distinct values it holds.

**Checked on the desktop, 11-21** (OpenGL 4x, White's city street from the user's save, as phase 2):
- The street holds 68,124 distinct depth values (0xFE3C9D to 0xFF75E6); its top 16 bits hold 314. The depth image is smooth, with no bands.
- Old and new DOF shots differ on 4.7% of pixels by at most 11/255, from the finer depth moving blur radii slightly; side by side they look the same. The tight `1,8,0,0.0005` shot is the same.
- The raw frame is byte-identical between the old and new builds, and with the DOF on and off.
- Flags: a text box frame marks 16 DS lines as 2D, Mario Kart DS's title marks every 3D pixel W-buffered, New Super Mario Bros' title marks the transparent 3D. WideMelon 342 gets depth in the wings. Compute gives none, as before. White 2's intro is display-captured, so it's 2D, as before.

**On the Fold, 11-21** (USB, plugged in; the same scene and scripts as phase 2; `perf_logs/2026-11-21-hd2d-depth24/`): the look matches phase 2's shots (player sharp, roofs and edge buildings blurred the same, no bands), a message box stays crisp over the blur at 4x, and logcat has no GL, shader or Lua errors. Every run held 60 FPS. The cost is phase 2's within noise: OpenGL 4x on 3.8 Mcycles a frame (3.7), 8x on 9.7-9.9 (9.5), 8x off 5.7 (5.8), Compute 4x on 5.7 (5.7). One run with the final build had a single 47 ms frame in 1800, with nothing in the logs near it; the other runs had none. Not checked on the Fold: the message box at 8x.

#### Step B: haze and 3D-only bloom

Two small changes that ride on passes we already have.

- **Haze.** HD-2D fades the far part of the scene towards a pale color. `melon.screen.haze(strength, { color, start, falloff })` mixes the 3D towards `color` as its depth goes past `start`, in the same raw depth units as the DOF. It's folded into bloom's combine pass, which already reads every texel once, so it costs almost nothing.
- **Bloom on the 3D only.** The flags byte says which pixels are 2D, so bloom's bright pass can skip them. White text boxes stop glowing, which settles phase 1's **Decide**: a `only_3d` bloom option, on by default when depth is there, and the old behavior on Compute and Software.

#### Step C: depth shading (soft contact shadows)

Darken the places where something sits in front of what's behind it: the base of walls, under roofs, around the player's feet, between trees. This is "unsharp masking the depth buffer" (Luft, Colditz and Deussen, SIGGRAPH 2006): blur the depth, and where a pixel is farther away than its blurred neighborhood, it's in a corner or behind something, so it gets darker. It only needs raw depth, no camera, and the blur is separable like the DOF's, so it's two passes plus a multiply.

To keep it cheap at 8x, the depth blur runs at DS resolution (256 x 192 plus the wings), as bloom's blur does, and the darkening is scaled back up with a depth check so edges don't get a halo. API: `melon.screen.shade(strength, { radius, range, color })`, tuned per game in raw depth like the DOF. It isn't true ambient occlusion, since it doesn't know distances in world units, but with a fixed camera like White's field it should look close.

Done when Striaton City shows soft shading at the base of buildings and under the player, with no halos at 8x and no flicker while walking, and the Fold still holds 60 FPS at 8x with everything on.

#### Step D, only if C isn't enough: the camera

Real ambient occlusion, shadows from a light direction (marching towards the light in screen space) and light shafts need view-space positions, which need the projection. The core has `GPU3D::ProjMatrix`, but that's the geometry engine's current matrix, and a game can change it mid-frame (for example an orthographic menu drawn in 3D last). The core would keep the projection that most of the frame's polygons used and hand it over with the depth. Decide after C whether the look needs it.

#### Step E: lamps and windows

Glow on chosen things instead of on anything bright. A Lua list of textures (by content hash, since VRAM addresses move) that the OpenGL renderer marks as lights: their polygons write a flag into the attribute buffer, and the depth resolve copies it into the flags byte. Bloom then takes flagged pixels instead of bright ones. **Not known yet:** whether White's lamps and lit windows are textures of their own, and how White's field looks in the evening and at night (the user's save is daytime). A spike on a night save comes before any of this.

#### Not in phase 3

- Color grading beyond tilt-shift's saturation and contrast. Add it if the look needs it after B.
- Compute's depth. It fits in once step A settles the format: Compute's final pass would write the same RGBA8 with one more `imageStore`.
- Effects running only on new frames at 30 fps (phase 2's open **Decide**). It matters more with more passes, so measure it with frame interpolation on after C.

**Cost.** Each step gets measured on the Fold like phases 1 and 2. Phase 2 left OpenGL 8x at about 9.5 Mcycles a frame with room to spare. B should add almost nothing, and C about as much as the DOF (+0.5 at 4x, +1.5 at 8x) or less, since its blur runs at DS resolution.

### Phase 4 (separate plan): better assets

Swapping the game's map models and textures for higher-detail ones through the display-list hook. This is its own project with its own plan, and HD-2D doesn't need it to look good. Mentioned here so the two don't get mixed up.

## Open questions

- Does the look hold up at 30 fps? The White overworld renders every other frame, so the post effects should run on the new frame only and be reused, or the blur will shimmer with frame interpolation on.
- Where WideMelon is on, the wings are part of the 3D, so depth of field there should just work. Not checked.
- Cost on the slower phones (OnePlus 7 Pro, Shield). Bloom and a depth blur at 8x are full-screen passes; they may need to run at a lower scale.
- Software 3D has no depth texture to hand over. Phase 2 is OpenGL only for now; Compute could hand its depth over from its final pass.
