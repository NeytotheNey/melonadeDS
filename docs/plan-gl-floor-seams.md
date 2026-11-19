# Floor seams in the GL renderer

Branch `fix/gl-floor-seams` (app and core), started 11-18.

## The bug

Pokemon Platinum, Diamond and SoulSilver overworlds show thin dark lines in the floor that flicker while walking. OpenGL only. Soft and Compute are clean.

## What the desktop diagnosis found (11-18)

Platinum, Twinleaf Town, 620 frames of walking, shots every 4 frames. "Gap pixels" = black in GL where soft is bright, leaving out row 0, column 255 and the player sprite.

| Run | Gap pixels | Frames with any |
|---|---|---|
| GL 1x | 3125 | 147 of 151 |
| GL 4x | 12451 | 150 of 151 |
| GL 8x | 30182 | all 151 |
| Compute 4x | 231 | 23 of 151 |
| SoulSilver route, GL 1x / 4x | 1350 / 5107 | 99 / 101 of 101 |

- **It's a real gap.** The pixels are the clear color, and no polygon covers them. It happens at 1x too.
- **Cause: T-junctions plus GL's fill rule.** The sand and path polygons share a vertical world line, but their vertices sit at different heights along it. Rounding the projected positions moves a T-vertex slightly off its neighbor's edge, leaving a crack under a pixel wide. The DS fills each span from the left edge's pixel to the right edge's pixel inclusive (`GPU3D_Soft.cpp` ~853–920), so a pixel an edge passes through belongs to both neighbors and the crack gets covered. GL only fills pixels whose center is inside.
- **Upscaling adds error.** `SetupVertex` (`GPU3D_OpenGL.cpp:1320`) does `(HiresPosition * ScaleFactor) >> 4`, which truncates to whole target pixels on top of the 1/16 rounding.
- **Edge marking makes it visible.** These frames have DISP3DCNT AA and edge marking on. With `GLEdgeMark=0` the gap count is the same, but each gap loses its two half-dark neighbor lines, which is the "streak" look on the phone.
- **Ruled out:** antialiasing, `GLEdgeSkip`, `GLClearFast`, `GLNoDiscard`, `GLTexCache`, `GLTransSkip` (same counts at 0), `BetterPolygons` (no effect), depth precision, fog. The vertex code is the same as upstream.

Rough experiments (reverted):

| Change | Platinum 1x | 4x | 8x | SoulSilver 1x |
|---|---|---|---|---|
| none | 3125 | 12451 | 30182 | 1350 |
| sub-pixel positions | – | 5730 | 23831 | – |
| grow opaque polygons outward | 7 | 181 | – | 3 |
| both | – | 196 | 963 | – |

Growing used a push away from the centroid, half a DS pixel at 1x and one target pixel at 4x. Difference from soft (pixels off by more than 48): 1x 11.35% → 12.71%, 4x 11.61% → 11.79%.

## The fix

One switch, `GPU::GLSeams`, debug `glseams`, so every step can be A/B'd:

- **Bit 0: sub-pixel vertex positions.** Stop truncating in `SetupVertex`; pass positions with fraction bits and divide in `kRenderVS`. `hires * scale` overflows u16 at 16x, so use fewer fraction bits or a float attribute. The BetterPolygons center vertex (~1551), `WideVertexX/Y` and the E1 `BuildEdges` outlines must use the same units, or they'll drift off the polygons.
- **Bit 1: grow opaque polygons by about one target pixel** (half a DS pixel at 1x), to copy the DS's inclusive edges.
  - Offset each edge outward along its normal and put the new vertex where the neighboring offset edges meet. Cap the miter at about 2× the offset so thin slivers don't spike.
  - Extrapolate texture coordinates, color and depth to the new vertex from the polygon's own plane, so textures don't slide. Keep it perspective-correct the same way the existing interpolation is.
  - Skip line polygons (type 1), translucent polygons (an overlap would blend twice), and shadow and shadow-mask polygons.
- **Fallback if bit 1 misbehaves:** draw each opaque polygon's edges as 1-pixel lines with its attributes, after the fill with depth test LEQUAL. Cheaper to build, but line rasterization varies between drivers, so it needs both phone GPUs.

Default: both bits on once the checks below pass; until then the default stays 0.

Since 11-18 it's the "Fix floor seams" setting (`video_gl_seams`, Display → Accuracy, off by default, grayed out for Software and Compute): on is `3`, off `0`, applied in `MelonInstance::updateRenderer` at game start and when settings change mid-game. The debug pause menu toggle is gone; `glseams` still overrides the setting in debug builds.

## Checks

1. **Desktop gap count** at 1x, 4x and 8x in Platinum and SoulSilver with each bit alone and both. Target: at or near Compute's 231 at 4x. Also eyeball the edge-mark streaks.
2. **Desktop difference from soft** must not get much worse than the experiment's 12.71% / 11.79%.
3. **Other games, desktop:** hash3d or shots against `glseams=0` in White 2 (title and town), Mario Kart, Contra 4, Dark Dawn. Expect small edge changes only; look for grown silhouettes, shifted edge-mark outlines, and z-fighting where equal-depth polygons now overlap.
4. **Compute and soft unchanged** (they don't read the switch).
5. **Fold run** once the phones are free (GL changes need Adreno), then the Pixel. Platinum or SoulSilver walking at 4x and 8x, `glseams` 0 vs 3, screenshots plus one 30 s perf pass each.

## Results (11-18, desktop)

Built as planned, except that bit 1 became a **skirt** instead of moving the polygon's own vertices. The first build grew the polygons in place, which closed the cracks but made a dashed line wherever two opaque polygons met on the same surface (the Twinleaf mailbox's band): each one's grown ring now overlapped the other at equal depth, and the winner changed pixel by pixel. Pushing the ring back by a depth margin didn't fix it in a way that holds for every game (2^18 worked in Platinum, 2^20 already reopened cracks against its clear depth). So now:

- **Bit 0** (`HiresToPos`): positions are in 1/(1 << PosFrac) target pixels, 4 fraction bits up to 8x, 3 at 16x, fewer for wide views, scaled back by a new `uPosScale` in the UBO. 1x still uses the DS's integer positions. `WideVertexX/Y`, the BetterPolygons center and `BuildEdges` use the same units.
- **Bit 1** (`BuildSkirt`): each opaque polygon gets a ring of new outer corners, one target pixel out (half a DS pixel at 1x) along the edge normals, miter capped at 2×. Their depth, W, color and texcoords are extrapolated from each corner's triangle plane (perspective-correct). Colors are clamped to the polygon's range, and texcoords to a 16th of a texel inside it (since 11-19, see "Lines on Gen 4 battle sprites" below). The rings are drawn right after the opaque pass with the stencil test passing only on the clear plane (bit 7 set, `GL_LESS polyid over 0x80`), so they fill cracks and edges against the background and never touch a pixel a polygon drew. Skipped: lines, translucent, wireframe, depth-equal, shadow and mask polygons. The edge-line fallback wasn't needed.

Platinum walk (151 shots), gap pixels, then difference from soft (box-averaged to 1x, pixels off by more than 48):

| | `glseams=0` | `1` | `2` | `3` |
|---|---|---|---|---|
| 1x | 3125 / 11.35% | 3125 / 11.35% | 15 / 11.21% | 15 / 11.21% |
| 4x | 12451 / 10.74% | 5730 / 10.58% | 245 / 10.62% | 251 / 10.54% |
| 8x | 30182 / 10.91% | 23831 / 10.81% | 1022 / 10.85% | 1028 / 10.77% |

Compute is 231 at 4x and 1097 at 8x by the same count. What's left at `3` is the same places Compute has: the screen's right column and bottom row and one frame's object, not the floor.

SoulSilver route walk (106 shots), gap pixels: 1x 1465 → 2 (`2` or `3`), 4x 5804 → 2038 (`1`) → 246 (`3`), 8x 11976 → 8367 (`1`) → 921 (`3`); Compute 4x 161. The leftovers are around the player sprite, as with Compute. Difference from soft: 1x 23.74% → 23.65%, 4x 16.32% → 15.38% (`1`) → 15.36% (`3`).

- The edge-mark streaks are gone with the cracks. The mailbox, house edges and trees look the same as at `0` apart from the closed cracks.
- `glseams=0` is pixel-identical to the build before the change (GL 1x, 4x, 8x, all 151 shots). Soft and Compute are identical too (check 4).
- WideMelon (`--wide 400`, with and without `--wide-margin 32`) and BetterPolygons line up at `3` and lose their cracks too.
- **Other games** (4x, shots against `0`): bit 1 changes no pixel in White 2's title and town, Mario Kart, Contra 4 or Dark Dawn. Their opaque polygons never border the clear plane there. Bit 0 changes edges by a fraction of a target pixel everywhere (White 2 title up to 75k pixels a shot, Mario Kart 220k of 786k). Box-averaged difference from soft: W2 title 1.97 → 2.06%, W2 town 3.07 → 3.13%, Mario Kart 10.33 → 10.40%, Contra 4 1.91 → 2.24%, Dark Dawn 5.18 → 5.09%. No glitches seen in the crops.
- **CPU in `BuildPolygons`** (PC, 4x, mean per build): Platinum 29 → 80 µs, Mario Kart 18 → 84 µs, Dark Dawn title (1300 polygons) 22 → 169 µs, worst build 556 µs. All of it is bit 1; bit 0 costs nothing. Expect 2–4× that on a phone.
- Memory: room for 4 skirt corners a polygon, so the vertex, edge-info, edge-line and index buffers grew by about 1.7 MB host-side and 1.5 MB in GL buffers.

Not checked: a W-buffer scene with the clear plane showing next to opaque polygons (none of the tested ones has one; skirts are built and drawn there, the stencil just never passes), and an Android build.

## Lines on Gen 4 battle sprites (11-19)

Branch `fix/glseams-billboard-lines` (app and core). With Fix seams on, Gen 4 wild battle intros (SoulSilver, Platinum, Diamond) showed a thin line above the opposing Pokemon and a short vertical tick right of the trainer, gone once the player's Pokemon was sent out. OpenGL only.

**Cause.** The battle sprites are 3D quads cut from one 256x256 texture in 80x80 cells: in Diamond the trainer is texcoords 0..80 × 0..80 and the Pokemon 0..80 × 80..160, both on clamping axes. The skirt extrapolated texcoords past the polygon's range and only clamped them on repeating axes, so its pixels a target pixel outside a sprite read the next cell: the Pokemon's top skirt read row 79, the bottom row of the trainer's cell, and the trainer's right skirt read column 80. The quads themselves are ordinary opaque polygons; the skirt drew texels the DS never shows there. The send-out replaces the trainer's cell, which is why the lines went away. Not tied to the wide battles: it shows at width 256 on the `material3` build too.

**Fix** (core `BuildSkirt`): texcoords stay a 16th of a texel inside the polygon's range on every axis, so the skirt only repeats the polygon's own edge texels. That also covers a repeating axis, where the old clamp let the skirt read the texture's other side.

Desktop checks (4x unless noted, shots against the old build; evidence in `logs/glseams-lines/`):

- The lines are gone in SoulSilver (`ss_appeared`) and Diamond (`dia_prethrow2`): `glseams=3` now matches `0` there pixel for pixel, at 1x, 4x and 8x and with `--wide 400` (what's left wide is bit 0's sub-pixel edges, the same with `glseams=1`).
- Platinum and SoulSilver walks (600 frames, 1x, 4x, 8x, and 4x with BetterPolygons): every crack the old skirt filled is still filled. Under 300 pixels change over 150 shots, all single crack pixels that now take the polygon's own edge texel instead of the one past it.
- Other games: no change in White 2's title and town or Dark Dawn. Contra 4's Konami logo has 54 changed pixels over 15 shots and Mario Kart 3 over 20, single target pixels at polygon corners against the clear plane.

Fold run (11-19, the user): the lines are gone.

**Known gap: a 1-px line can still show on an edge that sits exactly on pixel centers.** The skirt shares its inner vertices with the rim, and the rim keeps the polygon's own texcoords, so a skirt pixel right on the polygon's hi edge (or lo edge with negative texcoords, since the shader truncates toward zero) interpolates to exactly `hi` and can read the next texel. With bit 0's sub-pixel positions that edge lands on pixel centers at 4x (DS positions on an odd 1/8) and 8x (odd 1/16); GL's top-left rule then gives that column to the skirt, the stencil passes, and float error in the perspective divide decides the texel. Never seen: the desktop (Nvidia) matched glseams 0 pixel for pixel, and the user didn't spot it on the Fold. If it shows up (a faint one-pixel line or tick by a battle sprite, at 4x or 8x with Fix seams on): give the skirt its own copies of the rim vertices, same positions, texcoords clamped to [lo+1, hi-1] like the outer ring, and raise the `MaxGLVertices` check to 2n per polygon. Separately, the clamp is per axis on the polygon's texture-space box, so a triangle's skirt corner can still pick a texel inside the box but outside the triangle; quads (the sprites) aren't affected.

## Risks

- Untextured silhouettes grow by about one target pixel.
- Edge-mark outlines move slightly.
- Equal-depth overlaps go to whichever polygon draws first.
- Extra CPU in `BuildPolygons` for the offsets. Measure it.

## Not covered here

Seen during the diagnosis, not looked into: Compute 4x has a black row 0 where soft is bright, and GL 8x has a black column at DS x=255.
