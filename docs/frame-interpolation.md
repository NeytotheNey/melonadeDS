# Frame interpolation

Status (2026-11-17): the halfway-camera approach works and is merged. It's the "Smooth 30 FPS games (experimental)" setting under Frame pacing in all builds, which runs mode 4 with no logging, on the OpenGL or compute renderer (grayed out for Software). Debug builds also have `debug.melonade.halfcam` (modes 1–5, see [debug-switches.md](debug-switches.md)); when set it replaces the setting, so `0` forces it off. The code is `GPU3D_HalfCamera.cpp` in the core. See "Halfway-camera prototype" below; the sections before it are the older attempts from 2026-10-31, kept for why they failed.

## Why White 2 looks choppy at 1x

The overworld is 30 FPS by design, so it can't be unlocked with a code patch. This comes from static RE of IRDO rev 0, which matches the public pokebw2 decomp's w2_us symbols:
- `Field_CallRoutines` (ov036 `0x021803d4`) switches between `FieldRoutine_Run` and `FieldRoutine_RunAltFrame` every VBlank. Run does all the logic plus the first half of the 3D. RunAltFrame does the second half plus `GFL_G3DSysReqSwapBuffers`.
- `GameData_Set30FPSMode` is set on map spawn.
- Nothing takes a delta time, so a 60 FPS unlock doubles game speed. The Gen 4 limiter patch does the same: users report it runs about 2x. Battles are already 60.

Mario Kart DS draws 3D at 60 FPS in races, so it never needs interpolation.

## Older attempts (2026-10-31, reverted)

All built and tested on the Z Fold 8 (compute, 4x, WideMelon on). They were all smooth, but none got rid of the artifacts, so the code was reverted. The local patch copy in `logs/shelved-frame-interpolation/` is no longer on this PC.

Detection worked in every version: a native hash of the 2D output plus the core's `RenderFrameIdentical`, read one frame late, locked onto White 2's A,A,B,B pattern straight away.

- **Blend (frontend):** a 50/50 mix on the repeated frames. Blurry, and a scrolling camera gives a double image rather than motion.
- **Motion (frontend):** 8x8 block matching on luma at DS resolution, then warping both frames halfway. Visible artifacts on the upscaled 3D, about 1.1–1.6 ms on the render thread.
- **3D geometry (core):** matched the previous and new polygon lists in `GPU3D` by a signature (Attr without alpha, TexParam, palette, facing, shadow and w-buffer flags) and rendered an in-between scene with matched vertices halfway. Smooth, but with seams: about 96% of polygons matched, and the rest (mostly entering or leaving the view, or clipped at the screen edge) were drawn shifted next to neighbors drawn halfway.

Gotchas that still apply to any in-between rendering:
- Watch display capture. A capture armed in the VBlank IRQ (after line 192) or after line 215 can copy the in-between picture into VRAM, which is emulated state.
- Never free the interpolation buffers from VBlank. The threaded software renderer can still be reading them after an aborted frame.

## Static RE and logging on White 2 (2026-11-06)

A read-only pass over IRDO rev 0 (names from the pokebw2 symbols, behavior from our own disassembly), then a logging-only switch (`debug.melonade.vertexid`) on the Fold comparing consecutive overworld scenes. What mattered:
- Map blocks are one model each with tiles baked in, and 53% of polygons have a translated twin in the same block, so signature matching gets wrong matches. In the logs those were small, though; the bigger failures of the old matching were coverage collapsing below 75% in 1–2% of moving scenes, and 2–2.6% of polygons changing clip shape between scenes. 34–57% of kept polygons are clipped.
- Props are instanced (`MTX_LOAD_4x3` camera, then `MTX_MULT_4x3` placement, overlay 12 `0x02163848`–`0x02163874`), blocks use `MTX_TRANS`, and characters (`BillboardActorScene_Draw`, `0x0204ebdc`) are quads written by CPU straight to the command ports. Block order follows a VCOUNT budget, so vertex ordinals aren't safe as IDs.
- One matrix load covers 99.4–100% of vertices and its hash changes in 85–89% of moving scene pairs and never when still. That's the camera, sent separately, which led to the halfway-camera idea: re-run the scene with a halfway camera instead of pairing static geometry, and let normal clipping handle the screen edges.

## Halfway-camera prototype (2026-11-06)

The core records scene B's commands and replays them in a second `GPU3D` with the camera blended halfway between A and B. First Fold run (compute, 4x, mode 3, one short walk): 4,091 of 5,324 scenes interpolated, the rest a still camera or before the pattern lock. No crash, no capture conflicts, replay about 0.9 ms on average and 3.6 ms at worst on the emulator thread. The user saw clear smoothing and none of the old seams. A longer run in a larger White 2 area (about 4.5 minutes, more NPCs) had no crash, worst replay 2.1 ms and no notable FPS drop; about 13% of character batches went unpaired with no jitter reported.

Title screens that draw a new scene every frame (White 2, Dark Dawn) never lock onto the pattern and only pay for analysis (0.29 and 0.51 ms per scene).

### Other games and the billboard fix

Platinum and SoulSilver worked with no per-game changes, but mode 3 left NPCs with a subtle jitter when moving horizontally. The cause, from a static read of Platinum's ROM: NitroSystem's billboard commands (BB `0x020b0d7c`, BBY `0x020b1084`) read `CLIPMTX_RESULT` on the CPU and send it back as `MTX_LOAD_4x3`, so the recorded translation holds B's camera, and replay drew those sprites half a step ahead. Mode 4 replaces that translation with the shadow's position matrix and keeps B's rotation and scale. The user confirmed the jitter was gone.

Mode 4 then showed a flashing afterimage behind the player in tall grass, in Platinum and SoulSilver only, most likely from pairing by order and shape, since grass tufts (`kusaeff`) and NPC billboards are identical quads (never proven directly). Pairing now takes the closest pairs first (same palette and format, then format only), with limits of 10 and 8 px, and corrections capped at half the camera step plus 6. The user confirmed the afterimage, the NPC jitter and the character steadiness were all fine on that build. White (IRAO) worked on mode 4 with no billboards at all. Worst replay across these was about 3.2 ms.

### OpenGL (2026-11-15)

`GLRenderer::CanDrawReplayedScene` lets OpenGL draw the in-between scenes, and the setting was opened to OpenGL. Unlike compute, GL still draws an in-between scene that matches the real one, because only compute keeps the scene copy that catches it. On the Fold (OpenGL, 4x, plugged in, a 30 s White 2 walk), `halfcam 3` interpolated 384 of 909 scenes (the rest a still camera), replay 0.84 ms average and 1.51 ms worst, emulator work per frame 7.03 → 7.38 ms, a steady 60.00 FPS, no GL errors or seams. Logs are in `perf_logs/2026-11-15-halfcam-gl/`.

### Still open

- The bike or other fast movement is untested. The pair limits are tuned for walking, and 8 units is exactly the 10 px limit.
- No game outside Pokémon has been tried.
- Real GPU and battery cost.
- A small jitter on thin details like fence slats, only visible in motion. The user saw it with interpolation off too, so interpolation isn't its source; whether it's the game or the renderer at 4x wasn't tested.
- The log's `pairmax` reached 16 units in Platinum, over the pair limit. It's probably a rejected candidate, not an applied pair, but that wasn't checked.
