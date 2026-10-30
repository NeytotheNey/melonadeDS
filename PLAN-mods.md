# Plan: per-game mods (WideMelon + Lua)

## Context

Other melonDS forks are adding mods. MelonMix (KHMelonMix) is dropped: it's a set of hand-written C++ plugins for a few games, tied to the Qt desktop app and to KH HD asset packs. We're taking two things instead:

- **WideMelon** (pruefsumme): widens only the 3D view. 3D renders wider than 256 px and the projection is squeezed so the original view fills the middle, while 2D, menus and touch stay native in the centre. The Android core version is 7 commits, about 200 lines, in `pruefsumme/melonDS-android-lib` branch `widemelon` (`43fa0c80`..`6fd7f092`): `GPU3D.*`, `GPU3D_OpenGL.*`, `GPU_OpenGL.*`, `GPU_OpenGL_shaders.h`, `GPU.cpp`, new `WideMelon.h`. GPL-3.0-or-later, same as melonDS. Classic OpenGL renderer only.
- **Lua scripting**, like DraStic. DraStic's API is documented in its own readme (`C:\Dev\drastic\java-src\resources\res\raw\drastic_readme.txt`, section 8) and is small:
  - scripts in `DraStic/scripts/`, loaded as `<ROM name>.lua` or `default.lua`, with one global "Enable Lua" switch
  - callbacks `on_load(game)`, `on_unload()`, `on_frame_update()` (called once per frame; the emulator drives the script)
  - `drastic.get/set_buttons`, `drastic.get/set_touch`, `drastic.C.BUTTON_*`, `drastic.get_path()`
  - memory: `drastic.get/set_ds_memory_<arm9|arm7>_<8|16|32>(addr[, value])`
  - Android: `android.get_axis_lx/ly/rx/ry()`, `android.get_rotation()`, `android.set_layout(n)`, `android.show_overlay(0|1)`, `android.set_screen_swap(0|1)`
  - Lua 5.3, statically linked; no drawing API, no savestate callback

**Decision: our own API, with no DraStic compatibility.** DraStic's API works but has gaps: no savestate hook, no drawing, touch packed into one integer, globals instead of a module. Our own API fixes those. Anyone with old DraStic scripts adapts them to our API, and the scripts we ship will be our own. DraStic is only a design reference.

**Per game, not global.** Mods live in a new "Mods" section on the ROM details screen and are saved in `RomConfig`. Nothing runs unless it's switched on for that game.

## Research findings (2026-10-28)

**DraStic's real Lua setup** (decompiled `lua_init` 0x1811f8 and the `luaL_Reg` table at 0x2344a0 in `C:\Dev\drastic\decoded\lib\arm64-v8a\libdrastic_arm64.so`; Ghidra base 0x100000). This is reference only, since we're not doing compatibility:
- Button bits: UP 0x1, DOWN 0x2, LEFT 0x4, RIGHT 0x8, A 0x10, B 0x20, X 0x40, Y 0x80, L 0x100, R 0x200, START 0x400, SELECT 0x800, FFWD 0x20000, TOUCH 0x80000000. Pressed = bit set. Ours is active-low with A=0, B=1, Select=2, Start=3, Right=4, Left=5, Up=6, Down=7, R=8, L=9, X=10, Y=11. Our `melon.KEY.*` uses pressed = bit set, like DraStic, and the engine flips it to our active-low mask. That's friendlier for script writers.
- The readme is wrong about one name: the constant is `BUTTON_FFWD`, not `BUTTON_FAST_FORWARD`. Letting scripts trigger fast-forward is worth copying. For us it's app-side (`Input.FAST_FORWARD` is -1), so it would be an event to Kotlin.
- `get_buttons()` returns only bits `0x20fff`, plus TOUCH from a separate flag. `set_buttons()` replaces those bits and leaves the rest alone.
- `get_touch()` returns `x << 16 | y`. `set_touch(x, y)` stores the coordinates but doesn't press. Pressing is the TOUCH bit.
- The 17 `drastic.*` functions match the readme. The `android.*` table is filled in by relocations at load, and its names match the readme (`get_axis_lx/ly/rx/ry`, `get_rotation`, `set_layout`, `show_overlay`, `set_screen_swap`).
- Libraries opened: `_G`, `package`, `table`, `io`, `string`, `math`, `utf8`, **`bit32`**. No `os`, `debug` or `coroutine`. DraStic lets scripts write files through `io`. Our sandbox leaves `io` out. If scripts need to save data, add a small `melon.storage` API later that's limited to the game's own folder.
- Timing: `on_frame_update()` runs at the end of each frame (`system_frame_end`), after input polling. Its button and memory changes apply to the next frame, which is the same as us calling it just before `nds->RunFrame()`.
- No safety at all: `pcall` results are ignored, there's no instruction limit, and errors are silent. Ours will be stricter (see Phase 3).
- Memory: DraStic goes through its ARM9/ARM7 page map, so it sees what that CPU sees. Writes to code pages invalidate its JIT (`recompiler_invalidate`). Writes to non-RAM pages go through the I/O handler.

**JIT safety on our side:** `NDS::ARM9Write8/16/32` and `ARM7Write*` already call `JIT.CheckAndInvalidate` for main RAM and shared WRAM (`NDS.cpp:2117`, `2513`), so writes through the bus are JIT-safe. Two catches:
- `NDS::ARM9Read/Write` don't see ITCM/DTCM. Those are handled one level up in `ARMv5::DataRead/Write` (`CP15.cpp:795-913`). For an ARM9 view that matches DraStic's, check `addr < ITCMSize` and `(addr & DTCMMask) == DTCMBase` ourselves first, and fall back to the bus otherwise. Don't call `DataRead*`, since it adds cycle timing.
- Reads in the I/O range (0x04xxxxxx) can have side effects, like popping the IPC FIFO. Block I/O reads, or warn about them.

AR cheats write through the ARM7 bus (`AREngine.cpp`), which is the same JIT-safe path.

**RetroAchievements** ([hardcore compliance](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html)): "Memory editors, debuggers, and/or scripting/TAS/recorded input playback are strictly prohibited." So Lua must be fully off in hardcore, read-only scripts included. Widescreen is a different case: the page doesn't cover graphical enhancements, and it even exempts widescreen hacks done as cheat files ("External cheat files not used for minor QoL improvements (widescreen hacks, etc.)"). Allow WideMelon in hardcore, with one caveat: it also squeezes `BoxTest`, so the game's own visibility checks give different answers. That's minor, but it's a change the game can see.

**WideMelon onto our core** (scratch `git apply --check` against core `aff6ff57`; patches in the session scratchpad `wm/`):
- **It ports easily, and nothing needs real rework.** `GPU.cpp`, `GPU3D.cpp` and `WideMelon.h` apply cleanly. `GPU3D.h` and `GPU3D_OpenGL.*` need hunks re-placed by hand:
  - We have no `ResolveOpenGlVertexX`; X is worked out inline in `SetupVertex`.
  - Drop the two "coverage fix" hunks, because we don't have that feature.
- `GPU_OpenGL.*` applies but won't compile. It reads `gpu.SwapSelladoBuffer[backbuf]`, a per-back-buffer copy of POWCNT1 bit 15 that their fork records in `AssignFramebuffers`. Add a `u32[2]` to `GPU.h` and one line in `AssignFramebuffers` (`GPU.cpp:285-295`).
- `GPU3D` and `GLRenderer` each keep their own view width. If GL falls back to 256 (width over `GL_MAX_TEXTURE_SIZE`), set `GPU3D.SetViewWidth()` from `GLRenderer::GetViewWidth()`. Otherwise geometry gets squeezed with no wide target behind it. `SetRenderer3D` resets the width to 256, so set it again after every renderer switch.
- The wings rule reads engine A's registers at `Blit` time instead of values latched for that frame. **This clashes with threaded 2D** (`PLAN-threaded-2d.md`, and the core is already moving `Unit` state updates out of the renderer). Latch the values per frame.
- App side, besides what Phase 2 already lists: the frame texture in `MelonInstance.cpp:326-328`/`FrameQueue.cpp:101-121`, `updateRenderer()` (`:703`), the `ScreenshotRenderer` UVs (`:197-215`), the `DSRenderer.kt:232-246` UVs, and `VideoFilterPipeline.kt:100-116`.

**How cngjd/WatermelonDS went past classic GL** (`widescreen` app branch and `lib-widescreen` core branch, AI-written and auto-rebased daily):
- Compute: `ScreenWidth = width*scale`, so the tile grid grows on its own. Polygon X goes through `MapHiresX`/`MapFinalX`, and compositing reuses the GL compositor. **Probable bug:** dispatches use integer division (`ScreenWidth/CoarseTileW` and the like; ours is at `GPU3D_Compute.cpp:998`), so widths like 342 (16:9) leave the right edge unrendered. Pick widths that are multiples of 64 (320, 384, 448, 512…), or round the internal width up and crop.
- Vulkan: a second wide attachment per slot, then the centre copied back into the normal 4:3 image. We don't have a Vulkan renderer, so this doesn't apply.
- Presentation: a new `ScreenTextureUVs.kt`. `widenTopScreenRect` widens the top rect by width/256 around its centre at draw time. If it doesn't fit, it shrinks or clamps instead. Stored layouts never change, and touch mapping is untouched. This is a good model, and simpler than asking users to build 16:9 layouts.
- Width is a single global setting there, not per game. Its README lists pop-in at the edges and black bars in 2D scenes as known issues. Their compute version also skips 2D in the wings and gates them per frame instead of per line.

**Rules:** each phase goes coder subagent → fresh reviewer subagent → fixes → build → phone check. Paths are under `app/src/main/` unless they start with `melonDS-android-lib/`. Core changes are committed in the core submodule's own repo first, then the submodule pointer is bumped here.

## Phase 1: Mods section and per-game storage

- Add `mods: RomModsConfig` to `domain/model/rom/config/RomConfig.kt`, defaulting to everything off:
  - `wideViewWidth: Int?` (null = off)
  - `luaScripts: List<RomLuaScript>` (file name + enabled)
- Wire it through `impl/dtos/rom/RomConfigDto.kt`, `parcelables/RomConfigParcelable.kt` and `ui/romdetails/model/RomConfigUiModel.kt` / `RomConfigUpdateEvent.kt`. Old configs with no `mods` load as all-off, so no migration class is needed. The coder checks that against how the existing DTO versions are handled.
- `ui/romdetails/ui/RomConfigUi.kt`: add a "Mods" group, with a widescreen row (Phase 2) and a scripts row (Phase 4). Hide the rows until their phase lands.
- RetroAchievements hardcore: Lua is fully off (RA bans scripting outright), gated next to cheats (`impl/emulator/EmulatorSession.kt:70`), and the UI says why. Widescreen stays allowed (see the research findings).

Phone check: open a game's details, see the empty Mods group, and toggle back and forth without losing other ROM settings.

**Done (2026-10-29), as planned.** The hardcore note in the UI came with Phase 4, once there were script rows to put it on.

## Phase 2: WideMelon (classic OpenGL)

Re-checked 2026-10-29 against core `38071a81` (threaded 2D landed) and the current app. Scratch clone with pruefsumme's branch and the 7 patches: session scratchpad `wm-core/` and `wm-patches/`.

**How WideMelon works, in short.** The width W (even, 256..768) is set on both `GPU3D` and `GLRenderer`. Each vertex's clip-space X is squeezed by 256/W, so geometry the game would have clipped at the sides lands inside the view, and BoxTest corners get the same squeeze (PosTest doesn't). The GL renderer draws at `W × scale` and stretches X back. Display capture and `GetLine` only take the centre 256. The compositor's output is `W × scale` by `386 × scale`: the top screen's extra columns ("wings") show the 3D image, and the bottom screen sits in the centre with black bars.

**Porting status.** The squashed diff (+199/−23) goes on with `--3way`. Left to do by hand:
- `GPU.cpp`: keep our new `Sync2D()` line in `SetRenderer3D` next to theirs.
- `GPU3D_OpenGL.cpp`: 3 conflicts, all fork drift. Drop their ConvertBgra/coverage-fix helpers and the coverage-fix block in `BuildPolygons`; put the wide X into our inline `x =` in `SetupVertex`. `GPU3D_OpenGL.h`: keep only `GetViewWidth()`.
- `GPU_OpenGL.cpp` needs `gpu.SwapSelladoBuffer`, which only their fork has. This goes away with our own wings rule (below).

**What we do better than WideMelon:**
- **Wings rule per line, from the 2D renderer.** Theirs reads POWCNT1, DISPCNT and BLDCNT once at `Blit` time, so a mid-frame change gives wrong wings, and with threaded 2D the registers at line 192 aren't always what lines 0–191 showed. Instead, the 2D line renderer writes a "3D on BG0, show wings" flag plus the line's brightness effect into the free control-word bits 18–22 (`GPU2D_Soft.cpp:295`), from the same snapshot it draws the line from. The compositor reads it per line. This needs no latching, follows engine A to whichever screen it's on, and drops `SwapSelladoBuffer`.
- **One source of truth for the width.** `GPU3D` asks the renderer for its width, so a GL fallback to 256 (width over `GL_MAX_TEXTURE_SIZE`) can't leave geometry squeezed with nothing wide behind it. `GL_MAX_TEXTURE_SIZE` is read once, not every frame.
- **Off-centre viewports.** The stretch is around x = 0, not the viewport's centre (`GPU3D.cpp:1149`), so games with a viewport that isn't full-screen are misplaced. Map with the viewport centre instead.
- `ProjectX` rounds instead of truncating. The 3D X scroll (`_3dxpos`) is applied to the wings.

**Known limits we accept** (same as WideMelon): edge pop-in in games that cull on the CPU or with PosTest; ortho 3D HUDs and full-screen 3D quads stay 256 wide; 2D-only scenes get black wings; and BoxTest plus the polygon RAM limit make it a change the game can see. Wings show the 3D clear colour where 3D is transparent instead of the 2D layers behind it. Filling them with the 2D backdrop colour is a possible later fix.

**Dual-screen 3D (2026-10-30, not phone-tested).** Games with 3D on both screens (Phantom Hourglass, likely Mario Kart DS) move unit A to the other screen every frame, so each screen only got 3D wings every other frame and they flashed at 30 Hz. The GL compositor now holds them: when unit A's 3D wings show up in the other half of the output, a half that had its own in the last 2 frames reuses them, from a small copy of its wing columns (`HeldWingsTex`, made after drawing while both halves have had 3D lately). Only lines that had 3D wings in the copy and would be black now are held, for at most 2 frames in a row, and a width or scale change drops the copy. Single-screen 3D games never have 3D wings in both halves, so they never copy or hold and look as before. The held wings are a frame old, which lines up with the centre (a capture of the previous frame's 3D).

**The width can change at runtime.** Scale changes already reallocate the GL textures live. If it's changed, latch it at the 3D frame swap (`FlushRequest`, `GPU3D.cpp:2477`) so one frame doesn't mix widths. This phase still only sets it at launch, but the code shouldn't assume it can't change. Savestates don't store the width (correct); loading a state made at another width gives a frame or two of squeezed geometry, which is fine.

Native (`cpp/`):
- Pass the width through `EmulatorConfiguration` → JNI (`MelonDSAndroidConfiguration.cpp`, next to `luaDrawingsInScreenshots`) → `Configuration.h` → `MelonInstance`. `AndroidEmulatorManager.getRomEmulatorConfiguration` copies it from the ROM config. Only applied when the renderer is OpenGL; Compute and Software use 256.
- **Trap:** the renderer is a global setting that can change mid-game, and `updateRenderer()` builds a new `GLRenderer` (width back to 256) and calls `SetRenderSettings` whenever any setting changes. Keep the launch width in `MelonInstance` and apply it on every `updateRenderer()` while the renderer is OpenGL.
- `MelonInstance::runFrame` (`:339-358`, `:344`): the frame texture becomes `W × scale` wide on GL. `Frame` gets a `topWidth`, sent through the present callback (`MelonDSAndroidJNI.cpp:358`) to Kotlin.
- Screenshots (savestate and rewind pictures): crop the top screen to the centre 256 and the bottom to its 256 part. Sizes stay 256×384, so the Kotlin converters and the overlay blend (which works in screenshot pixels) don't change. Quads at `ScreenshotRenderer.cpp:359-380`.

App:
- `DSRenderer.kt:241-257`: top screen samples the whole width, bottom screen samples the centre `256/W` (the core centres it). The UVs are baked into the VBO, so rebuild it when the frame's width changes.
- **Top-screen rect:** widened around its centre at draw time in `DSRenderer.updateScreenCoordinates`, cngjd's way. If the layout's rect is already wide enough (a 16:9 layout), use it as is. Otherwise widen it up to the surface edge without covering the bottom screen's rect, and if that's not enough, shrink it to fit. Stored layouts don't change; fold, cover screen, swap and the external display all go through the same code. Touch is untouched (it maps the bottom view to 256×192).
- **Lua overlay:** it's drawn with the screens' vertex data and its texture is fixed at 512×772. Give it its own quads: the centre 256 of the top screen's rect, and the bottom screen as now. The Lua API stays 256×192 per screen.
- **Filters:** `VideoFilterPipeline.kt:113` decides "upscaled" from `width > 256`; use the height instead, or a 1× wide frame skips MMPX's pre-pass. In `ShaderProgramSource.kt`, `sampleScreen` needs a horizontal clamp so HQx/xBR/FXAA don't pull in the black bars, and LCD/scanlines need a logical-width uniform instead of `LOGICAL_TEXTURE_WIDTH = 256`.
- **Mods tab** (`RomModsUi.kt:94`, where the placeholder is): a widescreen switch, with an aspect choice (16:10 / 16:9 / 21:9, default 16:9) shown when it's on. Width = `192 × aspect` rounded to even and clamped to 256..768 (16:10 → 308, 16:9 → 342, 21:9 → 448). When the global renderer isn't OpenGL, the row says it needs OpenGL (add the renderer to the `combine` in `RomDetailsViewModel.kt:99`). RA hardcore doesn't block it.

Order: core port + fixes (built and checked with a test game before the app side) → native frame path, screenshots and launch wiring → presentation (UVs, rect, overlay, filters) → Mods tab row. Each goes coder → reviewer → fixes → build.

Phone check: a 3D game with known WideMelon results at 16:9, on both a 4:3 and a 16:9 top-screen layout, folded and unfolded. Check that 2D/HUD stays centred, the wings appear only in 3D scenes, touch still maps right, savestates and rewind work, a Lua script's drawings stay on the native area, the filters look right at the edges, and switching renderer mid-game falls back cleanly and comes back.

**Built (2026-10-29), not yet phone-tested.** Core committed as `ee8d5374` in the submodule; app side uncommitted. The emulator can't run the OpenGL renderer, so only the Mods tab was checked there. What differs from the plan above, or wasn't in it:
- Core: the width is latched at the polygon RAM swap (or while geometry is off), and the GL targets resize at `RenderFrame` to fit the polygons being drawn. `GetOutputWidth()` is the compositor's width and can lag the requested one; the app sizes its frame texture from it every frame. Forced-blank lines get white wings.
- A top rect already wider than W:192 shows the frame centred at its exact aspect, not stretched. A bottom screen that shares rows with the top screen stops the widening at its near edge, so a picture-in-picture bottom screen gives a smaller letterboxed top instead.
- HQ2x/HQ4x, xBR, Quilez and Linear got the horizontal clamp too, and only clamp when it's needed, so the 256 path samples exactly as before.
- A stored width that's odd or outside 256..768 counts as off. Turning widescreen off forgets the aspect choice.

Later, not in this phase: changing the width from the pause menu, and the compute renderer (widths must be multiples of 64 there; see the research findings).

## Phase 2b: wide layouts

Decided 2026-10-29 with the user: the player places the wide top screen themselves with the existing layout editor, not a second editor. A layout can be marked as a widescreen layout with a ratio, and each game picks a wide layout that's used while widescreen is on. The layout sets the ratio.

Facts this rests on: `LayoutConfiguration` holds one `UILayout` per `UILayoutVariant` (size, insets, orientation, folds, displays), with rects in absolute pixels per variant, saved in `files/layouts.json` (part of the settings backup). The editor can already draw a screen at any shape; `ScreenAspectRatio` (4:3, 16:9, Free) is only a resize lock, and `guessAspectRatio` works it out again from the rect each time. `DSRenderer.getTopScreenDrawRect` already uses a rect as is (frame centred at its exact aspect) when it's at least as wide as the frame, and only widens it automatically otherwise.

Data:
- Move `WidescreenAspect` to the domain model (layouts need it) and give it a stable key ("16:10", "16:9", "20:9", "21:9") for storage instead of the ordinal.
- `LayoutConfiguration.wideAspect: WidescreenAspect?` (null = a normal layout), through the DTO. Old `layouts.json` files without it load as normal layouts, so no migration; the coder checks that against the layout migrations in `migrations/legacy/layout/`.
- `RomModsConfig.wideLayoutId: UUID?` through the DTO, parcelable, UI model and `RomConfigUpdateEvent`.

Width and layout at launch:
- Widescreen on, a wide layout picked and found, and the renderer is OpenGL: the game uses that layout, and the width comes from its `wideAspect`.
- Widescreen on with no wide layout (none picked, or it was deleted, or it's no longer marked wide): the normal layout with automatic widening and the Mods tab's own ratio, as now.
- Widescreen off, or another renderer at launch: the normal layout at 256.
- Swapping in the layout happens in `EmulatorViewModel.startObservingLayoutForRom` (`wideLayoutId ?: layoutId`). `getPlayerLayoutPicks` treats whichever of the two is active as the ROM's own layout. Lua's `set_layout` and a player's layout pick still override it, as now.
- The width stays fixed for the session. Picking another layout mid-game, or switching renderer, just letterboxes inside the rect.

Renderer: no new flag. A wide layout's top rect is used as drawn through the existing "wide enough" branch. That check gets a 2 px tolerance so editor rounding doesn't push a hand-made rect into automatic widening. A variant the player never edited (generated from the default layout, so 4:3) still gets automatic widening, so a wide layout never shows a tiny letterboxed picture.

Editor:
- `LayoutPropertiesDialog`: a "Widescreen" row: Off / 16:10 / 16:9 / 20:9 / 21:9.
- Setting it widens the top screen once in every saved variant, using the same rule as the automatic placement, so the layout starts from something usable. Changing the ratio later does the same from the current rects. Turning it off leaves the rects as they are.
- In a wide layout, the top screen's aspect lock offers "Wide (20:9)" (the layout's ratio) next to 4:3, 16:9 and Free, and `guessAspectRatio` recognises it. The bottom screen's choices don't change.
- The layouts list and the layout picker show a small "Wide 20:9" label on wide layouts.

Mods tab, with widescreen on:
- "Wide layout": "Automatic" plus the layouts marked wide, through `LayoutSelectorActivity` with a wide-only filter. If there aren't any yet, the row says to make one in the layout editor.
- "Aspect ratio" only shows with Automatic. With a wide layout picked, the row shows the layout's ratio as text instead.
- The "needs OpenGL" note stays.

Phone check: make a wide layout (20:9) and place the top screen differently on the cover screen and unfolded, pick it for a game, and check it's used as drawn in both. Turn widescreen off and the normal layout comes back. Delete the wide layout and the game falls back to automatic. Change the layout's ratio and the game follows it at the next start. Old layouts and settings backups still load, and a Lua `set_layout` still works.

**Change (2026-10-29, after the user tried it): the ratio is per variant, not per layout.** The user wants e.g. 16:10 folded portrait and 20:9 unfolded landscape in one layout.
- `UILayout.wideAspect: WidescreenAspect?` replaces the layout-level field: each variant (folded/unfolded, portrait/landscape, displays) has its own ratio or none (a normal 4:3 top screen at width 256). A layout is "wide" when any saved variant has a ratio. Layouts saved with the old layout-level field get it copied to their saved variants when loaded.
- The top screen's Wide ▾ chip sets the ratio of the variant being edited only, and re-shapes only that variant. The Properties dialog loses its Widescreen row. A new "+" wide layout gives each variant it shows the game's ratio, as now.
- The width follows the variant on screen during play. When the active variant changes (fold, unfold, rotation), Kotlin sends the new width to native, which applies it on the emulator thread before the next frame through `GLRenderer::SetViewWidth` on the existing renderer (no renderer rebuild). The core latches it at the next polygon swap and the app follows `GetOutputWidth()`, so a frame or two shows the old width in the new rect. A variant with no ratio uses 256. The launch rules don't change: widescreen off or a non-OpenGL renderer at launch means 256 for the session, and Automatic (no wide layout) keeps one ratio from the Mods tab.
- The renderer's letterbox-at-256 and "use as drawn" rules use the active variant's ratio. The layouts list caption shows "Wide 20:9" when all wide variants share a ratio and "Wide" when they differ. The Mods tab says the ratio is "set by the layout".

Later: a pause-menu toggle that switches widescreen and the layout during play (the core already handles a width change).

## Phase 3: Lua engine and API (native)

- Add Lua 5.4 (MIT) as source under `cpp/lua/` and build it into the native lib in `app/CMakeLists.txt`. 5.4 has native integers and bitwise operators, so no `bit32` is needed.
- New `cpp/scripting/LuaEngine.{h,cpp}`, owned by `MelonInstance`, living only on the emulator thread. Kotlin sends `(name, source)` pairs over JNI at game start. Native never opens script files itself.
- Sandbox: open only `base`, `table`, `string`, `math` and `utf8`. No `io`, `os`, `package`, `debug`, `load` of binary chunks, or `dofile`. Add a `lua_sethook` instruction-count limit per callback. A script that runs over the limit or throws an error gets switched off for the session, and the app shows a message (plus a debug log line).
- Hook points in `MelonInstance`:
  - after `loadRom()`: `on_load(game)`
  - in `runFrame()`, just before `nds->RunFrame()`: `on_frame()`. This is the only place the script changes input or memory.
  - `loadState()` / `loadRewindState()`: `on_state_loaded()`
  - `stop()`: `on_unload()`, then close the Lua state
- Input override: the script never edits `inputMask` directly. Each frame the final mask is `(userMask & ~luaRelease) | luaPress`, and touch is the same idea. This means `pressKey`/`releaseKey` from the UI thread stay as they are.
- Memory goes through the NDS bus (`ARM9Read8/16/32`, `ARM7Read*` and the writes). That path is JIT-safe, and it's the one AR cheats use. On the ARM9 side, check ITCM/DTCM first, and block or warn on I/O-range reads (see the research findings).

API v1 (module `melon`):
- `melon.game.code()`, `melon.game.title()`, `melon.frame()`
- `melon.input.held()`, `melon.input.press(mask)`, `melon.input.release(mask)`, `melon.input.touch()` → `x, y, down`, `melon.input.set_touch(x, y)`, `melon.input.clear_touch()`, `melon.input.stick("left"|"right")` → `x, y`
- `melon.KEY.A`, …, `melon.KEY.TOUCH`
- `melon.mem.read8/16/32(addr)`, `melon.mem.write8/16/32(addr, v)`, `melon.mem.read_s8/s16/s32`, plus `melon.mem.arm7.*` with the same names
- `melon.screen.set_layout(id)`, `melon.screen.swap(bool)`, `melon.screen.show_controls(bool)`. These are sent to Kotlin as events, the way DraStic's `luaGetOverrides()` bitfield works.
- `melon.log(...)`, which goes to the opt-in debug log

- `melon.emu.fast_forward(bool)`, which is sent to Kotlin as an event like the screen calls

No DraStic compatibility layer. Old DraStic scripts need porting by hand.

Sticks and rotation: `EmulatorActivity` already reads controller axes. Forward them to native as atomics only while a script is active, like DraStic does (`luaIsActive()`).

**Done (2026-10-29).** What differs from the plan above, or wasn't in it:
- Lua 5.4.9 from lua.org, unchanged except `cpp/lua/patched/lstrlib.c`, which is built instead of the stock file and adds a work budget to pattern matching and plain find. `cpp/lua/LICENSE` says how to redo the patch on an upgrade.
- Each script gets its own Lua state, capped at 32 MB. Besides the libraries left out: `dofile`, `loadfile` and `warn` are removed, `load` only takes text, `print` goes to the log, `setmetatable` refuses `__gc` (finalizers run with hooks off, so the limit couldn't stop one), `collectgarbage` only takes collect/count/step/isrunning, and `string.rep` and `table.move/insert/remove/sort` have size caps.
- Limits per call: 1M instructions (10M for the main chunk plus `on_load`), and a work budget of 20 steps per instruction that pattern matching, the table functions and allocation charge against. Once either runs out, everything left in that call fails, so `pcall` can't keep a script going. The limits are there so a buggy script can't freeze the game; a script written on purpose to hang its own player's game is out of scope.
- Hooks: `on_load` runs at the end of `MelonInstance::start()`. `loadState` runs on an IO thread and rewind on the UI thread, so they only set a flag, and `on_state_loaded()` runs on the emulator thread before the next frame. Reset reloads every running script (`on_unload`, a fresh state, the main chunk, `on_load`); scripts that were switched off stay off.
- `melon.KEY.*`: pressed = bit set, A = bit 0 … Y = bit 11, TOUCH = bit 12. `press`/`release` last one frame; `set_touch` holds until `clear_touch`.
- Memory: ARM9 checks ITCM/DTCM first (ITCM writes also invalidate the JIT). Reads in 0x04xxxxxx raise an error on both CPUs.
- `melon.screen.set_layout` is left out: layouts are user-made and only have UUIDs, so a script has no stable id to ask for (see Later).
- `melon.log` goes to logcat (tag `melonDS-lua`) only while the debug log is on. Script errors are always logged.
- Events to Kotlin carry the script's index. Screen and fast-forward requests are coalesced per call, and Kotlin applies them only when they differ from the UI's state. Native events now go into the pipe in one write with up to 512 bytes of data, and the message queue's start waits for the pipe to open.
- Sticks are zeroed when the last script stops, so a script switched back on doesn't see a stale position.

## Phase 4: scripts UI

- ROM details → Mods → Scripts: an "Add script" file picker copies the `.lua` into app storage at `files/scripts/<gamecode>/`. Each script gets an on/off switch and a delete action. No SAF folder permissions to manage, and scripts survive ROM folder changes.
- Script header convention: a `-- description: ...` line in the leading comment block is shown under the script's name (otherwise the block's first comment line is used, and no leading comment means no description).
- Pause menu: a "Mods" entry that lists running scripts, lets you switch one off for this session, and shows the last error.
- Include settings backup/restore for the scripts folder, or leave it out on purpose and say so.
  - Left out of the app's own settings backup on purpose: it's a flat set of JSON files with a staged, rolled-back restore, and a nested folder of scripts doesn't fit it without reworking that. The ROM config's script list is backed up, so after a restore the scripts show as "File not found" until they're added again. Android's Auto Backup (`allowBackup="true"`, no rules) does include `files/scripts/`.

Phone check: a test script of our own (for example dual-analog aim in Metroid Prime Hunters) works, a broken script shows an error and doesn't crash, hardcore switches scripts off, and savestates/rewind keep working with a script running.

**Done (2026-10-29).** Checked on the phone (turbo A, screen swap, hardcore) and on the emulator with a set of test scripts (an API self-test, a screen swap, and broken scripts that error, loop, try sandbox escapes and run away in a pattern). What differs from the plan above, or wasn't in it:
- Folder: `files/scripts/<gamecode>/` when the header's code is 4 uppercase letters or digits and isn't `0000`. Otherwise (homebrew's `####` and the like) it's `homebrew-<first 16 hex digits of a SHA-256 of the ROM's URI>`, since several homebrew ROMs can share a code. So a homebrew ROM that moves loses its scripts; ROMs with a real code don't. `impl/LuaScriptStorage.kt` holds the path rules for both ROM details and the emulator.
- Import: `.lua` extension checked after picking, 1 MB cap, written to a temp file and renamed. Adding a name that's already there asks first, and the replaced script keeps its place and its switch. A script whose file is missing shows "File not found".
- Hardcore: the switches are disabled with a line saying why. The setting is re-read when ROM details resumes.
- Pause menu → Mods lists every script loaded this session with its description, or its error, or "Switched off". Ticking and unticking both work, for the rest of the session only, and never change the ROM config. Switching back on reloads the script (a fresh state, then `on_load`). Requests are applied on the emulator thread in the order they were made.
- When a script stops (an error, the Mods dialog, or the end of the session), the screen swap, controls and fast-forward it set go back to the player's values (`ui/emulator/LuaUiOverrides.kt`, with unit tests). Per setting: the baseline is the player's value before the first request, the newest request from a script that's still running wins, and a change the player makes by hand clears the requests and becomes the new baseline. Fast-forward's baseline is off at the start of every session.

## Phase 5: drawing API (`melon.draw`)

Decided 2026-10-29: a 2× overlay buffer and auto-clear every frame.

How it fits the pipeline: the emulator thread renders into `Frame.frameTexture` (`renderer/FrameQueue.cpp`, 256·s × 386·s with the bottom screen at row 194·s), runs `runLuaFrame()` just before `nds->RunFrame()`, then fences and queues the frame. `FrameRenderThread` presents it through `MelonDSAndroidJNI.cpp` → `FrameRenderCallback.renderFrame` → `DSRenderer.drawFrame`, which draws both screen quads with the filter shader.

Where it lives:
- Each `Frame` gets its own overlay texture, uploaded on the emulator thread before the frame's `renderFence`. It rides the same fences and queue slot as the game image, so it can't lag or tear against it, and it holds through fast-forward, pause and repeated frames.
- The overlay texture has the frame texture's layout at 2×: 512 × 772, top screen in rows 0..383, bottom screen from row 388. So it reuses the screen quads and UVs as they are and follows layouts, swap and screen alpha for free.
- `DSRenderer` draws it in its own pass with a passthrough shader (NEAREST, premultiplied alpha, multiplied by the screen's alpha), so filters and the DS colour shader never touch it. Draw order is interleaved: under-screen, its overlay, over-screen, its overlay.
- Created lazily the first time a frame has drawings. Per-frame flags say which screens have content; frames are recycled, so the flags are reset every frame. No drawings means no upload and no extra draw. Upload only the row range that has content now or had it the last time this texture was used.
- Screenshots and rewind thumbnails leave the overlay out.

API (module `melon.draw`):
- `pixel(screen, x, y, color)`, `line(screen, x1, y1, x2, y2, color)`, `rect(screen, x, y, w, h, color[, filled])`, `text(screen, x, y, str, color[, bg])`. `text` returns its width in DS pixels, and `\n` starts a new line.
- `screen` is `"top"` or `"bottom"`, meaning the DS screen, so swap moves the drawings with it.
- Coordinates are DS pixels (256×192). Fractions are allowed and land on the nearest half pixel, which is what the 2× buffer buys for shapes. Shapes are one DS pixel thick. Anything off-screen is clipped.
- Text uses melonDS's own OSD font (`melonDS-android-lib/src/frontend/qt_sdl/font.h`, GPL like us, already in the tree), drawn at the 2× resolution, so a line is 6 DS pixels tall. Characters outside ASCII draw as `?`, one per UTF-8 code point.
- Colours are `0xAARRGGBB` integers masked to 32 bits (so -1 is opaque white). `0xFF0000` has alpha 0 and draws nothing; the docs say so, and there's no "missing alpha means opaque" rule.
- Auto-clear: each frame shows only what was drawn in that frame's `on_frame`. Draw calls anywhere else (main chunk, `on_load`, `on_state_loaded`) do nothing, so a HUD drawn from both can't show twice on a state-load frame. No `clear()`.

Budget and safety:
- Draw calls only record commands into a per-script list (text goes into a per-script string store). After all callbacks have run, the engine rasterises the lists of scripts still running into the shared buffer, in script order (later scripts on top). A script that errors or is switched off mid-frame shows nothing that frame.
- Each recorded command is charged against the work budget: a base cost plus its pixel count after clipping (at 2×). Cap of 4096 commands per script per frame; going over raises the usual limit error. Clip in 64-bit before computing areas.
- Stop, error, the Mods dialog, reset and the last script stopping all just mean the next frame has no drawings, so `LuaUiOverrides.kt` doesn't change. While paused, the frame on screen keeps its overlay.
- Savestates and rewind don't store the overlay; the next callbacks redraw it. Hardcore runs no scripts, so nothing is drawn.
- The buffer and the upload stay on the emulator thread: no new locks.

Steps:
- Native: `scripting/LuaEngine.{h,cpp}` (recording, the `melon.draw` table, charging, `rasterizeOverlay`), a new `scripting/LuaDraw.{h,cpp}` (clipped pixel, line, rect, text, premultiplied source-over), `renderer/FrameQueue.{h,cpp}` (overlay texture and flags per frame, lazy creation, deleted in `clear()`), `MelonInstance.cpp` `runFrame()` (rasterise only while scripts are active, upload before the fence), and the JNI callback in `MelonDSAndroidJNI.cpp` (passes the overlay texture and screen flags).
- App: `FrameRenderCallback.kt`, `PresentFrameWrapper.kt` and `FrameRenderCoordinator.kt` carry the new fields; an overlay shader in `common/opengl/ShaderProgramSource.kt`; `DSRenderer.kt` splits its draw call per screen and interleaves the overlay. `ExternalPresentation`'s renderer gets it too.
- Test scripts: one that draws every primitive on both screens (edges, negative and huge values, fractions, text with `\n` and non-ASCII), and a runaway fill loop that must hit the work limit.

Phone check: a HUD script (text with a background, plus a rect) on both screens follows swap and an overlapping custom layout, doesn't lag the game during fast camera moves, isn't changed by the CRT/xBR filters, and vanishes when switched off in Mods. Savestate, rewind and reset redraw cleanly, the external display shows it, and frame time with no script is unchanged.

**Done (2026-10-29).** Checked on the phone (OpenGL 4x with the LCD filter: drawings on both screens, filter left off them, self-test passes, no visible frame-time cost) and on the emulator (Mods switch, savestate, reset, the limit scripts). What differs from the plan above, or wasn't in it:
- Text lines are 7 DS pixels apart. `text`'s background is one box around all lines.
- A rect with zero or negative width or height draws nothing. Outlines sit inside the rect, and lines include both end pixels. Draws that land fully off-screen aren't recorded and don't count towards the 4096.
- Each command costs 32 work steps plus 1 per pixel (at 2×) and 1 per byte of text. Text is capped at 64 KB per script per frame, since it's stored outside the Lua memory cap.
- A frame's overlay texture is zero-filled once when it's created. The overlay shader is built with the other shaders.
- The app's FPS counter sits over the top-left corner of the top screen, so scripts should keep their HUD out of there.

Left for later: `image` (from a string of ARGB bytes, or PNG once `melon.storage` exists), a text size option, circles, a hook after `RunFrame`, the overlay in screenshots as an option, and overlay UVs for WideMelon's wider top screen.

## Phase 6: Mods tab and linked script folders

Decided 2026-10-29 with the user: one folder per game, picked in that game's Mods tab, that stays linked (not copied). Scripts found in it start switched off. The folder replaces the one-file picker.

- ROM details gets a third tab, "Mods", between Configuration and RetroAchievements (`RomDetailsTab`, `RomHeaderUi`, `RomDetailsScreen`). The Mods group moves out of `RomConfigUi` into its own screen, and widescreen will go there too once Phase 2 lands.
- `RomModsConfig` gets the folder's tree URI (DTO, parcelable and UI model too). Picking uses `OpenDocumentTree` and takes a persisted read permission. Changing or removing the folder clears the script list and releases the old permission, unless another ROM's config still uses that folder.
- The script list is still `luaScripts` (file name + switch), now kept in step with the folder: top-level `.lua` files only (subfolders wait for `require`), sorted by name. A new file is added switched off. When the folder can be read and a file is gone, its entry is dropped. When the folder can't be read (permission lost, card removed, restored from a backup), the list is kept as it is and the tab says the folder can't be found and offers to pick it again.
- The sync runs when the Mods tab is shown or ROM details resumes, and at game start. At game start only switched-on scripts that exist are read and sent to native, with the same 1 MB cap. A script that can't be read shows its error in the pause menu's Mods dialog, like a script that fails to load.
- No delete action: the app never changes the user's folder. To get rid of a script, the user removes it from the folder.
- `LuaScriptStorage` drops the copy/import/delete code and the `files/scripts/<gamecode>/` folder rules. Old copied scripts aren't migrated: lists that point at no folder are cleared, and `files/scripts/` is deleted once (it only holds copies the app made).
- The hardcore note and disabled switches move to the Mods tab unchanged. The pause-menu Mods dialog doesn't change.

Phone check: pick a folder with several scripts; they show up off with their descriptions; switch one on and it runs; add a file to the folder from a PC and it appears after leaving and reopening ROM details; delete one and it disappears; change the folder; hardcore still blocks; and a moved folder shows the "can't find the folder" state without crashing the game.

**Done (2026-10-29).** Checked on the phone (Mods tab, hardcore) and on the emulator (picking, adding and deleting files from outside, a moved folder, changing folders, the cover screen). What differs from the plan above, or wasn't in it:
- The folder's permission is also kept when the same tree is one of the app's own folders (ROM folders, BIOS, saves, perf log), since they share one grant. Picking the same folder again keeps the switches.
- The folder is listed with one `DocumentsContract` children query. It counts as unreadable when it isn't a directory, when the provider throws, or when the cursor says it's still loading or has an error, so a slow cloud folder can't wipe the list.
- While the folder can't be found, the last known descriptions stay on screen for that visit.
- A switched-on script that can't be read at game start is listed after the loaded ones in the pause menu's Mods dialog, with the reason, and can't be switched on there.
- `files/scripts/` is deleted at app start rather than in a migration, since the fork's versionCode doesn't change.
- Side fixes: the ROM list is emitted as a copy (a scan could change it mid-iteration), and the GBA ROM/save permissions are only taken when those paths are picked, not on every config save.

## Phases 7–10: the rest of the Lua list

Designed 2026-10-29. The user chose: options can also be changed from the pause menu, script savestate slots live in memory only, and script drawings can be put in savestate/rewind pictures with a global setting that's off by default. Defaults picked without asking: storage is kept per game by game code (or `homebrew-<sha256 of the ROM URI>`), is capped at 256 KB per script, and isn't deleted when its script leaves the folder; PNGs and modules come from the whole folder tree.

Facts from the code these rest on: draw commands are recorded in `on_frame` and rasterised in `LuaEngine::runFrame`; the overlay upload happens after `RunFrame` and before the fence. There's no player screenshot feature: savestate thumbnails (`takeScreenshot`) and rewind thumbnails both go through `ScreenshotRenderer::renderScreenshot`. Saving state between frames on the emulator thread is safe (rewind does it). `stopEmulation` destroys the instance, so anything Kotlin pulls after stop must live outside `MelonInstance`. The event pipe carries at most 512 bytes. Layout names are optional and not unique.

### Phase 7: drawing extras

- `melon.draw.text(screen, x, y, str, color[, bg[, size]])`: `size` is an integer 1–4 (default 1). Each glyph pixel becomes size×size overlay pixels; the returned width and the work charge scale with it.
- `melon.draw.circle(screen, cx, cy, r, color[, filled])` on the half-pixel grid. The outline is the pixels whose centre is between r−1 and r DS pixels from the middle. r ≤ 0 draws nothing. Charged like `rect` (clipped pixel count, worked out over visible rows when recorded).
- `on_frame_end()`: a new callback after `RunFrame` (and the software upload), before the overlay upload. `LuaEngine::endFrame()` runs it and then rasterises, so drawings from both callbacks land on the same frame: each script's `on_frame` commands, then its `on_frame_end` commands. It gets its own instruction budget; the 4096-command and 64 KB text caps are per frame across both. `melon.frame()` is the frame that just ran. Memory writes are allowed; `press`/`release` there apply to the next frame. `endFrame()` runs whenever `runLuaFrame` ran, so a script that stopped mid-frame is still cleared.
- Overlay in savestate and rewind pictures: a global setting in video settings, "Show script drawings in save state pictures", off by default, carried in `EmulatorConfiguration` (which already updates live). GL path: after the frame quad in `ScreenshotRenderer`, draw the overlay texture with a premultiplied blend and a 2×2 average shader (`.bgra` swizzle); don't change the texture's filter, since the render thread may be presenting it. Software path: CPU-blend the overlay with a 2×2 average.

**Done (2026-10-29).** Checked on the emulator (sizes, circles, `on_frame_end`, an error in `on_frame_end`, a runaway circle loop, save state pictures with the setting off and on); not yet on the phone. What differs from the plan above:
- No `.bgra` swizzle in the save-state-picture overlay shader: the frame and overlay textures already share byte order there, and only the display shader swizzles. The plan's swizzle would have swapped red and blue.
- A circle's centre is a pixel corner, like `rect`'s x/y (10.5 centres on pixel 10). Radii are capped at 2^28 DS pixels so the maths fits in 64 bits on 32-bit ARM.
- Circles are charged 32 + 2 per visible row before their pixels are counted (counting and drawing both walk the rows), plus the pixel charge.
- The setting sits under Settings → Display and is copied into an atomic for the emulator thread.

### Phase 8: folder content (`require`, images)

- Setup over JNI changes once to an array of `LuaScriptSetup` objects (name, source; later phases add storage and options), plus the session's modules and images.
- Kotlin walks the linked folder's subfolders at launch when any script is on: at most 4 levels and 64 folders, one children query per folder. Top-level `.lua` files stay the only scripts and can't be required.
- `require`: `lib/util.lua` is module `lib.util`, `lib/init.lua` is `lib`. Path parts outside `[A-Za-z0-9_-]` are skipped. Caps: 1 MB per file, 2 MB and 256 files in total, taken in path order; names left out are sent so the error can say why. Native keeps one immutable map for the session, shared by every state. `require` is a C function with a per-state cache in the registry (a fresh state loads modules fresh), loads text only with chunk name `=lib/util.lua`, caches the result (or `true`), catches loops, and charges 1 work step per source byte. Still no `package` table. Errors: `module 'x' not found (looked for x.lua and x/init.lua in the script folder's subfolders)`, `module 'x' wasn't loaded: modules are limited to 2 MB in total`, `loop in require: a -> b -> a`.
- Images: `melon.image.load("hud.png")` → image or nil plus an error; `melon.image.new(w, h, argb_string)` (4 bytes per pixel, 0xAARRGGBB little-endian); `img:size()` → w, h; `melon.draw.image(screen, x, y, img[, opts])` with opts `{ scale = 1, src = {x, y, w, h}, alpha = 1 }`. `scale` is a multiple of 0.5 from 0.5 to 8 (1 = one image pixel per DS pixel, 0.5 = one per overlay pixel for 2× art), nearest neighbour. Names are paths relative to the folder (`img/hud.png`).
- Kotlin finds `.png` files in the same walk, checks bounds first (each ≤ 1024×1024; at most 128 files and 4M pixels in total), decodes unpremultiplied and sends ARGB ints. Native premultiplies, keeps them immutable and shared for the session outside the Lua memory cap. `new` images live in Lua's allocator, so they count. A drawn image is anchored in a per-frame registry table so it can't be collected before rasterising. Charged 32 + clipped destination pixels at 2×.
- Edited modules and images take effect at the next game start, like scripts.

**Done (2026-10-29).** Checked on the emulator (the `require` and image self-tests pass, both together, savestate and reset); not yet on the phone. What differs from the plan above, or wasn't in it:
- The walk also stops after 4096 files found. Caps are taken in path order, and once one module or image doesn't fit the total, later ones are left out too. The 256/128 file limits count files considered. The 64-folder cap doesn't count the root.
- The path-name rule applies to images too. `x.lua` wins over `x/init.lua`, like Lua's own search. A module is called with its name and path, like stock Lua. A file over 1 MB gives `module 'x' wasn't loaded: modules are limited to 1 MB each`.
- Module errors keep their traceback (the engine's message handler runs inside `require`).
- `src` parts outside the image draw transparent. The image metatable is hidden from `getmetatable`. Image draws cost 2 steps per destination pixel, since they're heavier than fills.

### Phase 9: per-script data (storage, options)

- `melon.storage`: `get(key)`, `set(key, value)` (nil deletes), `keys()`, `clear()`, `flush()`, `size()` → bytes used, limit. Keys are strings up to 256 bytes. Values are strings, numbers (integers and floats kept apart), booleans or tables (at most 16 deep, string or integer keys, no cycles); `set` deep-copies and `get` returns a fresh copy. Cap 256 KB per script, encoded; over it is a normal catchable error. Encoding and decoding charge 1 work step per byte.
- Native: `scripting/LuaStorage.{h,cpp}`, a versioned binary codec and a key → encoded map that lives in `LuaEngine::Script`, not in the Lua state, so it survives reset reloads and switching off and on. A corrupt blob starts empty (logged) and isn't overwritten until the script writes.
- Saving: never every frame. When changed, native encodes a snapshot on `flush()` (applied at the end of the frame), every 600 frames, when the emulator thread parks for a pause, when a script stops, and at `stop()` after `on_unload`. Snapshots go into a global outbox (mutex, outside `MelonInstance`); a small event wakes Kotlin to drain it on IO, and `stopEmulator()` drains it after the emulator stops. Kotlin (`impl/LuaScriptDataStore.kt`) writes `files/lua-data/<gameKey>/<script>.bin` via a temp file and rename, and sends the blob with the source at launch. Not part of savestates or rewind. The Mods tab shows "Saved data: N KB" with a Clear action.
- Options, declared in the leading comment block and parsed in Kotlin (new `impl/LuaScriptHeader.kt`, which also takes over the description parsing):
  ```
  -- option: speed type=number min=1 max=10 step=1 default=5 label="Aim speed"
  -- option: hud type=toggle default=true label="Show HUD"
  -- option: mode type=choice values=easy,normal,hard default=normal label="Difficulty"
  ```
  At most 16 options; key `[a-z_][a-z0-9_]{0,31}`; label up to 60 characters; choice up to 8 values of up to 24 characters; number finite, min < max, at most 1000 steps. Header read limit 8 KB. Bad lines are skipped and the Mods tab says how many couldn't be read.
- Stored in RomConfig as `RomLuaScript.options: Map<String, String>` (DTO, parcelable, kept by `LuaScriptFolderSync`). At launch, declarations are parsed from the source being sent; a missing or invalid stored value falls back to the default. In Lua, `melon.options` is a plain table built at each load with typed values (boolean, integer when integral, number, string); undeclared keys are nil.
- UI: an expandable options section under each script in the Mods tab (switch, slider, dropdown). The pause menu's Mods dialog gets "Options…" per running script: it saves to the ROM config and restarts that script with the new values (the off/on path plus an options payload).

**Done (2026-10-29).** Checked on the emulator (option controls in the Mods tab and the pause menu, the storage launch count across relaunches, a force-stop while paused, Clear, reset); not yet on the phone. What differs from the plan above, or wasn't in it:
- Storage keys must be real strings. An empty store deletes its file, and a data file over 256 KB is ignored. If the ROM header can't be read, storage is session-only (nothing loaded or saved) rather than falling back to a guessed key.
- The final drain runs before `stopEmulator()` returns, and a snapshot that can't be handed over JNI goes back into the outbox. Clear is ignored for the game that's running.
- Option `step` defaults to 1, and `default` to min, false or the first choice. An off-step number default snaps to the nearest step, and an unknown attribute makes the line unreadable.
- The pause-menu Mods dialog is built from rows so each running script has its own "Options…" button.
- ROM details re-reads the ROM's config when it resumes, so values changed in the pause menu aren't overwritten by its old copy.
- The native codec has a host test (`app/src/test/cpp/LuaStorageTest.cpp`, built by hand with MinGW; the command is at the top of the file). It isn't wired into Gradle.

### Phase 10: emulator control (`set_layout`, savestates)

- Layouts become an option type: `-- option: map_layout type=layout label="Layout while the map is open"`. The Mods tab shows "Don't change" plus the player's layouts and stores the UUID. `melon.screen.set_layout("map_layout")` names that option; `set_layout(nil)` goes back to the player's layout. Native sends `(script, key)`; Kotlin resolves the UUID. `LuaUiOverrides.kt` gets a layout request (newest from a running script wins, dropped when it stops; no baseline needed, the ROM's layout is the fallback). A deleted layout or "Don't change" does nothing.
- `melon.state.save(slot)`, `melon.state.load(slot)`, `melon.state.has(slot)`, slots 1–4 per script, kept in memory in `Script` (survive a reset, freed when the script is switched off, gone at the end of the session, never touch the player's slots). `save` runs on the emulator thread between frames (like rewind), at most one per script per frame, and returns true or false plus an error. `load` queues the load to run after every script's `on_frame`: it backs up first like a player's load, drops that frame's input override, and makes `on_state_loaded` run before the next `on_frame`. A small callback interface in `MelonInstance` keeps `Savestate` out of `LuaEngine`. Defensive check against hardcore even though scripts are off there.

**Phase 10 done (2026-10-29).** Checked on the emulator (layout switch while SELECT is held and back, switching the script off mid-hold, "Don't change", script save/load/refused save, a load after reset, the player's own slots untouched); not yet on the phone. What differs from the plan above, or wasn't in it:
- Layout options have no default, and scripts see them as a boolean (picked or not). Unnamed layouts show as "Unnamed layout", numbered if there are several. A key that isn't a layout option is a Lua error.
- Only a real player pick of a layout (the global layout setting changing while in use) clears scripts' layout requests. The ROM's own layout being deleted doesn't.
- Rebuilding the on-screen controls on a layout change used to cancel held touches, which released a held button (and made a held-button layout switch snap straight back). Held touches are now carried over to the new controls, without haptics, and the DS touch screen handles a cancelled touch. This applies to every layout rebuild (fold, rotation, swap).
- Script saves: one every 30 frames per script, saves and loads cost 1M work steps, and all script slots together are capped at 64 MB. A restart with new options keeps the slots. A queued script load is dropped if the player loads or resets first, and a restart clears it. A script load resets the achievements display like a player load.

Phone check per phase: the matching test scripts work on the phone, and the earlier ones still do.

## Later (not planned in detail)

- WideMelon on the compute renderer.
- Script savestate slots on disk, if anyone asks (Phase 10 keeps them in memory).
