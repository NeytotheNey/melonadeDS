# Frame interpolation (shelved)

Tried on 2026-10-31 and set aside. The goal was 2x-fast-forward smoothness at normal speed in 30 FPS games, with Pokémon White 2's overworld as the test case. Every approach was built and tested on the Z Fold 8 (compute renderer, 4x, WideMelon on). They were all smooth, but none got rid of the artifacts, so the code was reverted. A local copy of the patches is in `logs/shelved-frame-interpolation/` (git-ignored, so it only exists on this PC).

## Why White 2 looks choppy at 1x

The overworld is 30 FPS by design, so it can't be unlocked with a code patch. This comes from static RE of IRDO rev 0, which matches the public pokebw2 decomp's w2_us symbols:
- `Field_CallRoutines` (ov036 `0x021803d4`) switches between `FieldRoutine_Run` and `FieldRoutine_RunAltFrame` every VBlank. Run does all the logic plus the first half of the 3D. RunAltFrame does the second half plus `GFL_G3DSysReqSwapBuffers`.
- `GameData_Set30FPSMode` is set on map spawn.
- Nothing takes a delta time, so a 60 FPS unlock doubles game speed. The Gen 4 limiter patch does the same: users report it runs about 2x. Battles are already 60.

Mario Kart DS draws 3D at 60 FPS in races, so it never needs interpolation.

## What was tried

Detection worked in every version. On White 2 it locked onto the A,A,B,B pattern straight away, with 0 skipped frames and clean pacing. The top screen's 3D redraws every other frame, and its 2D almost never changes. Detection was a native hash of the 2D output plus the core's `RenderFrameIdentical`, read one frame late.

- **Blend (frontend):** a 50/50 mix of the two pictures on the repeated frames, one frame of delay. It looked blurry and not smooth, because a scrolling camera gives a double image rather than motion.
- **Motion (frontend):** 8x8 block matching on luma at DS resolution, then warping both frames halfway. It had visible artifacts on the upscaled 3D. Render-thread cost was about 1.1–1.6 ms.
- **3D geometry (core):** in `GPU3D`, the previous and new polygon lists were matched by a signature (Attr without alpha, TexParam, palette, facing, shadow and w-buffer flags), with a 32-polygon window and a resync by screen cell. On the frame a new scene arrived, an in-between scene was rendered with matched vertices halfway. The next frame forced a render of the real scene. Only 3D was delayed, by one frame.
  - First version: smooth, but bad seams. 95% of polygons matched, but about 25 per scene were drawn at the new position next to neighbors drawn halfway. They were mostly polygons entering or leaving the view, or ones clipped at the screen edge.
  - Seam fix: unmatched polygons were moved by half the local motion. That motion came from a 32 px grid of the matched pairs. Old-only polygons leaving the view were re-inserted moved forward. Clipped vertices stayed pinned to the viewport edge. Matching rose to 96.6%, and about 9 of ~270 polygons per scene weren't truly interpolated, nearly all of them shifted. There were still visible artifacts.

## Why it stopped

The leftover artifacts are most likely the limit of matching polygons between two scenes:
- wrong matches between identical tiles, which count as matched, so the stats can't see them
- objects moving differently from their surroundings
- seams where a rigidly shifted polygon meets interpolated ones

One cheap idea was never tried: welding shifted vertices onto matching vertices of their neighbors. It would only fix about 1 px cracks, and the user's artifacts looked bigger than that.

## If someone picks it up again

- Geometry interpolation is the only approach that gave real motion. The hard part is knowing which polygon in one scene is the same polygon in the next. A per-vertex identity, such as tracking matrix stack and display list offsets at submit time, would be more reliable than signature matching.
- Watch display capture. A capture armed in the VBlank IRQ (after line 192) or after line 215 can copy the in-between picture into VRAM, which is emulated state. The shelved version checked for capture at VBlank and again before the line-215 render.
- Never free the interpolation buffers from VBlank. The threaded software renderer can still be reading them after an aborted frame.
