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

8-bit Classic (outline and lip are #121218 for every part):

| Part | shadow | base | highlight | ink |
|---|---|---|---|---|
| A | #A32D2D | #E24B4A | #F09595 | #121218 |
| B | #BA7517 | #EF9F27 | #FAC775 | #121218 |
| X | #185FA5 | #378ADD | #85B7EB | #121218 |
| Y | #3B6D11 | #639922 | #97C459 | #121218 |
| D-pad | #444441 | #5F5E5A | #888780 | #D3D1C7 |
| Everything else | #888780 | #B4B2A9 | #D3D1C7 | #121218 |

The D-pad gets its own colours. Its arms have the inputs UP/DOWN/LEFT/RIGHT, but its centre square has `input = null`, and so do Start, Select and the icon buttons. That means `colorsFor()` can't tell the centre apart from them, so add a `Part.colorGroup` field (or similar) to the D-pad parts.

## Step 2: drawing pixel buttons

Add the pixel path to `RaisedButtonView` behind `Shading.PIXEL`, or as a small helper class it calls. The other shadings stay exactly as they are.

**Pixel size.** Every button uses the same on-screen pixel size, whatever its design grid: 4dp for 8-bit and 2dp for 16-bit, rounded to a whole number of physical pixels. The grid origin is the view's top-left corner, so all parts in one view line up.

**Masks.** For each part, rasterize its face `Path` into a grid-sized `ALPHA_8` bitmap with anti-aliasing off (or with a 50% threshold), so each grid cell is either in or out. Do the same for its glyph and its icon. The existing vector icons rasterize fine at this size, so no hand-drawn icons are needed. If an icon looks muddy at 8-bit size, a hand-drawn 7×7 version can replace it later.

**Lip and travel** in grid pixels:
- 8-bit: lip 2, travel 2. The face sinks all the way onto the lip.
- 16-bit: lip 4, travel 3. One row of lip stays visible while pressed.

These are fixed instead of following each size's design-unit lip, because the mock looked best with the same depth on every button.

**Shading.** A cell is "interior" when it and its four neighbours are all in the mask. Mask cells that aren't interior are the outline. For each interior cell, count the steps to the nearest non-interior cell going up (`ut`), left (`lt`), down (`bt`) and right (`rt`); a cell touching the outline counts 1. Then check these rules in order, and the first one that matches wins:

1. Glyph or icon cell: `ink`.
2. 16-bit only: the cell up-left of it is a glyph or icon cell: `inkShadow` (a hard drop shadow).
3. D-pad centre dimple (a circle of radius 1.3 cells for 8-bit, 3.5 for 16-bit, in the middle of the centre square): `shadow` for 8-bit. For 16-bit, `shadow` on its top half and `highlight` on its bottom half, so it reads as dented in.
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

**Lip cells** are the face mask moved down by 1 to `lip - travel` rows, wherever the face itself isn't. For 8-bit they're all `outline`. For 16-bit, a lip cell is `outline` if the cell below it, left of it or right of it is empty, and `lip` otherwise.

**Labels.** Labels go through the same mask step, so they sit on the grid. Use a small built-in 3×5 bitmap font for A, B, X, Y, L, R, START and SELECT. Scale it by the largest whole number of grid pixels that still fits inside the face: about 2× on ABXY and L/R, which matches the mock. If START or SELECT doesn't fit even at 1×, leave the label off. This avoids bundling a font file and its licence. Labels move down with the face when it's pressed.

**Caching.** For each part, build three small bitmaps whenever the size or the parts change (`onSizeChanged`, `setParts`): the lip, the unpressed face and the pressed face. `onDraw` keeps the current order: all lips, then pressed faces, then unpressed faces. Draw the bitmaps scaled up by the pixel size with `filterBitmap = false`. Don't do the shading inside `onDraw`.

**Toggled-off icons** (`offIcon`, or a dimmed `icon`) are rasterized as their own icon mask. Draw the dimmed case in `shadow` instead of `ink`, since alpha doesn't suit pixel art.

## Step 3: checks

- Build, then let the emulator-tester agent go through: the style picker shows both new styles with every button pressable; the in-game buttons in both styles; the layout editor with D-pad and ABXY spacing at 0, 50% and 100% (arms and buttons move apart, pixels stay sharp and on one grid); the toggled-off look of the touch/mic buttons; and switching back to Classic, which must look unchanged.
- Phone: check that pixels are sharp (no blur from scaling), and that pressing feels the same as the other styles. Watch for any frame-time cost from the cached bitmaps. There shouldn't be any.

## Not in this plan

- The Game Boy and DS Lite pixel palettes from the mock. Adding them later is just new rows of colours.
- Hand-drawn pixel icons.
