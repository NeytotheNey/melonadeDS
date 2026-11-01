# Findings: Kotlin layout editor / ui.common / domain / romdetails

Scope: `ui/layouteditor/**`, `ui/common/**`, `domain/**`, `ui/romdetails/**` on branch `material3` vs `c42995ca`.
All paths relative to `app/src/main/java/me/magnum/melonds/` unless noted. Effort S/M/L.

## 1. Dead code

- `ui/common/view/ControllerAppearance.kt:150-156` — `pixel()` builds a `ControllerButtonColors` for pixel styles that the comment itself says is never used for drawing ("only there so that every appearance has them"); it is only consumed by `RaisedButtonView.setParts` to tint icons that the pixel painter then rasterizes as an alpha mask — Make `colors` nullable or model `ControllerAppearance` as a sealed class (`Flat(colors)` / `Pixel(art)`) so no placeholder colours are needed — M.
- `ui/common/view/ControllerAppearance.kt:78-80` — `enum class ColorGroup { DPAD }` has a single entry and is only ever compared to `DPAD` (6 places in `PixelButtonPainter`, 2 in `RaisedButtons`) — Replace it with an `isDpad: Boolean` on `RaisedButtonView.Part` — S.
- `ui/common/view/RaisedButtons.kt:44-47, 76-78` — `RaisedButtons.dpad()`/`abxy()` build parts at spacing 0, then `LayoutView.addPositionedLayoutComponent` immediately calls `applySpacing`, which rebuilds all parts when spacing > 0 — Give `dpad()`/`abxy()` a `spacing` parameter and pass it from `LayoutView` so parts are built once — S.
- `ui/layouteditor/LayoutEditorView.kt:71` — `minComponentSize` became public (was `private by lazy`) but is used only inside `LayoutEditorView` (9 references, no external callers) — Make it `private val` again — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:67-68` — `margin` (Float) and `marginPx` (Int) are both `R.dimen.layout_editor_margin` — Keep `marginPx` and convert at the two Float uses in `positionPill` — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:393-395` — `formatSize()` is `String.format(Locale.getDefault(), "%d", size)`, equivalent to `size.toString()` — Inline `size.toString()` and delete the helper — S.
- `ui/layouteditor/LayoutEditorSnapping.kt:10-13, 35-36` — top-level `gridOrigin(limit)` is a one-liner wrapped again by `SnapAxis.origin` — Inline `limit / 2` into `SnapAxis.origin` and `gridLines` and delete `gridOrigin` — S.
- `ui/common/view/PixelIcons.kt:16` — `PixelIcon.withCompact()` is only used as a builder inside `PixelIcons` right after `icon(...)` — Give `icon(vararg rows)` an optional `compact` parameter and drop `withCompact` — S.
- `ui/layouteditor/LayoutEditorActivity.kt:101` — `controllerStyle` field is only used to pass the style to the external presentation in `setOnShowListener`; the collector already assigns it to `layoutEditorManager.layoutEditorView.controllerStyle` — Read that property there and drop the field — S.

## 2. Duplication and reuse

### Pixel-art painter vs. the vector button drawing
- `ui/common/view/PixelButtonPainter.kt:513` + `ui/common/view/RaisedButtonView.kt:286` — `ICON_SIZE = 0.55f` is defined twice; the painter's comment says "The same as RaisedButtonView's vector icons" — Make it one `internal const val BUTTON_ICON_SIZE` in `RaisedButtonView.Companion` and reference it from the painter — S.
- `ui/common/view/PixelButtonPainter.kt:82, 220` — the nominal detail-cell size `art.pixelSizeDp / art.detailPerCell * density` is computed in both `setViewSize` and `iconScaleLimit` — Store it once as `private val nominalDetailSize` — S.
- `ui/common/view/PixelButtonPainter.kt:181-185, 207-211, 309-311` — `labelCentre`, `iconCentre` and the dimple-centre math all do "(view px − grid.origin) / detailSize" — Add `PartGrid.toDetail(x, y)` and use it in all three — S.
- `ui/common/view/PixelButtonPainter.kt:462-475, 481-500` — `fitScale` and `stamp` both compute `iconsWidth`, `iconsHeight`, `left`, `top` with the same formulas; `stamp` calls `fitScale` and then recomputes them — Have `fitScale` return a `Placement(scale, left, top)?` that `stamp` consumes — S.
- `ui/common/view/PixelButtonPainter.kt:242-405` — `buildPart` is ~165 lines holding 7 local functions (`isInDimple`, `dimpleColor`, `distance`, `shapeColor`, `isLip`, `isButton`, `lipColor`, `faceColor`) — Extract a `private class PartShader(art, colors, mask, interior, ...)` with those as methods, leaving `buildPart` to assemble ink and call `bitmapOf` — M.
- `ui/common/view/PixelButtonPainter.kt:136-142, 149-157, 164-166, 287-293` — four hand-rolled `for (y) for (x)` loops over a `Mask` — Add `Mask.forEach { x, y -> }` / `Mask.fill { x, y -> Boolean }` helpers — S.
- `ui/common/view/PixelButtonPainter.kt:262-263` + `ui/common/view/RaisedButtonView.kt:273` — the toggle-state icon rule `if (isToggledOn) icon else offIcon ?: icon` is written in both files — Move it to `RaisedButtonView.Part.iconFor(isToggledOn)` / `pixelIconFor(isToggledOn)` — S.
- `ui/common/view/ControllerAppearance.kt:87-105, 142-148` — `raised(...)`/`colors(...)` take `label: Int` while the other channels are `Long`, forcing `WHITE` to be an `Int` const and `0xFF4A4D55.toInt()` at call sites — Make every colour parameter `Long` and convert inside the helpers — S.
- `ui/common/view/ControllerAppearance.kt:108-126` — the 16-bit and 8-bit palettes repeat the same shadow/base/highlight/ink/inkShadow values per input in two parallel tables — Define each palette once and derive the 8-bit one with a `toEightBit()` mapper (`outline`/`lip` → `PIXEL_8_OUTLINE`, `glint` → `highlight`) — S.

### Layout editor: item controls vs manager view vs editor view
- `ui/layouteditor/LayoutEditorItemControls.kt:550-554, 604` — the "does this rect overlap the floating menu button" test is written twice with raw x/y/width/height comparisons — Add a `private fun View.overlaps(left, top, right, bottom)` (or use `RectF.intersects`) in both `positionPill` and `keepPanelClearOfMenuButton` — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:220-237` + `ui/layouteditor/LayoutEditorManagerView.kt:355-357` — both tint `ImageButton`s with `R.color.textLayoutEditor` in code with the same "app:tint isn't applied in the presentation" comment — Tint `buttonFloatingMenu` inside `LayoutEditorItemControls.tintIcons()` (it already has the binding) and drop the duplicate — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:289, 318, 405` — the `fillingControls` guard is repeated in the seek bar helper, the aspect toggle group and the above-screen switch — Route all through one `ifUserChange { }` helper — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:424-453` + `ui/layouteditor/LayoutEditorView.kt:535-560` — sizes are passed around as `Pair<Int, Int>` (`getScaleSize`, `getFitSize`, `fitsInEditor`, `applyScale`, `constrainSize`, `getSelectedViewSizeRanges`) — Introduce `data class Size(width, height)` (or reuse domain `Point`) — S.
- `ui/layouteditor/LayoutEditorView.kt:467-468` + `:545` — the min-width-under-ratio formula `max(minComponentSize, minComponentSize * ratio)` is in `snapResize` (comment: "Same minimum as constrainSize") and in `constrainSize` — Extract `private fun minSize(ratio: Float?)` and call it from both — S.
- `ui/layouteditor/LayoutEditorView.kt:497-501` + `:793-797` — the "other screen" lookup `when (component) { TOP_SCREEN -> views[BOTTOM_SCREEN]; BOTTOM_SCREEN -> views[TOP_SCREEN]; else -> null }` is copy-pasted in `getSnapAxes` and `setSelectedScreenOnTop` — Add `private fun otherScreenOf(component)` — S.
- `ui/layouteditor/LayoutEditorView.kt:742-747` + `ui/layouteditor/LayoutEditorSnapping.kt:164-166` — `clampInside` (per axis) and `LayoutEditorSnapping.clamp` are the same `start.coerceIn(0, limit - length)` — Make `LayoutEditorSnapping.clamp` internal and use it from `clampInside` — S.
- `ui/layouteditor/LayoutEditorView.kt:826, 833` + `ui/layouteditor/LayoutEditorSnapping.kt:160-162` — `(limit - length) / 2` is in `centerSelectedViewHorizontally/Vertically` and in `LayoutEditorSnapping.centeredStart` (comment: "Same as the centre buttons") — Expose `centeredStart` and use it in both centre functions — S.
- `ui/layouteditor/LayoutEditorView.kt:433-438, 452-453, 464-465, 471-472, 860-861` — the "pick fixed/dragged edge from `Anchor.isLeft/isTop`" ternaries are repeated five times — Add `Anchor.fixedCorner(rect)` / `Anchor.place(fixedX, fixedY, w, h): Rect` on the enum — S.
- `ui/layouteditor/LayoutEditorView.kt:371-376` + `:600-606` — touch-slop detection (`hypot(dx, dy) >= touchSlop` → dragging) is duplicated between the resize gesture and the drag handler — Share a small `DragTracker(downX, downY, touchSlop)` — S.
- `ui/layouteditor/LayoutEditorView.kt:304-311` — the size-label background reuses the `handleRect` field as scratch — Use a separate `labelRect` — S.
- `ui/layouteditor/LayoutEditorManagerView.kt:429-430, 457-458, 462-466` — floating-menu-button preference keys are assembled with `"${keyPrefix}_x"` templates in three places — Add a `FloatingMenuButtonPosition(preferences, key)` helper or `_x`/`_y` suffix constants in `PreferenceKeys` — S.

### Compose: RomModsUi / LuaScriptOptionControl vs existing components
- `ui/common/component/LuaScriptOptionControl.kt:141-187, 192-254` — `ChoiceControl` and `LayoutControl` are the same Box/Row/Column/Icon/DropdownMenu structure, differing only in the current-value label and the item list — Extract `private fun DropdownControl(label, valueLabel, enabled, items: List<Pair<String, () -> Unit>>, modifier, horizontalPadding)` and have both call it — M.
- `ui/romdetails/ui/RomModsUi.kt:240-268, 484-526` — `WidescreenSwitchItem` and `LuaScriptItem` are both "title + caption + trailing Switch, 64dp row" and duplicate `ui/common/preference/SwitchPreference.kt:SwitchItem`, which lacks caption/enabled params — Add `caption: String? = null` and `enabled: Boolean = true` to `SwitchItem` and delete both local composables — M.
- `ui/romdetails/ui/RomModsUi.kt:214-238` — `InfoItem` is `ActionLauncherItem` without a click — Add a `clickable`/`onLaunchAction: (() -> Unit)?` to `ActionLauncherItem` and drop `InfoItem` — S.
- `ui/romdetails/ui/RomModsUi.kt:445-482` — `ScriptFolderItem` is another title+caption row with a trailing IconButton — Fold into a shared `TwoLineItem(title, caption, trailing)` that `SwitchItem`/`ActionLauncherItem`/`ScriptFolderItem` build on — M.
- `ui/romdetails/ui/RomModsUi.kt:118-123` + `:279-284` — the section header `Text(titleSmall, primary, padding 16/24/8)` is written twice — Add `private fun SectionTitle(text)` (or a shared `PreferenceCategoryTitle` in `ui/common/preference`) — S.
- `ui/romdetails/ui/RomModsUi.kt:127-131, 204-208, 287-291, 310-314` — four `CaptionText(..., Modifier.padding(horizontal = 16.dp, vertical = 8.dp))` notes — Add `private fun SectionNote(text)` — S.
- `ui/romdetails/ui/RomModsUi.kt:184-188` + `ui/common/component/LuaScriptOptionControl.kt:260-266` — "layout name, or Unnamed layout" fallback is written in both (`RomModsUi` reuses the string `lua_script_option_layout_unnamed` for a *wide layout*) — Expose one `@Composable layoutDisplayName(name, unnamedNumber = 0)` and rename the string to `layout_unnamed` — S.
- `ui/common/ProvideEnabledContentColor.kt:13` + `ui/settings/fragments/ControllerStylePreferencesFragment.kt:184` — disabled alpha `0.38f` literal — Name it `const val DISABLED_CONTENT_ALPHA = 0.38f` next to `ProvideEnabledContentColor` — S.

### Domain
- `domain/model/layout/WideTopScreen.kt` (`getLetterboxedRect` callers) + `domain/model/layout/ScreensDrawing.kt:58, 74` — `SCREEN_WIDTH / SCREEN_HEIGHT.toFloat()` recomputes the 4:3 ratio although `consoleAspectRatio` already exists in `NDS.kt` — Use `consoleAspectRatio` — S.
- `domain/model/layout/ScreensDrawing.kt:59` — `frameWidth.coerceAtLeast(SCREEN_WIDTH)` evaluated twice in one expression — Bind to a local — S.
- `domain/model/rom/config/RomModsConfig.kt:7-9` + `domain/model/layout/WidescreenAspect.kt:3-4` — `MIN/MAX_WIDE_VIEW_WIDTH` live in `rom.config` but describe the core's 3D view like `SCREEN_WIDTH` in `NDS.kt` — Move them to `NDS.kt` — S.

## 3. Consolidation

- `domain/model/emulator/LuaScriptSetup.kt` — one file holds five unrelated JNI-facing classes (`LuaScriptSetup`, `LuaScriptOptionValue`, `LuaScriptDataSnapshot`, `LuaScriptModule`, `LuaScriptImage`) — Split per class, or rename the file `LuaScriptNativeModels.kt` with one header comment — S.
- `impl/LuaScriptHeader.kt` is imported by `ui/romdetails/model/RomLuaScriptsUiModel.kt:4`, `ui/romdetails/ui/RomModsUi.kt:54` and `RomDetailsViewModel.kt:181` — UI and UI models depend on an `impl` class while its contents (`LuaScriptOption`) are in `domain` — Move the `LuaScriptHeader` data class to `domain/model/emulator` and keep only the parser in `impl` — M.
- `ui/layouteditor/LayoutEditorItemControls.kt:56-62, 418, 487-511` + `ui/layouteditor/LayoutEditorView.kt:86, 528-530` — aspect-ratio lock state is split: the controls own `ScreenAspectRatio`/`lockedRatio` and push a `Float?` into `LayoutEditorView.selectedScreenAspectRatio`, and `setPropertyValue` has to reconcile the two — Keep the enum on the editor view (it owns `selectedView`) and let the controls only render it — M.
- `ui/layouteditor/LayoutEditorManagerView.kt:524-551` — `openGridDialog` is the only menu entry built from a View-binding dialog while the other editor dialogs are Compose — Rewrite it in Compose next to `LayoutPropertiesDialog`, or move it with `LayoutEditorGridSettings` into `LayoutEditorGridDialog.kt` — M.
- `ui/layouteditor/LayoutEditorView.kt:109-153` — grid-settings loading/listening (5 fields + `loadGridSettings`) is a self-contained concern inside a 900-line view — Extract a `SnapGrid(context)` holder exposing `isEnabled`, `cellPx` and attach/detach — M.
- `ui/layouteditor/LayoutEditorView.kt:273-349` — grid/fold/handle/size-label drawing (`dispatchDraw`, `drawGrid`, `insetLine`, `drawHandle`, 8 paints) is ~90 lines of pure drawing — Move into a `LayoutEditorOverlayDrawer` — M.
- `ui/layouteditor/LayoutEditorActivity.kt:164-176, 317-350` — nine `latest*`/`reported*` fields plus `updateLayoutVariantInputs` form a change detector for five inputs — Replace with `data class LayoutVariantInputs(uiSize, insets, folds, orientation, displays)` and a single `reported` field compared per property — M.
- `ui/layouteditor/LayoutEditorViewModel.kt:261-271, 361-399` — `wideAspect = _currentLayout.value?.layout?.wideAspect` with the same "A variant borrowed from a similar one shows its ratio" comment is repeated three times — Extract `private fun shownWideAspect()` — S.
- `domain/model/layout/WideTopScreen.kt:89` — `buildCentredRect` is a public top-level helper used only by `WideTopScreen.kt` and `ScreensDrawing.kt` — Make it `internal` — S.

## 4. Comments (multi-sentence, history-narrating or restating)

Proposed single-sentence replacements (or deletion). Only comments longer than one sentence are listed.

### ui/common/view
- `PixelButtonPainter.kt:17-22` (4 sentences) → "Draws a RaisedButtonView's parts as cached pixel-art bitmaps snapped to a whole-pixel grid, with labels and icons on a finer detail grid."
- `PixelButtonPainter.kt:25-27` → "One part's cached lip, face and pressed face bitmaps and where they're drawn."
- `PixelButtonPainter.kt:30-32` → "A boolean cell grid where out-of-range cells read as false."
- `PixelButtonPainter.kt:47-50` → "A part's shape snapped to its own cell grid, with a one-cell border and room for the lip."
- `PixelButtonPainter.kt:53-56` → "Cells whose four neighbours are also in the shape, on the cell grid and the detail grid."
- `PixelButtonPainter.kt:75-78` → "Picks the cell size so small views still have at least `minCells` cells across; call before `lipHeight` or `update`."
- `PixelButtonPainter.kt:125-128` → "Trims each D-pad arm so at least one empty cell separates it from the centre; hit areas are unchanged."
- `PixelButtonPainter.kt:187-190` → "Single letters share the biggest big-font scale that fits all of them; others fall back to the small font."
- `PixelButtonPainter.kt:213-216` → "The biggest scale that keeps `icon` within `ICON_SIZE` of the face's smaller side, never below 1."
- `PixelButtonPainter.kt:219` → delete (restates the `max`).
- `PixelButtonPainter.kt:261, 265, 270-271, 298, 307, 364, 385` → first clause only, e.g. `:270-271` → "Hand-drawn icons are tried with, then without, their shadow before the vector icon is rasterized."
- `PixelButtonPainter.kt:407-410` → "Rasterizes `draw` onto a cell grid; a cell is in when at least half-covered."
- `PixelButtonPainter.kt:435-438` → "Left or top edge of a `size`-cell box centred on `centre`, in half cells, rounding up/left."
- `PixelButtonPainter.kt:441-444` → "Visits every ink cell of `icons` laid side by side with a one-cell gap; stops when `cell` returns false."
- `PixelButtonPainter.kt:458-461` → "Biggest scale up to `maxScale` at which `icons` (and their shadow) land entirely on `interior`, or 0."
- `PixelButtonPainter.kt:477-480` → "Stamps `icons` into `ink` (and `inkShadow`) at the biggest fitting scale; returns false if none fits."
- `PixelButtonPainter.kt:508, 510, 512` → one clause each, e.g. "Max icon size as a share of the face's smaller side."
- `RaisedButtonView.kt:17-21` → "Draws buttons in `appearance`'s shading in a square design space of `designSize` units scaled to the view."
- `RaisedButtonView.kt:75-78` → "Replaces the parts (and design size) while keeping the view, its listener and selection."
- `RaisedButtonView.kt:140` → "Rings are clipped to the shape, so only the inner half of the stroke shows."
- `RaisedButtonView.kt:151-155` → "Shrinks or raises the faces so a pixel lip of fixed depth still fits inside the view."
- `RaisedButtons.kt:13-16` → "Builders for the raised on-screen buttons, whose shapes match the input handlers' hit areas."
- `RaisedButtons.kt:20-22` → "Lip depth and pressed travel in design units for one design size."
- `RaisedButtons.kt:31-34` → "How far the D-pad/ABXY move out at spacing 1; the design box grows by twice this."
- `RaisedButtons.kt:36-39` → "Design box size for the D-pad/ABXY at `spacing`, 512 at 0 to 768 at 1."
- `RaisedButtons.kt:49-52`, `:80-83` → "Parts of the D-pad/ABXY at `spacing` (0..1), drawn in a `spacedDesignSize` box."
- `RaisedButtons.kt:113-116` → "Rounded square with `iconRes`; `offIconRes` or a dimmed `iconRes` is shown while off."
- `RaisedButtons.kt:124, 127, 157` → one clause each.
- `ControllerAppearance.kt:7-9` → "Colours of one button; `edge` is the lip, ring or inner ring depending on shading."
- `ControllerAppearance.kt:17-20` → "Colours of one pixel-art button by role."
- `ControllerAppearance.kt:32-37` (5 sentences) → "Pixel-art parameters: 8-bit uses 4dp cells and three shades, 16-bit 2dp cells plus lip side, dither and glint."
- `ControllerAppearance.kt:74-77` → "Parts that share colours despite different inputs (the D-pad centre with its arms)."
- `ControllerAppearance.kt:107, 117` → "16-bit palette: outline, lip, shadow, base, highlight, glint, ink, ink shadow." / "8-bit palette: shadow, base, highlight, ink, ink shadow."
- `ControllerAppearance.kt:150-152` → delete once §1 (placeholder colours) is fixed.
- `PixelIcons.kt:6-9` → "A hand-drawn bitmap (`#` = ink) with an optional smaller `compact` version."
- `PixelIcons.kt:33-37` → "Hand-drawn icons and fonts for pixel buttons, looked up by drawable; big icons also have a ≤9×9 compact version."
- `PixelIcons.kt:94` → "Open eye with a highlighted iris (touch controls shown)."
- `SingleButtonIcons.kt:7-9` → "Icons of the single buttons that show an icon, shared by emulator, editor and style previews."
- `SingleButtonIcons.kt:29-31` → "The icon a toggle shows while off; toggles without one dim their normal icon."

### ui/common (other)
- `LayoutComponentViewBuilder.kt:10-14` (3 sentences) → "Spreads out a view's buttons from 0 to 1; only the D-pad and face buttons override it."
- `LayoutComponentView.kt:19-21` → "See `PositionedLayoutComponent.spacing`; setting it does not redraw."
- `component/text/CaptionText.kt:38` → "Keeps the current content alpha so disabled items' captions dim too."
- `component/dialog/TextInputDialog.kt:72` → "The keyboard only opens once the dialog window is focused."
- `component/dialog/TextInputDialog.kt:126` → "Opens the keyboard immediately instead of only focusing the field."
- `component/LuaScriptOptionControl.kt:41-45` (3 sentences) → "Switch, slider or dropdown for one script option; `value` is the option's canonical text and sliders report on release."
- `component/LuaScriptOptionControl.kt:189-191` → "Dropdown of “Don't change” plus the player's layouts; a deleted layout shows as missing."
- `component/LuaScriptOptionControl.kt:256-258` → "The layout's name, or a (numbered) “Unnamed layout”."

### ui/layouteditor
- `LayoutEditorItemControls.kt:43-46` → "Controls for the selected item: a pill beside it or an expandable full panel, remembering the last mode."
- `LayoutEditorItemControls.kt:59` → "The wide variant's own ratio, offered only by its top screen."
- `LayoutEditorItemControls.kt:102-105` → "Refills the controls after the variant's widescreen ratio changed."
- `LayoutEditorItemControls.kt:124-127` → "Repositions and refills the controls after a layout pass."
- `LayoutEditorItemControls.kt:164-167` → "The ratio the selected screen is locked to, or null when free or a control is selected."
- `LayoutEditorItemControls.kt:170-174` (3 sentences) → "Applies a value from the value dialog to `component`, reselecting it if the layout was reshown meanwhile."
- `LayoutEditorItemControls.kt:177-178, 185-186, 291-292, 308, 358, 473` → first clause only.
- `LayoutEditorItemControls.kt:331-334` → "Fills the panel from the selected item; listeners are muted meanwhile."
- `LayoutEditorItemControls.kt:420-423` → "Screen size at an integer DS scale; a wide top screen keeps the DS height instead."
- `LayoutEditorItemControls.kt:436-439` → "Biggest size that fits the editor at the locked ratio, or the current proportions."
- `LayoutEditorItemControls.kt:455-458` → "The Wide chip's menu: the ratios, plus Off in a wide variant."
- `LayoutEditorItemControls.kt:513-516` → "Places the pill above, below or inside the item, moved aside to avoid the floating menu button."
- `LayoutEditorItemControls.kt:580-583` → "Pushes the panel past the floating menu button, measured from its unoffset docked position."
- `LayoutEditorManagerView.kt:80-81` → "An editor dialog opened or closed; `onEditorSettled()` must follow once the editor is back to size."
- `LayoutEditorManagerView.kt:108-110` (3 sentences) → "Editor size when the dialogs opened, which values entered in them are applied against."
- `LayoutEditorManagerView.kt:229-230` → "Re-arms the top screen's selection across the reshow the ratio change causes."
- `LayoutEditorManagerView.kt:239` → "Includes the status bar so the editor's own controls stay clear of it."
- `LayoutEditorManagerView.kt:263-264` → "Refreshes the item controls whenever the shown variant's widescreen ratio changes."
- `LayoutEditorManagerView.kt:296-298` → "Called by the host once the dialogs are closed and the editor is back to size."
- `LayoutEditorManagerView.kt:500` → "Stored again because the other display's editor may have changed while the menu was open."
- `LayoutEditorManagerView.kt:575-577` (pre-existing, 2 sentences) → "Shows the dialog on this view's window, needed on a secondary display."
- `LayoutEditorView.kt:35, 37` → one clause each.
- `LayoutEditorView.kt:60-64` (3 sentences) → "Baseline rect and spacing the spacing slider works from, retaken if the view was moved or resized since."
- `LayoutEditorView.kt:75` → "The item deselected by the last reshow, until anything is (de)selected."
- `LayoutEditorView.kt:195, 233, 267, 318, 352, 387, 432, 440, 463, 466, 513, 518, 567-568, 578, 599, 623, 645, 763, 791, 811-812, 885, 892` → first sentence only; `:811-812` → "Index 0 inserts underneath, matching DSRenderer's stacking."
- `LayoutEditorView.kt:399-401, 406-409, 457-460, 480-483, 493-495, 507-509, 532-534, 552-554, 661-663, 669-671, 687-689, 716-719, 725-727, 739-741, 756-760, 837-840, 846-850, 866-869, 900-902` (KDocs) → one sentence each, e.g. `:756-760` → "Sets the selected D-pad/ABXY spacing, growing the view around its centre and clamping it to the editor."
- `LayoutEditorSnapping.kt:23-33` (@property block) → one clause per property; `:52-55, 62-65, 81-84, 117-121, 148-150, 157-159, 168-170` → one sentence each; `:68-69, 72, 91, 94, 99, 101, 123-124, 131` → first clause only.
- `LayoutEditorGridSettings.kt:201-203` → "Snap grid setting shared by the on-device and external editors."
- `LayoutEditorGridSettings.kt:218-221` → "Registers `onChanged`; keep the returned listener and pass it to `removeListener`."
- `LayoutEditorPanelScrollView.kt:244-247` → "Scroll view capped at 480dp wide and half its parent's height."
- `LayoutEditorActivity.kt:63` → "Makes a new layout wide at this ratio (`WidescreenAspect.key`)."
- `LayoutEditorActivity.kt:164-167` (4 sentences) → "Layout-variant inputs are held back while an editor dialog is open, so the keyboard can't swap the layout being edited."
- `LayoutEditorActivity.kt:317-320` → "Pushes changed variant inputs to the view model after storing the shown layout; held while a dialog is open."
- `LayoutEditorActivity.kt:356-358` → "After the dialogs close, waits until the keyboard is gone or the editor is back to size."
- `LayoutEditorViewModel.kt:45-47` → "The configuration as opened or last saved, for revert and unsaved-change detection."
- `LayoutEditorViewModel.kt:50-52` → "Ratio of a new wide layout, applied to each variant until the first save."
- `LayoutEditorViewModel.kt:121, 208, 217` → first clause only.
- `LayoutEditorViewModel.kt:224-227` → "Whether the configuration differs from the opened/saved one; store editor changes first."
- `LayoutEditorViewModel.kt:293-297` (4 sentences) → "Sets the shown variant's widescreen ratio (re-placing its top screen) and returns whether the shown layout changes."
- `LayoutEditorViewModel.kt:308-311` → "`layoutVariants` with the shown variant saved at `wideAspect`, its top screen widened."

### domain
- `model/EmulatorConfiguration.kt:26-28` (3 sentences) → "Top-screen 3D view width at launch in DS pixels, or 0; changed in play via `EmulatorManager.setWideViewWidth`."
- `model/NDS.kt:5-6` → "Empty lines between the stacked screens in the frame texture; must match MelonInstance.cpp."
- `model/NDS.kt:8` → "Frame texture height in DS pixels before upscaling."
- `model/NDS.kt:11` → "Base emulator frame rate; must match EMULATOR_TARGET_FPS in FramePacing.h."
- `model/render/PresentFrameWrapper.kt:13-14` → "Top screen width in DS pixels; wider than SCREEN_WIDTH when widescreen is on."
- `model/render/PresentFrameWrapper.kt:16-18` → "Lua drawing overlay texture (2x, premultiplied) and the OVERLAY_SCREEN_* bits saying which screens use it."
- `model/render/PresentFrameWrapper.kt:20` → "Emulator frame sequence number, 0 when the frame is invalid."
- `model/RendererConfiguration.kt:9, 11` → "Draws 2D layers on their own thread (read natively)." / "Redo display capture at internal resolution (OpenGL/compute, read natively)."
- `model/layout/LayoutConfiguration.kt:15` → "True when any saved variant has a widescreen ratio."
- `model/layout/UILayout.kt:6-7` → "Widescreen ratio of this variant, or null for a normal 4:3 top screen."
- `model/layout/PositionedLayoutComponent.kt:10-13` → "D-pad/ABXY spacing from 0 to 1; `rect` grows so the buttons keep their size."
- `model/layout/ScreensDrawing.kt:7-15, 22-25` → one clause per @param; `:29-42` (7 sentences incl. bullets) → "How DSRenderer draws both screens from a `frameWidth`-wide frame: the screen in the layout's top rect gets the whole half, the other its middle SCREEN_WIDTH columns."; `:66-71` (5 sentences) → "Draw rect of the top-slot screen: letterboxed in a wide layout, grown by `getWideTopScreenRect` otherwise."
- `model/layout/WideTopScreen.kt:7-9` → "Tolerance in px below which a rect counts as already wide."
- `model/layout/WideTopScreen.kt:12-18` (5 sentences) → "Where a top screen drawn at `aspectRatio` goes for layout rect `rect`: centred if already wide, else grown sideways up to the surface and bottom screen, else shortened."
- `model/layout/WideTopScreen.kt:29-30, 41-42` → first clause only.
- `model/layout/WideTopScreen.kt:48-50` → "Biggest `aspectRatio` rect centred in `rect`, or `rect` itself if within tolerance."
- `model/layout/WideTopScreen.kt:61-64` → "`layout` with each edited screen layout's top screen widened for `aspect` on `variant`'s surfaces."
- `model/layout/WideTopScreen.kt:90` → "Edges are rounded, not sizes, so the rect stays centred."
- `model/layout/WidescreenAspect.kt:9-12` (3 sentences) → "Widescreen ratios in `R.array.widescreen_aspect_options` order; `viewWidth` is the even 3D-view width that gives each."
- `model/layout/WidescreenAspect.kt:22` → "Drawn shape, derived from the rounded width."
- `model/rom/config/WidescreenLaunch.kt:7-17` (6 sentences) → "How widescreen starts for a session; `isWideSession` is fixed at launch and the width follows the shown layout."; each @param → one clause.
- `model/rom/config/WidescreenLaunch.kt:22-25, 41-44` → "Width for the shown variant: its ratio, 0 for an unwide variant of a wide layout, else the Mods tab's width." / "Resolves the launch from the Mods config, the picked wide layout and the renderer."
- `model/rom/config/RomModsConfig.kt:7, 12-13, 17-18, 21` → one clause each, e.g. `:17-18` → "Wide layout used while widescreen is on, or null for automatic."
- `model/rom/config/RomLuaScript.kt:3-7` (3 sentences) → "A script in the game's folder with the player's option values by key; unknown keys are ignored."
- `model/rom/config/RomConfig.kt:12` → "Whether the player left the screens swapped by hand."
- `model/emulator/LuaScriptOption.kt:10-13, 18-21, 43-46, 57-59, 67-69, 74, 95, 111-113, 134-138, 145, 152-154, 169-171` → one sentence each, e.g. `:134-138` → "A layout option whose value is a layout UUID or empty for “Don't change”; existence is checked only when the script asks."
- `model/emulator/LuaScriptSetup.kt:3-9, 12, 19-22, 35, 41-44, 51-54, 61-64` → one clause per class/param, e.g. `:41-44` → "A snapshot of a script's melon.storage to save; empty `data` means delete the file."
- `model/emulator/LuaLayoutChoice.kt:5-11, 14-17` → "A layout offered by a layout option; `unnamedNumber` distinguishes unnamed layouts (0 when unique)." / "Layouts in list order, numbering unnamed ones when there are several."
- `model/emulator/LuaScriptStatus.kt:3-6` → "A session script's header data, running state, last error and option values."
- `services/EmulatorManager.kt:22-29` (@params) → one clause each; `:38-40, 43-46, 48-50, 54-57, 60-64, 72-74` → one sentence each, e.g. `:60-64` → "Restarts (or starts) a script with new option values; returns false if it doesn't exist or couldn't be read."

### ui/romdetails
- `RomDetailsActivity.kt:104-105` → "Refreshes state the emulator, settings or the script folder may have changed in the background."
- `RomDetailsUiMapper.kt:41-42` → "Cached per folder because resolving the path uses reflection and provider queries."
- `RomDetailsUiMapper.kt:45-46` → "Prefer the resolved path; a bare “/” means the volume is unknown, so show the in-volume path."
- `RomDetailsUiMapper.kt:56-58` → "Path part of a tree document ID, like `LuaScripts/White2` for `primary:LuaScripts/White2`."
- `RomDetailsViewModel.kt:77-78` → "Game key for script data, read lazily from the ROM header."
- `RomDetailsViewModel.kt:81, 95, 99, 151-152, 201-202, 238-239, 295-296, 340, 366-367` → first clause only, e.g. `:151-152` → "File permissions are taken only when a file is picked, since a lost grant would throw on every save."
- `RomDetailsViewModel.kt:124-126, 187-190, 218-222, 277-280, 325-328` (KDocs, 2-3 sentences) → one sentence each, e.g. `:218-222` → "Syncs the script list with the linked folder and reads headers and saved-data sizes."
- `model/RomLuaScriptsUiModel.kt:7-16` → keep @params but one clause each (`isFolderMissing` is two sentences).
- `model/RomConfigUiModel.kt:17, 19` → one clause each.
- `ui/RomModsUi.kt:125, 140, 151, 189, 192, 196, 211-213, 342-344, 402` → first clause only (`:192` → "Backing out keeps the stored pick.").

## 5. Polish

- Spelling: `PixelButtonPainter.kt` (`labelCentre`, `iconCentre`, `centredStart`, `centreIndex`), `WideTopScreen.kt` (`buildCentredRect`, `centreX`), `ControllerStyle.SUPER_COLOURS` use British spelling while the pre-existing code and the new `LayoutEditorSnapping.centeredStart`, `centerSelectedViewHorizontally`, `ColorGroup`, `PixelColors` use American — Standardise identifiers on American spelling; comments may stay — S.
- `ui/common/view/RaisedButtonView.kt:177-186` — `setPressed` presses every pressable part (touch feedback) while `setPressedInputs` presses by input, with no doc distinguishing them — Add one sentence of KDoc to `setPressed` — S.
- `ui/common/view/PressedInputsView.kt` — 7-line interface in its own file with no KDoc — Add one sentence of KDoc — S.
- `ui/common/view/RaisedButtonView.kt:44` — `Part` documents `pixelIcon`/`pixelOffIcon` with a `//` comment inside the parameter list; the rest of the class uses KDoc — Convert to a `@property` block — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:50-53` — constructor parameters documented with `//` line comments instead of KDoc `@param` — Move to class KDoc — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:240-241` — fully-qualified `androidx.appcompat.R.attr.colorPrimary` / `com.google.android.material.R.attr.colorOnPrimary` inline — Import with aliases like the rest of the codebase — S.
- `ui/layouteditor/LayoutEditorItemControls.kt:37` — `SIZE_STEP_PX = 16` is a raw pixel step while every other editor size is dp-converted — Make it dp (`context.dpToPixels(8f)`) — S.
- `ui/layouteditor/LayoutEditorManagerView.kt:312-321` — `updateBackground` still uses `e?.printStackTrace()` (pre-existing) while new code logs nothing — Replace with `Log.w(TAG, ...)` or drop — S.
- `ui/layouteditor/LayoutEditorView.kt:21, 32` — wildcard imports `domain.model.*` and `kotlin.math.*` (pre-existing) next to explicit imports added by the branch — Expand to explicit imports while the file is reworked — S.
- `ui/layouteditor/LayoutEditorView.kt:254-256` — `setOnClickListener` override silently stores into `otherClickListener` (pre-existing) — Rename to `setOnEmptyAreaClickListener` or document in one sentence — S.
- `ui/layouteditor/LayoutEditorSnapping.kt:34` — `SnapAxis` is a plain `class` while sibling `SnapSpan`/`SnappedEdge` are `data class`es — Make it a `data class` — S.
- `ui/layouteditor/LayoutEditorSnapping.kt:71, 152` + `PixelButtonPainter.kt:439` — Java `Math.round`/`Math.floorDiv` mixed with Kotlin `roundToInt()`/`floorDiv()` elsewhere in the same files — Use the Kotlin stdlib forms — S.
- `ui/layouteditor/LayoutEditorActivity.kt:131-176` — `onEditorDialogShown/Hidden` and 9 tracking fields are declared inside the listener object and between methods, far from `onCreate` — Group the settle-state fields together above the listener — S.
- `ui/layouteditor/LayoutEditorGridSettings.kt:195-199` — `LayoutEditorGridSize` enum has no KDoc, and its `dp` is converted to px in `LayoutEditorView` with a `MIN_GRID_CELL_PX` clamp — Add `cellPx(context)` on the settings so the view doesn't own that math — S.
- `ui/common/component/LuaScriptOptionControl.kt:268` — `MAX_TICKED_STEPS` is declared at the bottom of the file; the branch's other files put `private const val`s at the top — Move to top — S.
- `ui/common/component/LuaScriptOptionControl.kt:57-60` — the four `*Control` functions take parameters in differing orders and `modifier` is buried mid-list, unlike Compose convention — Reorder to `(option, value, onValueChange, modifier, enabled, horizontalPadding)` — S.
- `ui/romdetails/ui/RomModsUi.kt:186` — reuses `R.string.lua_script_option_layout_unnamed` for a *wide layout* name — Rename the resource to `layout_unnamed` — S.
- `ui/romdetails/RomDetailsViewModel.kt:62-64, 76` — `_luaScriptFolderState`, `_areLuaScriptsBlockedByHardcore`, `_luaScriptDataSizes` use the `_` backing-field prefix but have no public counterpart — Drop the underscore — S.
- `ui/romdetails/RomDetailsViewModel.kt:96-97` — `.map<List<LayoutConfiguration>, List<LayoutConfiguration>?> { it }.onStart { emit(null) }` is an awkward type-widening — Use `map { it as List<LayoutConfiguration>? }` or a nullable `StateFlow` — S.
- `ui/romdetails/RomDetailsViewModel.kt:99` — comment "combine() takes up to 5 flows" justifies a pre-combined pair — Use the vararg `combine` overload or a small input data class — S.
- `ui/romdetails/model/RomConfigUiModel.kt:17-19` — `isWidescreenSupported` and `wideLayouts` are app-wide facts stuffed into the per-ROM config model — Move them to a sibling `RomModsUiModel` beside `luaScripts` — S.
- `domain/model/layout/WidescreenAspect.kt:13-16` — enum keeps `widthRatio`/`heightRatio` only to compute `viewWidth`, while `key` repeats "16:9" as a string — Derive `key` from the two ints — S.
- `domain/model/emulator/LuaScriptSetup.kt:23-39` — `LuaScriptOptionValue.TYPE_*` are raw `Int` consts "read by native code" in a domain model — Make them an `enum class LuaScriptOptionType(val nativeId: Int)` and map at the JNI boundary — S.
- `domain/model/NDS.kt:5-12` — JNI-coupled constants (`SCREEN_GAP`, `FRAME_TEXTURE_HEIGHT`, `EMULATOR_TARGET_FPS`) with "must match X.cpp" comments live in `domain` — Move to `common/` next to other native-facing constants — S.

## 6. Correctness smells (confident only)

- `ui/layouteditor/LayoutEditorItemControls.kt:464, 470` — `WidescreenAspect.ordinal` is used as the `PopupMenu` item id, so `RATIO_16_10` gets id `0 == Menu.NONE`; it works today only because `findItem(0)` still matches, and `setGroupCheckable(Menu.NONE, …)` happens to target group 0 — Use `it.ordinal + 1` for item ids — S.
- `ui/romdetails/RomDetailsViewModel.kt:293-300` — when the same folder is re-picked and `takeLuaScriptFolderPermission` fails, the failure is swallowed and `syncLuaScripts()` runs anyway, unlike the new-folder path which emits `FolderPickFailed` — Report the failure in both branches — S.
- `ui/common/view/RaisedButtonView.kt:89-92` — icons are tinted once in `setParts` with the part's label colour; for `SOFT_TINT`/`OUTLINE` the pressed face colour differs but the icon tint never changes, and for pixel styles the tint is dead work — Tint in `drawFace` (or skip for pixel styles) — S.

## Top 5 highest-value items in this area

1. **Merge the near-identical dropdown and switch rows in Compose** — `LuaScriptOptionControl.ChoiceControl`/`LayoutControl` into one `DropdownControl`, and `RomModsUi.WidescreenSwitchItem`/`LuaScriptItem`/`InfoItem`/`ScriptFolderItem` onto the existing `ui/common/preference` items (`SwitchItem` gaining `caption`/`enabled`) — removes ~150 lines of copy-pasted UI (M).
2. **Collapse the pixel painter's repeated geometry math** — single `ICON_SIZE` shared with `RaisedButtonView`, one `nominalDetailSize`, a `PartGrid.toDetail()` helper, `fitScale` returning a placement reused by `stamp`, and `buildPart` split into a `PartShader`; drop the single-entry `ColorGroup` enum and the placeholder `ControllerButtonColors` for pixel styles (M).
3. **De-duplicate the layout editor's snapping/clamping/centre math** — `clampInside` ↔ `LayoutEditorSnapping.clamp`, centre buttons ↔ `centeredStart`, a shared `minSize(ratio)` for `snapResize`/`constrainSize`, `otherScreenOf()`, `Anchor` edge helpers, and one overlap helper in `LayoutEditorItemControls`; the comments already say each pair "must be the same" (M total).
4. **Move `LuaScriptHeader` to `domain` and split `LuaScriptSetup.kt`** — `RomModsUi`, `RomLuaScriptsUiModel` and `RomDetailsViewModel` import from `impl`, and one domain file holds five unrelated JNI structs (M).
5. **Comment trim pass** — ~120 multi-sentence comments across `PixelButtonPainter`, `LayoutEditorView`, `LayoutEditorItemControls`, `RomDetailsViewModel`, `ScreensDrawing`/`WideTopScreen`, `LuaScriptOption` and `EmulatorManager`, with replacements listed in §4 (L total, S each).
