# Plan: pixel button styles (8-bit and 16-bit Classic)

## Context

Two new on-screen button styles, picked from the mock at https://claude.ai/artifact/1AsXtUFrDTAGMERkbgjkbg (read it with the Artifact tool's `read` action; the "8-bit Classic" and "16-bit Classic" chips are the two to build). The Game Boy and DS Lite chips in the mock are not part of this plan.

Both styles draw the existing raised-button shapes from `RaisedButtons.kt` as pixel art: the shape is snapped to a coarse grid, given a hard outline and a few flat shades, and drawn with no anti-aliasing. Shapes, hit areas, spacing and the input handlers don't change. The D-pad stays a fixed centre square plus four separate arms, so the spacing slider works the same as it does for the other styles.

Paths below are under `app/src/main/`. Workflow as usual: coder subagent, then a fresh reviewer subagent, fixes, build, then checks on the emulator and phone.

## What exists today

- `domain/model/input/ControllerStyle.kt`: the style enum. Saved as its lowercase name (`res/values/arrays.xml` `controller_style`), with display names in `strings.xml` `controller_style_options`, in the same order.
- `ui/common/view/ControllerAppearance.kt`: colours per style, plus `Shading` (`RAISED`, `OUTLINE`, `SOFT_TINT`). `ControllerButtonColors` only has face, pressed face, edge and label.
- `ui/common/view/RaisedButtonView.kt`: draws every button from `Part`s (a `Path` shape, optional glyph, label or icon). Raised buttons draw all lips first, then pressed faces, then unpressed faces. `setParts()` is called again when spacing changes.
- `ui/common/view/RaisedButtons.kt`: the shapes. D-pad and ABXY on a 512 grid that grows with spacing, L/R on 128, Start/Select and icon buttons on 64. Each size has its own lip and travel.
- `ui/settings/fragments/ControllerStylePreferencesFragment.kt`: the style picker preview. It builds the buttons through `RaisedButtons`, so new styles show up there on their own.

## Step 1: the styles and their colours

- Add `PIXEL_8` and `PIXEL_16` to the end of `ControllerStyle`. Add `pixel_8` and `pixel_16` to the end of the `controller_style` array and "8-bit" and "16-bit" to the end of `controller_style_options`. Never reorder: the saved values are already on devices.
- Add `Shading.PIXEL` and a colour class for it, since pixel buttons need more than four colours:

```kotlin
class PixelColors(outline, lip, shadow, base, highlight, glint, ink, inkShadow)
```

- Colours by role. The 16-bit style uses all eight. The 8-bit style uses the same class with `lip = outline` and `glint = highlight`, so one drawing path covers both.

16-bit Classic:

| Part | outline | lip | shadow | base | highlight | glint | ink | inkShadow |
|---|---|---|---|---|---|---|---|---|
| A | #501313 | #791F1F | #A32D2D | #E24B4A | #F09595 | #FCEBEB | #FCEBEB | #501313 |
| B | #412402 | #633806 | #BA7517 | #EF9F27 | #FAC775 | #FAEEDA | #FAEEDA | #412402 |
| X | #042C53 | #0C447C | #185FA5 | #378ADD | #85B7EB | #E6F1FB | #E6F1FB | #042C53 |
| Y | #173404 | #27500A | #3B6D11 | #639922 | #97C459 | #EAF3DE | #EAF3DE | #173404 |
| D-pad | #121218 | #2C2C2A | #444441 | #5F5E5A | #888780 | #B4B2A9 | #D3D1C7 | #121218 |
| Everything else (L, R, Start, Select, icon buttons) | #2C2C2A | #5F5E5A | #888780 | #B4B2A9 | #D3D1C7 | #F1EFE8 | #2C2C2A | #F1EFE8 |

8-bit Classic (outline and lip are #121218 for every part). Its ink and ink shadow are the same as 16-bit's, since black ink was hard to read on the dark faces:

| Part | shadow | base | highlight | ink | inkShadow |
|---|---|---|---|---|---|
| A | #A32D2D | #E24B4A | #F09595 | #FCEBEB | #501313 |
| B | #BA7517 | #EF9F27 | #FAC775 | #FAEEDA | #412402 |
| X | #185FA5 | #378ADD | #85B7EB | #E6F1FB | #042C53 |
| Y | #3B6D11 | #639922 | #97C459 | #EAF3DE | #173404 |
| D-pad | #444441 | #5F5E5A | #888780 | #D3D1C7 | #121218 |
| Everything else | #888780 | #B4B2A9 | #D3D1C7 | #2C2C2A | #F1EFE8 |

The D-pad gets its own colours. Its arms have the inputs UP/DOWN/LEFT/RIGHT, but its centre square has `input = null`, and so do Start, Select and the icon buttons. That means `colorsFor()` can't tell the centre apart from them, so add a `Part.colorGroup` field (or similar) to the D-pad parts.

## Step 2: drawing pixel buttons

Add the pixel path to `RaisedButtonView` behind `Shading.PIXEL`, or as a small helper class it calls. The other shadings stay exactly as they are.

**Two grids.** The button's shape (outline, lip, body and edge shades) goes on the style's grid: 4dp cells for 8-bit and 2dp for 16-bit. Everything drawn on the face (letters, arrows, icons and their drop shadows) goes on a 2dp detail grid in both styles. For 16-bit that's the same grid. For 8-bit it's half the cell, and each 4dp cell is exactly 2×2 detail cells, so the two always line up. 8-bit is a hybrid: a chunky silhouette with the finer 16-bit ink on top.

**Pixel size.** Every button uses the same on-screen pixel size, whatever its design grid. The detail size is 2dp, rounded down to a whole number of physical pixels (at least 1px), and the 8-bit cell is twice that. Rounding down keeps faces from losing cells: at 2.75 density it gives 5px, not 6px. Small views, like the ones in the style picker preview, would end up only a few pixels across, so the size is capped per view at `min(styleSize, minViewDimension / minCells)`, with `minCells` 10 for 8-bit and 20 for 16-bit (20 detail cells for both), still rounded down to whole physical pixels and at least 1px. At the default in-game sizes (40dp small buttons, 140dp pads) the cap doesn't kick in. All parts in one view share one cell size.

**Masks.** For each part, rasterize its face `Path` into a grid-sized `ALPHA_8` bitmap with anti-aliasing off, so each cell is either in or out at a 50% threshold. Each part gets its own grid, centred on the part's bounds, so circles and arms come out symmetric. Parts in one view can land on different whole-cell offsets, which is fine.

**D-pad gap.** The design gap between the D-pad's centre and its arms is thinner than a cell, so their outlines would touch. Trim the inner end of each arm's mask until there's at least one empty cell between it and the centre. That's drawing only, so the hit areas don't change.

**Lip and travel** in grid pixels:
- 8-bit: lip 2, travel 2. The face sinks all the way onto the lip.
- 16-bit: lip 4, travel 3. One row of lip stays visible while pressed.

These are fixed instead of following each size's design-unit lip, because the mock looked best with the same depth on every button. On small buttons that's deeper than the room the design leaves below the face, so the lip and the pressed face would run off the bottom of the view. When the face plus the pixel lip is taller than the view, shrink the scale until it fits, keeping it centred across. If the lip still runs off the bottom because the design leaves more room above than below (the D-pad), move the faces up into that room. Hit areas stay as they are.

**Shading.** A cell is "interior" when it and its four neighbours are all in the mask. Mask cells that aren't interior are the outline. For each interior cell, count the steps to the nearest non-interior cell going up (`ut`), left (`lt`), down (`bt`) and right (`rt`); a cell touching the outline counts 1. Then check these rules in order, and the first one that matches wins:

These first three rules are checked per detail cell, on interior cells only:

1. Label, arrow or icon cell: `ink`.
2. The detail cell up-left of it is a label, arrow or icon cell: `inkShadow` (a hard 1-cell drop shadow).
3. D-pad centre dimple: a circle of radius 3.5 detail cells in the middle of the centre square, smaller on small D-pads and left out below 1.5. Its edge is `shadow` on the top-left half and `highlight` on the bottom-right half, and `base` inside, so it reads as an evenly recessed dent.

The rest are checked per cell of the style's grid:

4. 8-bit, not pressed: touching the outline on the top or left only: `highlight`. Bottom or right only: `shadow`. Otherwise `base`.
5. 8-bit, pressed: the same, with `highlight` and `shadow` swapped.
6. 16-bit, not pressed:
   - Glint: `ut == 2 && lt in 2..4`, or `ut == 3 && lt == 2`.
   - Shadow: `bt <= 2 || rt == 1`, or the dithered row `bt == 3 && (x + y) % 2 == 0`.
   - Highlight: `ut <= 2 || lt == 1`.
   - Otherwise `base`.
7. 16-bit, pressed:
   - Lip colour: `ut <= 2 || lt == 1`, or the dithered row `ut == 3 && (x + y) % 2 == 0`.
   - Otherwise `shadow`, so the whole face goes darker.

**Lip cells** are the face mask moved down by 1 to `lip` rows, wherever the face itself isn't. A pressed face covers the top of the lip. For 8-bit they're all `outline`. For 16-bit, a lip cell is `outline` if the cell below it, left of it or right of it is empty, and `lip` otherwise.

**Labels.** Labels are stamped onto the detail grid from built-in bitmap fonts. The single letters A, B, X, Y, L and R use a blocky 5×7 font in an arcade style, with two-cell-wide strokes like Press Start 2P. Longer labels use a compact 3×5 font, which single letters also fall back to when the big one doesn't fit. Scale the label by the largest whole number that still fits inside the face. A label fits when its cells, plus its drop shadow, are all interior cells, with no extra margin. Single letters in one view (A, B, X and Y) share one scale: work out the biggest that fits each of them first, take the smallest, and draw them all at that, so they never come out at mixed sizes. START and SELECT never fit at 1×, so they fall back to shorter labels, using the first that fits: START, STA, ST and SELECT, SEL, SL. If none fit, leave the label off. This avoids bundling a font file and its licence. Labels move down with the face when it's pressed.

**Caching.** For each part, build three small bitmaps whenever the size or the parts change (`onSizeChanged`, `setParts`): the lip, the unpressed face and the pressed face. `onDraw` keeps the current order: all lips, then pressed faces, then unpressed faces. Draw the bitmaps scaled up by the pixel size with `filterBitmap = false`. Don't do the shading inside `onDraw`.

**Pixel icons and arrows.** Rasterized vector icons turn to mush at this size, so every icon from `componentbuilders/SingleButtonIcons.kt` has a hand-drawn version (`PixelIcons.kt`), drawn as rows of `#` and `.` on the detail grid, about 9×13 to 13×13 cells, centred on the face, with clean 1-cell strokes: hinge, settings gear (pause), fast forward, touch (an open eye: almond lids with pointed corners and a solid iris with a highlight gap at its top left; a pointing hand read as a middle finger), touch off (a closed eye with lashes, so open means touch controls shown and closed means hidden), refresh (reset), swap screens, quick save, quick load, rewind, microphone and microphone off. The microphone off icon is drawn by hand too, as the microphone with a 45° slash that has a one cell gap on each side and stays inside the art. The closed eye is shorter than the open one, and each icon is centred and scaled from its own size. Icons over 10 cells across also have a compact version of at most 9×9, since 8-bit's thick outline leaves a 40dp face only about 10 to 12 detail cells inside. They're looked up by drawable resource id in `RaisedButtons.iconButton` and passed in as `Part.pixelIcon` and `Part.pixelOffIcon`. The D-pad arrows are a hand-drawn 7×4 triangle, turned for each arm.

Icons and arrows grow with the button, like letters do. Each part gets the largest whole scale at which the art plus its drop shadow fits the interior and the art's larger side is at most 55% of the face's smaller side, the same share as the vector icons (`ICON_SIZE`). D-pad arrows measure against their arm, and all four arrows in a D-pad share the smallest of their scales. The drop shadow stays one detail cell at every scale. At the default sizes that's 1×; a 120dp icon button gives 2× and a 300dp D-pad gives 3× arrows.

An icon and its drop shadow are centred together as one box, in whole and half cells, with ties going up and left. The first of these that fits is used, each at the biggest scale allowed: the icon with its shadow, the icon without its shadow, the compact icon with its shadow, the compact icon without it. If none fit, or a drawable has no pixel version, the vector one is rasterized at a 50% threshold instead. With the default sizes (40dp small buttons) every icon fits as a hand-drawn one in both styles at densities 2.0, 2.625, 2.75, 3.0 and 3.5.

**Toggled-off icons** use `pixelOffIcon` (or `offIcon`), or the normal icon dimmed. Draw the dimmed case in `shadow` instead of `ink`, since alpha doesn't suit pixel art.

## Step 3: checks

- Build, then let the emulator-tester agent go through: the style picker shows both new styles with every button pressable; the in-game buttons in both styles; the layout editor with D-pad and ABXY spacing at 0, 50% and 100% (arms and buttons move apart, pixels stay sharp, shapes stay symmetric and the D-pad arms keep their gap); the toggled-off look of the touch/mic buttons; and switching back to Classic, which must look unchanged.
- Phone: check that pixels are sharp (no blur from scaling), and that pressing feels the same as the other styles. Watch for any frame-time cost from the cached bitmaps. There shouldn't be any.

## Not in this plan

- The Game Boy and DS Lite pixel palettes from the mock. Adding them later is just new rows of colours.
