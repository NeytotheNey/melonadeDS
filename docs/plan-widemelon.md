# Improving WideMelon

Notes and plan for WideMelon improvements, mainly more 2D in the wings, from what we learned from PokeDaisy's widescreen ("EXPAND") on 11-07. How WideMelon works today is in [MODS.md](../MODS.md); wide 2D on the GPU renderer is step 4 of [plan-gpu-2d.md](plan-gpu-2d.md).

## PokeDaisy

The full gap list against EXPAND, including the camera and game-state features these notes missed, is now [plan-expand-parity.md](plan-expand-parity.md) (11-17).

[lidor30/pokedaisy](https://github.com/lidor30/pokedaisy) (GPL-3.0) is an Android GB/GBC/GBA/DS player for Pokémon games with a live companion. DS uses upstream melonDS (a pinned submodule) plus one patch of about 570 lines, mostly hooks; the logic is in the app (`app/src/main/cpp/nds/pk_nds_wide.cpp`, `pk_nds_gl.cpp`). GBA uses mGBA. No Lua or other scripting: per-game support is C and Kotlin tables keyed by game code or ROM CRC, some generated from TOML at build time (`gamedata/`, `scripts/gen_wide_tables.py`). Read from their code, not run or measured.

**DS 3D margins.**
- A hook gets every polygon that passed culling, before clipping, and copies it into their own lists. The game's polygon RAM and list counts never change, so the game can't tell.
- Software: 8 slices around the screen, each shifted one screen in clip space, drawn by a second SoftRenderer on 3 helper threads. Their own comment puts it at about two thirds of a frame of CPU.
- GPU (2x and up): one remapped, re-clipped list drawn into a bigger viewport instead of the game's.
- A BoxTest that misses is retried once against the wider view plus 4 px.
- Widens vertically too (up to 248 px a side, 96 above and below), with zoom-out and look-around in the app (`WideCamera.kt`).
- Limits: 8 DS carts only (`WideScreen.kt` `ndsKnows`), only on the field or in battle (from RAM reads), full-screen viewport only, a 4096-polygon cap with a fallback, top screen only, no dual-screen games.

**DS 2D in the margins.**
- A BG blended over the 3D (fog and the like) carries on from VRAM, but only if it covers the screen's whole outline.
- Rows a layer covers edge to edge repeat the edge pixels.
- Transparent 3D shows the 2D backdrop color, not the 3D clear color.
- Battles (tuned on HG/SS): BG3 drawn 512 wide, sprites crossing the edge drawn from OAM, HP boxes and the text box carried to the edges with "held still for 8 frames" checks.
- Seams from edge marking fixed. Everything else black.

**GBA and GB 2D (`pk_wide.c`, `pk_wide_gb.c`).** This is their real 2D widening. The margins are rebuilt every frame from the game's own map, tiles and sprite tables, not from VRAM: per game they know where the map buffer, camera offsets, pan, `gSprites`, sprite order and object events live (decomp names, `scripts/find_wide_addrs.py` finds them in hacks from headless RAM dumps). Battles are wide too; hacks whose battle scenes are only screen-wide get the scene mirrored out from its edges. `scripts/verify_wide.py` checks that the middle matches the real frame exactly ("map 440/440"). Menus and other screens get plain bars.

**Where each is better.** Ours is the better general feature: any game, any viewport, OpenGL and Compute, one pass, dual-screen wing hold. Theirs looks better in the games it was tuned for: the game can't see it and the margins get 2D.

## Ideas for us

1. **Shadow polygon list from a submit hook.** Squeeze copies, not the game's polygon RAM. Mostly covered now by `widepolys` ([plan-widemelon-polylist.md](plan-widemelon-polylist.md), on by default): the polygon limit, count registers and DISP3DCNT bit 13 are the native view's. What's left that a shadow list would fix is BoxTest being squeezed, so the RetroAchievements caveat shrinks to that but doesn't go.
2. **Safe generic 2D rules** for plan-gpu-2d step 4, with no per-game data:
   - carry a BG into the wings only if it covers the screen's whole outline;
   - otherwise repeat edge pixels on rows a layer covers edge to edge.
   These avoid the tilemap-wrap junk step 4 worries about.
3. **Per-game wide 2D from game knowledge**, the way their GBA mode does it: the game's map, camera and sprite tables say what belongs in the wings, so there's no guessing about wrapped tilemaps or parked sprites. For us this fits Lua: a script already knows the game's RAM, so an API that tells WideMelon what to draw in the wings (a BG's extra tiles, which sprites are real) keeps the core generic. Decompiling the game, case by case, is how we'd find the addresses.

## Decisions (user, 11-07)

- **Wide 2D waits for the GPU 2D renderer.** No separate CPU wing pass. The wings get 2D from plan-gpu-2d step 3's shader renderer, so that renderer has to draw X outside 0..255 from the start.
- **First game for per-game 2D: HeartGold/SoulSilver battles.** It's what PokeDaisy tuned its battle 2D on, so we can compare directly. We don't have its RAM mapped yet. The pret `pokeheartgold` decomp is the place to find the addresses.
- **2D-only lines get edge repeat when it's safe**, not black. Safe means the same rule as with 3D: a layer covers the row edge to edge.
- **Shadow polygon list is deferred.** See "Open".

## Plan

Steps 1 to 3 are done and merged into `material3` / `melonade` (11-11). Step 4 is plan-gpu-2d step 3 (in progress in [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md)), and steps 5 to 7 build on it. Sizes are rough line counts.

**1. Backdrop in transparent wings.** Done 11-08 (core `widemelon`, edge check reworked in `c19ab873`). The CPU writes one wing pixel's planes per line and side, and the compositor's `WingPixel` composites them; how it works is in MODS.md ("Wing planes"). Change from the plan: a wing only gets "3D over the backdrop" where its edge visibly shows the backdrop, because White 2's intro has a magenta backdrop under a grey BG, and Contra 4's "Licensed by Nintendo" screen is a screen-wide black 3D quad over a white backdrop, which the squeeze leaves out of the wings (the first version had white wings next to a black middle there). Test scene: Contra 4's Konami logo right after boot (white backdrop, transparent black 3D clear). Checked on the OnePlus and the Fold, OpenGL and Compute, filter 0 and 1: Konami logo, White 2's title, Phantom Hourglass's wing hold and Mario Kart DS, no crashes or GL errors, steady screens' middles pixel-identical. Logs in `perf_logs/2026-11-08-widemelon/`.

**2. Headless wide check.** Done: `--wide-check` in the core's `tools/swi-harness` (`WideCheck.cpp`, `wide_check.sh`). It runs the same frames with wide on and off on software 3D, fails if the middle 256 changes (planes 0 to 2, the control words with the wing bits masked, the capture banks), and writes the wing output as PNGs. Once step 4 exists, the same runner should also check that the GPU renderer's middle 256 matches CPU 2D.

**3. Small 3D fixes.** Done 11-08 (core `widemelon`): BoxTest tests the native box first and retries only a miss, squeezed for a slightly wider view; the `debug.melonade.widedrops` counter (also in the wide check's summary); and geometry timing from the native view's clip result, checked with the wide check's `--timing native`.

**4. GPU 2D shader renderer (plan-gpu-2d step 3).** Planned there. What wide 2D needs from it:
- every BG and sprite drawn over an X range wider than 0..255;
- a per-layer "draw past the edge" switch, off by default;
- the wing columns composited with the 3D at the wing's X.

The middle stays the display-only shadow of CPU 2D, and capture stays CPU's middle 256.

**5. Generic 2D rules in the shader.**
- **5a, edge repeat.** On a line where one layer covers every column, the wings repeat column 0 (left) or 255 (right) of that layer, composited with the 3D at the wing's X. Per line, so no lag. This applies to 2D-only lines too, which is how they stop being black.
- **5b, full-outline carry.** A text BG alpha-blended over the 3D (fog and the like) that was opaque along the whole screen outline last frame gets drawn from its tilemap into the wings on the next frame, with each line's own scroll. Limiting it to "blended over 3D" stops a full-screen picture's left edge wrapping into the right wing. The verdict lags one frame. Affine BGs are left out.
- **Risks:** 5a can stretch a menu's border or a picture's edge into a band. If that looks bad in real games, add a per-game off switch. Also, the MMPX filter has to cover the wings, or a seam shows with "Smooth 2D edges" on.
- **Fold:** Platinum's fog, a menu screen, a 2D-only game.

**6. Lua API for per-game wide 2D.** The script tells the core what is safe, and the core draws it:
- `melon.wide.width()`;
- `melon.wide.bg(n, {left=, right=, first=, last=})`: BG n has this many valid pixels past each edge, on these lines;
- `melon.wide.obj(i, x)`: OAM entry i is real, and x is its true signed X, which settles the 9-bit X question;
- `melon.wide.clear()`.

The config is latched at line 0 and capped (4 BGs, 128 sprites). It's cleared when the script stops or the game resets. A wrong script can only spoil the wings, and step 2's check proves the middle is unaffected. Separately, `melon.draw` can be allowed to reach the wings by widening the overlay (`LuaDraw.h` `kScreenWidth`, `FrameQueue`, `DSRenderer`). `LuaEngine` runs on the emulator thread with the `NDS*`, so `melon.wide` can hand the config to the core. Lua is off in RetroAchievements hardcore, so per-game wide 2D will be too. Size: about 250 core lines and 200 app lines, plus about 120 for the overlay. API errors can be tested on the AVD; the visuals need the Fold.

**7. HG/SS battles.** The first per-game script, using step 6: the battle background past the edges, Pokémon and trainers sliding in from off screen, and the HP boxes and text box carried to the edges. Map the battle RAM from the `pokeheartgold` decomp first, as [pokemon-battle-ram.md](pokemon-battle-ram.md) did for White 2, then compare on the Fold against PokeDaisy's version. White 2 battles can follow with the RAM we already have.

## Camera: pan and zoom (11-15)

What PokeDaisy's EXPAND does on DS: the game's camera never moves. EXPAND renders margins around the real frame, then shows a window inside that wider picture each frame. Zooming out makes the window bigger, and the right stick slides it around, limited to the margins. The window drifts back to the middle when the player walks, and it snaps back to 1x on text, menus and the start of a battle, which it knows from Pokémon field RAM. The user wanted zoom-out and look-around in WideMelon, with per-game behavior through Lua and no PC-from-anywhere style function calls.

**Stage 1 (display-side camera) and stage 2 (vertical zoom-out) are built** (11-15); how they work is in MODS.md, "Camera". The core renders at a render width Wr (the widest zoom) and, with stage 2, M margin rows above and below; the app shows a window of that picture in the layout's wide screen.

- **Stage 2 results (11-15).** Desktop: M = 0 is bit-exact with the build before it on OpenGL and Compute. Phones, 16:9 layout (342 shown, 512 rendered, M = 48), 4x, OpenGL and Compute: zoomed fully out fills the rect with no seams, pan clamps right in both axes, walking eases back, a top-screen 2D menu snaps to 1x, the overlay stays on the middle rows, and Mario Kart DS's margins don't flicker. Not covered: a hardware stick, and a live race on the Fold (the OnePlus did one). Logs in `perf_logs/2026-11-15-wide-camera-stage2/`.
- **Fill cost.** OpenGL showed no cost on the Fold or OnePlus. The OnePlus's Compute did: in White 2's field at 4x, 342 rendered gave 45 fps and 20.7 ms of work, while 512 rendered at zoom 1 gave 30 fps and 31.9 ms (`perf_logs/2026-11-15-wide-camera/`). With the margin, zoomed out, `widey` 1 vs 0: the Fold held 60 fps on both renderers (+0.2 ms on Compute, plugged in); the OnePlus (unplugged) held 60 on OpenGL, but Compute went 29.9 → 18.5 fps (+21 ms). The user's call (11-15) is to come back to this after the remaining Compute efficiency work, which may change these numbers.
- Other costs to keep in mind: wider views fill polygon RAM sooner (`widepolys` covers it), and pop-in at the far edges from the game's own culling gets more visible.

**Stage 1b, if the fill cost shows (optional).** Render only the window: an asymmetric `ProjectX` that maps a native range `[x0, x0+Wr)` instead of a centered one, using PokeDaisy's off-axis form `(256x − (R−L)w) / (256+L+R)`. The stretch center at `GPU3D.cpp` HiresPosition, the BoxTest retry, `WideXAdjust` and the compositor's middle offset all move with it. This also lets the pan go past 768 wide.

**On-screen camera button (user, 11-15, not started).** A layout button for the camera, so touch-only players can zoom without the R3 hotkey or a pinch.
- It's a toggle like the microphone button: a new `LayoutComponent` with on and off faces (`SingleButtonIcons.kt`), placed by the layout editor, and only shown when the game's Camera switch is on.
- A tap does what the Camera Zoom hotkey does (user, 11-15): it steps through the zooms and wraps back to 1x. The face shows off at 1x and on while zoomed out.
- **It needs a new icon**, in both of our icon sets: the vector button drawables (`res/drawable/ic_button_*.xml`) and the pixel set for the controller styles (`PixelIcons.kt`). It needs an on face and an off face, and the off face should match how `ic_button_microphone_off` is drawn.
- Size: about 60–100 app lines plus the icons. Checks: the AVD for the layout editor and the toggle, the Fold for the zoom.

**Stage 3, per-game real camera (idea).** A Lua script writes the game's own camera position or distance in RAM. The game then culls and draws for the new view, so there's no edge pop-in and no margin limit. The camera address has to be found per game (decompiling helps), and the game sees the change.

## Open

1. **Shadow polygon list: the user's call.** The `widedrops` counter found real drops (Mario Kart DS's Shroom Ridge start: 2,212 dropped, 94% would fit natively; NSMB's title intro at 426: 137–244 a frame, its 3D flowers vanish). `widepolys` fixed those without a second geometry pass, so the shadow list would now only hide BoxTest from the game. If it's ever built, reuse HalfCamera's FIFO replay into a shadow `GPU3D` rather than copying `SubmitPolygon`. It doubles geometry work on the emulator thread (the OnePlus's limit).
2. Optional: in compmode 4 the wing could take the edge's 3D alpha instead of 31, so wings follow fades of a translucent full-screen quad (Contra 4's fade into "Licensed by Nintendo" stays black in the wings).
3. Steps 4–7 wait for the GPU 2D renderer ([plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md)).
4. The camera's stage 1b, the on-screen button and stage 3 above.
