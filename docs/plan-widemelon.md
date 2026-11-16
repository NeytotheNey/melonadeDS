# Improving WideMelon

Notes for a WideMelon improvement pass, mainly more 2D in the wings. Not a plan yet: what we learned from PokeDaisy's widescreen ("EXPAND") on 11-07, and the ideas it gives us. How WideMelon works today is in [MODS.md](../MODS.md); wide 2D on the GPU renderer is step 4 of [plan-gpu-2d.md](plan-gpu-2d.md).

## PokeDaisy

[lidor30/pokedaisy](https://github.com/lidor30/pokedaisy) (GPL-3.0) is an Android GB/GBC/GBA/DS player for Pokémon games with a live companion. DS uses upstream melonDS (a pinned submodule) plus one patch of about 570 lines, mostly hooks; the logic is in the app (`app/src/main/cpp/nds/pk_nds_wide.cpp`, `pk_nds_gl.cpp`). GBA uses mGBA. No Lua or other scripting: per-game support is C and Kotlin tables keyed by game code or ROM CRC, some generated from TOML at build time (`gamedata/`, `scripts/gen_wide_tables.py`).

Read from their code on 11-07 (not run, not measured).

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

## Where each is better

Ours is the better general feature: any game, any viewport, OpenGL and Compute, one pass, dual-screen wing hold. Theirs looks better in the games it was tuned for: the game can't see it, it goes vertical, and the margins get 2D.

## Ideas for us

1. **Shadow polygon list from a submit hook.** Squeeze copies, not the game's polygon RAM, so the polygon limit and list counts stop showing (and the RetroAchievements caveat goes). The cost is a copy of SubmitPolygon's work to keep in step with upstream. 11-14: partly covered by the native-counted polygon RAM with a bigger wide list ([plan-widemelon-polylist.md](plan-widemelon-polylist.md), `GPU::WidePolys`, debug switch `widepolys`, off by default until the phones have checked it): with it on, the polygon limit, the count registers and DISP3DCNT bit 13 are the native view's, so they stop showing to the game. BoxTest is still squeezed, so the RetroAchievements caveat shrinks to that (and to polygon timing, which already follows the native view) but doesn't go.
2. **Backdrop in transparent wings** instead of the clear color. Already in MODS.md's smaller ideas.
3. **Safe generic 2D rules** for plan-gpu-2d step 4, with no per-game data:
   - carry a BG into the wings only if it covers the screen's whole outline;
   - otherwise repeat edge pixels on rows a layer covers edge to edge.
   These avoid the tilemap-wrap junk step 4 worries about.
4. **Per-game wide 2D from game knowledge**, the way their GBA mode does it: the game's map, camera and sprite tables say what belongs in the wings, so there's no guessing about wrapped tilemaps or parked sprites. For us this fits Lua: a script already knows the game's RAM, so an API that tells WideMelon what to draw in the wings (a BG's extra tiles, which sprites are real) keeps the core generic. Decompiling the game, case by case, is how we'd find the addresses.
5. **Verify the middle.** Their `verify_wide.py` checks that the center 256 still matches the real frame. A headless check like that for us would catch wide bugs without the phone.
6. Smaller: retry BoxTest only on a miss; vertical widening; zoom-out.

## Where things stand (11-07)

Checked in code on `gpu-2d`:
- **No GPU 2D renderer yet.** The branch has the step 1 counters and step 2 filtering (MMPX, Deband). plan-gpu-2d step 3, the shader renderer, isn't started.
- **The wings see only the 3D texture.** `WingPixel()` in `GPU_OpenGL_shaders.h` reads the line's control word, samples the 3D texture at the wing X, and then applies the BLDCNT fade and master brightness. It never sees the 2D planes, the backdrop or 3D alpha, which is why transparent 3D shows the clear color. `ScreenTex` holds the middle 256 only. Compute goes through the same `GLCompositor`, so wing work covers both renderers.
- **The control word is full.** All 32 bits are in use (brightness, hi-res capture, wing bits, 3D X scroll), so new wing data needs its own buffer.
- **Lua** can already read VRAM, OAM, palette and game RAM. `melon.draw` is clipped to 256 wide (`LuaDraw.h` `kScreenWidth`). `LuaEngine` runs on the emulator thread with the `NDS*`, so a `melon.wide` table can hand a config to the core. Lua is off in RetroAchievements hardcore, so per-game wide 2D will be too.
- BoxTest was always squeezed (`GPU3D.cpp` BoxTest); since step 3 it retries only a native miss. HalfCamera already replays a frame's FIFO into a shadow `GPU3D`, which a shadow polygon list could reuse later.

## Decisions (user, 11-07)

- **Wide 2D waits for the GPU 2D renderer.** No separate CPU wing pass. The wings get 2D from plan-gpu-2d step 3's shader renderer, so that renderer has to draw X outside 0..255 from the start.
- **First game for per-game 2D: HeartGold/SoulSilver battles.** It's what PokeDaisy tuned its battle 2D on, so we can compare directly. We don't have its RAM mapped yet. The pret `pokeheartgold` decomp is the place to find the addresses.
- **2D-only lines get edge repeat when it's safe**, not black. Safe means the same rule as with 3D: a layer covers the row edge to edge.
- **Shadow polygon list and vertical widening are deferred.** A dropped-polygon counter goes first. The shadow list gets built only if the counter shows real drops in real games.

## Plan

Steps 1 to 3 don't need the GPU renderer and can be done any time. Step 4 is plan-gpu-2d step 3, and steps 5 to 7 build on it. Sizes are rough line counts.

**1. Backdrop in transparent wings.** For each line, the CPU works out one wing pixel's planes: 3D over the backdrop, through the same blend rule as the middle (under WINOUT). It writes that into a new side buffer per line and half, swapped along with `Framebuffer` on POWCNT. The compositor uploads it as a small texture, and `WingPixel` runs the existing `CompositePixel` on it. That also replaces the hand-written fade code. Lines without 3D wings keep today's behavior until step 5.
- Files: `GPU2D_Soft.cpp`, `GPU2D.h`, `GPU.cpp/.h`, `GPU_OpenGL.cpp/.h`, `GPU_OpenGL_shaders.h`. About 150 to 200 lines.
- Risks: Adreno integer quirks (keep the flags as texture data, per MODS.md's Adreno gotcha), and keeping white and held lines first.
- Fold, on OpenGL and Compute: a game whose 3D is transparent over a colored backdrop, and Phantom Hourglass for the wing hold.
- Done 11-08 (core `widemelon`). The wing planes live after each screen's lines in its framebuffer, so they follow POWCNT, threaded 2D and the Blit after the readback for free; details in MODS.md ("Wing planes"). Changes from the plan: a wing only gets "3D over the backdrop" where its edge visibly shows the backdrop. The CPU checks the 2D (3D on top at column 0 or 255, backdrop under it, and the wing takes that column's planes, window effects included), and the shader lets the wing's transparent 3D show the backdrop only if the 3D one pixel in from the edge is transparent too; otherwise that pixel shows the 3D clear color, while 3D with alpha is still blended or faded with the edge's compmode and window effects. Lines where a 2D layer is over or under the 3D at the edge look as before step 1. Reasons: White 2's intro has a magenta backdrop under a grey BG, and Contra 4's "Licensed by Nintendo" screen is a screen-wide black 3D quad over a white backdrop, which the squeeze leaves out of the wings (the OnePlus test of the first version had white wings next to a black middle there). Test scene: Contra 4's Konami logo right after boot (white backdrop, transparent black 3D clear), found with a throwaway harness scan of the library. OnePlus, first version: the Konami logo, White 2's title, Phantom Hourglass's wing hold and Mario Kart DS passed on OpenGL and compute, filter 0 and 1; the edge check (core `d53f3a0c`, reworked in `c19ab873` to keep the edge's compmode and window effects) came after that. OnePlus, `c19ab873` (11-08): the same four games on OpenGL and compute, filter 0 and 1, no crashes or GL errors; logs in `perf_logs/2026-11-08-widemelon/oneplus-step1fix2/`. Fold (Adreno 840), `c19ab873` (11-08): the same 10 runs against the step 3 build, no crashes or GL errors, steady screens' middles pixel-identical, no white or junk wings (Konami's wings take the white backdrop, "Licensed by Nintendo" stays black as on the OnePlus); logs in `perf_logs/2026-11-08-widemelon/fold-step1fix2/`.

**2. Headless wide check.** A core debug switch plus a runner built on `tools/swi-harness` runs the same frames with wide on and off, using software 3D. It fails if the middle 256 changes: planes 0 to 2, the control words with the wing bits masked, and the capture banks. It also writes the wing output as PNGs, so wing 2D can be looked at without the phone. Once step 4 exists, the same runner also checks that the GPU renderer's middle 256 matches CPU 2D. About 30 core lines and 200 to 250 tool lines. No phone needed.

**3. Small 3D fixes.** Done 11-08 (core `widemelon`): BoxTest retry, the `debug.melonade.widedrops` counter (also in the wide check's summary), and geometry timing from the native view's clip result, checked with the wide check's `--timing native`. No drops in the check's three games at 342, so the counter still needs real games on the phone.
- **BoxTest retry on a miss.** Test the native box first, and only if that misses, test it squeezed plus about 4 px, so games that cull with BoxTest stop popping at the edges. About 25 lines in `GPU3D.cpp`. Check on a game that culls with BoxTest (`VertexIdLog->OnBoxTest` can help find one).
- **Dropped-polygon counter.** Count polygons dropped when polygon RAM is full with wide on, in the debug counters. About 20 lines. This decides whether the shadow list ever gets built.

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

The config is latched at line 0 and capped (4 BGs, 128 sprites). It's cleared when the script stops or the game resets. A wrong script can only spoil the wings, and step 2's check proves the middle is unaffected. Separately, `melon.draw` can be allowed to reach the wings by widening the overlay (`LuaDraw`, `FrameQueue`, `DSRenderer`). Size: about 250 core lines and 200 app lines, plus about 120 for the overlay. API errors can be tested on the AVD; the visuals need the Fold.

**7. HG/SS battles.** The first per-game script, using step 6: the battle background past the edges, Pokémon and trainers sliding in from off screen, and the HP boxes and text box carried to the edges. Map the battle RAM from the `pokeheartgold` decomp first, as [pokemon-battle-ram.md](pokemon-battle-ram.md) did for White 2, then compare on the Fold against PokeDaisy's version. White 2 battles can follow with the RAM we already have.

**Deferred.**
- **Shadow polygon list:** only if step 3's counter shows drops. If built, reuse HalfCamera's FIFO replay rather than copying `SubmitPolygon`. It doubles geometry work on the emulator thread (the OnePlus's limit), and BoxTest stays visible to the game anyway.
- **Vertical widening** (camera stage 2 below): needs a Y squeeze, a taller 3D target and compositor, and changes to the wing hold rows and the app's layout rects. Probably 600+ lines.

## Camera: pan and zoom (11-15)

What PokeDaisy's EXPAND does on DS (read from `pk_nds_wide.cpp` and `WideCamera.kt` on 11-15): the game's camera never moves. EXPAND renders margins around the real frame, then shows a window inside that wider picture each frame. Zooming out makes the window bigger, and the right stick slides it around, limited to the margins. The window drifts back to the middle when the player walks, and it snaps back to 1x on text, menus and the start of a battle, which it knows from Pokémon field RAM. The user wants zoom-out and look-around in WideMelon (11-15), with per-game behavior through Lua and no PC-from-anywhere style function calls.

**Stage 1, display-side camera.** This doesn't change the 3D path in the core.
- **Render and show widths are separate.** The core renders at a *render width* Wr, which is the widest view the zoom range needs (up to 768). The layout's wide screen keeps the *show width* the player picked (Wd, the current `wideViewWidth`). With the camera off, Wr = Wd and nothing changes.
- **The app shows a window** `[x, x+w)` of the Wr-wide top texture in the wide screen's rect. At 1x, w = Wd. Zooming out widens w toward Wr, and the picture is letterboxed top and bottom, because there's no vertical widening yet. Panning moves x, clamped to 0..Wr−w.
- **Controls.** The right stick pans. One mappable Camera Zoom hotkey, the right stick's click by default, steps through the zooms and back to 1x, like PokeDaisy's Expand, and the picked step is remembered per game. On touch, a drag or pinch on the wide screen does the same, since the top screen takes no DS touch. When the stick is let go and the player moves (D-pad or left stick), the window eases back to the middle.
- **Automatic 1x.** The window goes back to the middle at 1x when the frame has no 3D in the wings: the core counts the top screen's lines that have 3D wings. Lines the wing hold fills from an earlier frame count as wings, so dual-screen 3D games still pan and zoom (Mario Kart DS is the check); the held wings cover the whole render width, and once a game drops its 3D the holds end within 2 frames. It also goes back to 1x when a script asks for it.
- **The overlay follows the window.** Lua's `melon.draw` overlay is placed over the middle 256 wherever the window puts it. The FPS counter is pinned to the activity's corner, not the DS screen, so the camera doesn't move it. The bottom screen and touch input are unchanged.
- **Settings, per game in the Mods tab:** a Camera switch (off by default), a max zoom-out (it sets Wr), and return-to-center on move (on by default).
- **Lua:**
  - `melon.wide.camera()` → `x, zoom` (the window's offset from center in DS pixels, and the zoom factor, 1 = native);
  - `melon.wide.set_zoom(z)`, clamped to the allowed range, with `nil` handing control back to the player;
  - `melon.wide.pan(x)`, an offset from center in DS pixels, clamped, with `nil` handing control back;
  - `melon.wide.hold_native(bool)`, which forces 1x and the middle while it's true (menus, text, battles).

  Newest running script wins, and a script's requests go away when it stops, like `tilt_shift`. Lua is off in RetroAchievements hardcore, but the camera itself is display-only, so it stays available there.
- **Costs and risks:**
  - With the camera on, the GPU fills Wr, not Wd, even at 1x. Measure on the Fold and OnePlus at 342 shown with 512 rendered.
  - Wider views fill polygon RAM sooner. `widepolys` helps.
  - Pop-in at the far edges from the game's own culling gets more visible.
- **Size:** about 30 core lines (the wing-line count, and letting Wr exceed Wd), about 400–500 app lines (camera class, input, DSRenderer UVs and rects, overlay offset, settings), and about 120 Lua lines. OpenGL and Compute only, like the rest of WideMelon.
- **Checks:**
  - The AVD covers the settings UI and Lua API errors.
  - The Fold covers the visuals: Mario Kart DS and White 2 on OpenGL and Compute, with pan, zoom, auto-1x in a menu, and a script holding native.

**Stage 1b, if the fill cost shows (optional).** Render only the window: an asymmetric `ProjectX` that maps a native range `[x0, x0+Wr)` instead of a centered one, using PokeDaisy's off-axis form `(256x − (R−L)w) / (256+L+R)`. The stretch center at `GPU3D.cpp` HiresPosition, the BoxTest retry, `WideXAdjust` and the compositor's middle offset all move with it. This also lets the pan go past 768 wide.
- **11-15 phone data.** OpenGL showed no cost on the Fold or OnePlus. The OnePlus's Compute did: in White 2's field at 4x, 342 rendered gave 45 fps and 20.7 ms of work, while 512 rendered at zoom 1 gave 30 fps and 31.9 ms (`perf_logs/2026-11-15-wide-camera/`). The user's call (11-15) is to come back to this after the remaining Compute efficiency work, which may change these numbers.

**Stage 2, vertical zoom-out.** This is the deferred vertical widening above, about 600+ lines. It's what makes zooming out fill the screen instead of letterboxing.

**Stage 3, per-game real camera (idea).** A Lua script writes the game's own camera position or distance in RAM. The game then culls and draws for the new view, so there's no edge pop-in and no margin limit. The camera address has to be found per game (decompiling helps), and the game sees the change.

## Open after the merge (11-11)

Steps 1–3 are merged into `material3` / `melonade` (11-11). Phone data: `perf_logs/2026-11-08-widemelon/`.
1. The `widedrops` counter found real drops at Shroom Ridge's race start in Mario Kart DS (2,212 dropped, 94% would fit natively). Per the 11-07 decision that's the case for the shadow polygon list above, so building it is the user's call. 11-14: NSMB's title intro drops 137–244 polygons a frame at 426 and its 3D flowers vanish (all native-fit); the user chose a cheaper fix than the shadow list, native-counted polygon RAM with a bigger wide list: [plan-widemelon-polylist.md](plan-widemelon-polylist.md).
2. Optional: in compmode 4 the wing could take the edge's 3D alpha instead of 31, so wings follow fades of a translucent full-screen quad (Contra 4's fade into "Licensed by Nintendo" stays black in the wings, as before step 1).
3. Steps 4–7 wait for the GPU 2D renderer (`docs/plan-gpu-2d-shadow.md`).
