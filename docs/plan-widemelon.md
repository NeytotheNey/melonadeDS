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

1. **Shadow polygon list from a submit hook.** Squeeze copies, not the game's polygon RAM, so the polygon limit and list counts stop showing (and the RetroAchievements caveat goes). The cost is a copy of SubmitPolygon's work to keep in step with upstream.
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
- **Vertical widening:** needs a Y squeeze, a taller 3D target and compositor, and changes to the wing hold rows and the app's layout rects. Probably 600+ lines.
