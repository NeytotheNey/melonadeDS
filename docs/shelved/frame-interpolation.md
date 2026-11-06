# Frame interpolation

Update 2026-11-06: a new approach worked and is merged behind `debug.melonade.halfcam` (debug builds, compute only). Use mode 4. See "Halfway-camera prototype" below. The sections before it describe the older attempts.

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

## Static RE of White 2's overworld (2026-11-06)

A read-only pass over IRDO rev 0 to test the per-vertex identity idea above. Function names come from the public pokebw2 symbol files, behavior from our own disassembly. Nothing here was run, so anything about runtime behavior is still unproven. The scripts are in `logs/shelved-frame-interpolation/white2-re-scripts/` (git-ignored, this PC only).

How geometry reaches the hardware:
- Map path: `FieldChunk_Draw` (`0x02163414`, overlay 12), `NNS_G3dDraw`, `SHP_InternalDefault` (`0x02066918`), `NNS_G3dGeSendDL` (`0x02067c18`).
- Display lists of 256 bytes or more go by DMA channel 3 straight from the model's data, not a scratch buffer. Shorter lists, materials and matrices go through a RAM command buffer and get flushed by CPU, so they have no DMA source.
- Map blocks are one model each, with tiles baked in as separate geometry (all 1065 blocks in `a/0/0/8`: median 15 shapes, 417 polygons, 1,366 vertices). No shape is drawn twice in a block, but 53% of polygons have a translated twin in the same block, which fits the wrong matches seen earlier.
- Props are truly instanced from a per-area bundle (`a/2/2/5`), about 25% of drawn vertices (not weighted by real scenes). The small ones are CPU only.
- Characters (`BillboardActorScene_Draw`, `0x0204ebdc`) are quads written by CPU straight to the command ports, with no display list.
- BoxTest culls per block and per prop. The block list is rebuilt and sorted by file size against a VCOUNT-based budget, so vertex ordinals aren't safe as IDs.

What that means for identity:
- A DMA source tag is a poor ID: it repeats on instanced props and doesn't exist for short lists or characters.
- A hash of resolved vertex positions works for terrain, but it has to use the position after the relative commands (`0x25`–`0x28`), not the raw words.
- The best key adds the placement matrix. Each prop sends `MTX_LOAD_4x3` (camera), then `MTX_MULT_4x3` with its placement (`0x02163848`–`0x02163874`, overlay 12), and blocks send `MTX_TRANS`. It works for CPU and DMA alike. Moving characters still need something else.
- Better IDs only fix wrong matches. Geometry entering or leaving the view and objects moving on their own would still cause artifacts.

An untested alternative: because the camera matrix is sent separately, static geometry might not need pairing at all. Re-running the scene's transforms with a halfway camera would also let normal clipping handle the screen-edge polygons that caused seams. Characters would need their own fix.

## Logging run on the Fold (2026-11-06)

A logging-only switch (`debug.melonade.vertexid`) compared consecutive scenes in White 2's overworld. One run from a fresh boot: standing still, walking, an area change, then what is probably a block edge. It measures identity, not image quality, and only covers those maps.

| Phase | Polygons kept (median) | Key: no partner | Old signature: no partner | Old and key disagree | Under top camera load |
|---|---|---|---|---|---|
| Still 1 | 190 | 1.06% | 0.00% | 0.00% | 100% |
| Walk 1 | 282 | 1.30% | 3.71% | 0.10% | 99.69% |
| Area change | 396 | 1.48% | 7.89% | 0.54% | 99.62% |
| Walk 2 | 413 | 0.93% | 4.61% | 0.39% | 99.95% |
| Block edge (inferred) | 191 | 2.08% | 4.69% | 0.49% | 100% |
| Still 2 | 219 | 0.30% | 0.01% | 0.00% | 100% |

- Wrong matches were small, so they probably weren't the main cause of the artifacts, which goes against the first reason listed under "Why it stopped". The bigger failures of the old matching were coverage collapsing below 75% in 1–2% of moving scenes (minimum 4%), and 2–2.6% of polygons changing clip shape between scenes.
- 34–57% of kept polygons are clipped, so the screen edges touch a lot of the scene.
- The placement-plus-vertex key finds a partner for about 95–97% of polygons with the same clip shape. Its gap is small quads whose placement changes every scene, even standing still. They're probably characters, and the title screen suggests anything animated by placement breaks the key.
- One matrix load covers 99.4–100% of vertices. Its hash changes in 85–89% of moving scene pairs and never when still, so it's very likely the camera, which supports the halfway-camera idea for static geometry. Matrix values weren't logged, so the smoothness of the camera path is unknown.
- The 270 vs 417 gap is culling: 78–92% of attempted polygons never make it to the polygon list, and nothing was rejected for full RAM.
- Geometry popping in is mild on screen: submitted vertices can jump by up to 2,938 in one scene, but that added at most 18 kept polygons.
- Draw order is effectively unmeasured. `ordchg` fires constantly because the quads are in its hash, and `reorder` could only be tested on 130 pairs.
- Logging cost about 26–51 ms per second, at a steady 30 scenes per second.

## Halfway-camera prototype (2026-11-06)

`debug.melonade.halfcam` records scene B's commands and replays them in a second `GPU3D` with the camera blended halfway between A and B, compute renderer only. First Fold run (compute, 4x, mode 3, one short walk): 4,091 of 5,324 scenes interpolated. The rest were 1,031 with a still camera and 202 before the pattern lock. No crash, no capture conflicts, replay about 0.9 ms on average and 3.6 ms at worst on the emulator thread. The user saw clear smoothing and none of the old seams.

One open artifact: a small jitter on thin details like fence slats, sometimes, only visible in motion. A screenshot doesn't show the cause. The slant difference across the fence is normal perspective, mirrored around the screen center. The user saw the same jitter with interpolation off, so interpolation isn't its source. Whether it comes from the game or the compute renderer at 4x wasn't tested.

Title screens (compute 4x, by eye for the baseline): White 2 stayed at about 60 with interpolation on and off. Dark Dawn averaged about 40 on a short run, close to the 41.6 in `PERF-NOTES.md`, but that's a different session on a noisy scene. Both draw a new scene every frame, so they never lock onto the pattern and only pay for analysis (0.29 and 0.51 ms per scene).

Larger White 2 area with more NPCs, mode 3, about 4.5 minutes: 5,596 of 7,218 scenes interpolated, with 1,354 skipped for a still camera and 268 before the lock. No crash, 3 seconds under 59 emulated frames, worst replay 2.1 ms, and the user saw no notable FPS drop. 2,654 character batches went unpaired against 18,483 paired, about 13%, with no jitter reported. GPU and battery cost still aren't measured properly.

### Other games and the billboard fix

Platinum and SoulSilver worked with no per-game changes, but mode 3 left NPCs with a subtle jitter when moving horizontally. The cause, from a static read of Platinum's ROM: NitroSystem's billboard commands (BB `0x020b0d7c`, BBY `0x020b1084`) read `CLIPMTX_RESULT` on the CPU and send it back as `MTX_LOAD_4x3`, so the recorded translation holds B's camera, and replay drew those sprites half a step ahead. Mode 4 replaces that translation with the shadow's position matrix and keeps B's rotation and scale. The user confirmed the jitter was gone. SoulSilver wasn't checked statically, but its logs show the same billboards.

Mode 4 then showed a flashing afterimage behind the player in tall grass, in Platinum and SoulSilver only. The likely cause was pairing by order and shape, since grass tufts (`kusaeff`) and NPC billboards are identical quads, and the billboard fix exposed bad placements that mode 3's recorded load had hidden. That was never proven directly. Pairing now takes the closest pairs first (same palette and format, then format only), with limits of 10 and 8 px, and corrections capped at half the camera step plus 6. Mode 5 is mode 4 plus a per-pair trace. The user confirmed the afterimage, the NPC jitter and the character steadiness were all fine on that build.

Scene outcomes on the last builds: Platinum interpolated 980 of 1,640 scenes (274 still, 325 no camera, probably battle or menu scenes, unverified), and SoulSilver 391 of 438 on a 30 second run. No crash, worst replay about 3.2 ms. White (IRAO) worked on mode 4 with no billboards at all.

Still open:
- The bike or other fast movement is untested. The pair limits are tuned for walking, and 8 units is exactly the 10 px limit.
- No game outside Pokémon has been tried.
- It's compute only. The app's default of OpenGL skips it entirely.
- Real GPU and battery cost.
- Thin-detail jitter on fences happens with interpolation off too. Its source hasn't been found.
- The log's `pairmax` reached 16 units in Platinum, over the pair limit. It's probably a rejected candidate, not an applied pair, but that wasn't checked.
