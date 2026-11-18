# GPU 2D step 3: the shadow renderer

Plan for step 3 of [plan-gpu-2d.md](plan-gpu-2d.md), written 11-08. CPU 2D stays the real result. The shadow pass redraws lines it can prove it matches, at output resolution, on the GPU, for display only. Nothing in `GPU2D_Soft.cpp` changes, so CPU output, HASH2D, capture and threaded 2D can't change. It has to draw X outside 0..255 from the start for WideMelon steps 4–7 ([plan-widemelon.md](plan-widemelon.md)), and it builds on WideMelon's pieces: the block after `AccelWingPlanesStart`, `WingPixel` and `WingPlanesTex`, the hold order and the window edge patch.

**Status (11-17):** phase 0, increment 1 (core `e4b893bb`) and increment 5 (blending, core `aa597f11`) are done, phone-checked and in `material3` / core `melonade`; the `gpu-2d-shadow` and `widemelon` branches are merged and gone. The OnePlus display stall in mode `all` is fixed and merged (the planes pass, below; core `f65efa1a`, app PR #9). GLTIME dropping sets while the shadow runs is fixed on `fix/shadow2d-gltime` (in "Fixed", below). Open: whether OpenGL gets a pacing safety net (also in "Fixed"). Next: windows (increment 4).

## Phase 0: gameplay counters (done, 11-09)

Step 1's runs were title screens and menus, so before any renderer code the counters ran on gameplay, headless on the PC harness. New counters: `real` (pages dirty mid-frame whose bytes actually changed, a memcmp of the 512-byte page), `log` (distinct changed pages per frame), `logev`, `realpal`, and `logbothev` summing both engines. New harness modes: `--stats2d N`, `--state` on its own, `--tap KEY@FRAME[+N]`, `--touch`, `--sav`. The harness never writes saves (`WriteNDSSave` is a no-op in `tools/swi-harness/main.cpp`). Golden Sun used the user's states (cave, battle start, battle end with the spinning camera); the others used the user's states or button scripts from boot. Golden Sun's world map and menus weren't measured, and the battle run may not include a Psynergy cast. Data and screenshots are in `logs/gpu2dstats-phase0/`.

Harness gotchas: use `--bios free` for the app's states (the official NTR BIOS hangs Mario Kart on a course load). Kirby's and NSMB's states were made in DSi mode with DSP HLE (config word `0x00010001`) and load with the harness's existing DSi setup.

- **Golden Sun's mid-frame writes are real, but it isn't the hi-res design case** (logs in `logs/gpu2dstats-phase0/gs-pages/`). Engine B rewrites one 256-byte bitmap row on every line (counted as 2 pages because bank H is mirrored at 0x06200000 and 0x06210000): an ARM9 HBlank DMA copies the next row of a 256x192 8-bit picture from main RAM, re-armed at VBlank, and the visible bitmap BG samples exactly that row. Its "rotation" is nominal: the matrix is identity (A=256, B=C=0) with a fixed Y, so these are 1:1 pre-rendered pictures streamed through a line buffer, and hi-res gives them nothing. Step 1's `bmprot`/`rotbg` count any affine BG, so they overstate real rotation. Both engines together peak at 575 changes (287.5 KB) a frame in battle. Engine A is text BGs plus 3D, with captures on about half its lines in battle; its battle effects (up to about 190 changes) weren't looked at.
- **Every other game changes nothing mid-frame in gameplay.** Mario Kart race, Metal Slug 7 mission 1 and Kirby's Spring Breeze had zero changes; Contra 4's stage 1 had 7 frames with any, at most 8 OBJ pages.
- **Layers:** Mario Kart B and Metal Slug 7 B rotate two extended BGs (16-bit tilemap) with blending on every line, and Metal Slug 7 B has a window on every line (Mario Kart A has a window and OBJ-window sprites on every line). Only Advance Wars A rotates plain affine BGs. Contra 4 and Kirby are text BGs with blending. No game used mosaic. At most one rotated sprite on screen.

What this changed in the plan:

- "Gains something" has to mean a real rotation or scale (B or C nonzero, or A or D other than 256), not just an affine BG. Golden Sun's 1:1 layers stay CPU under mode `1` and never touch the page log or its cap.
- The page log counts physical bank pages, not mirrors. With that, the 512-page cap holds for everything measured. A per-line row log (at most 48 KB a frame) is only worth it if mode `all` has to cover line-buffer games.
- Blending is on every rotated line in all three rotating games, and windows on Metal Slug 7's, so windows and blending come before affine. Mosaic drops to last or is skipped.
- The real hi-res cases are Mario Kart B and Metal Slug 7 B (rotated extended BGs with blending, plus a window in Metal Slug 7) and Advance Wars. Increment 7 (bitmap BGs) drops in priority unless a game with truly rotated bitmaps turns up.

## Architecture

### Where things happen

- **Emulator thread, while each `LineJob` is built:** the recorder, `Shadow2DRecorder` in `src/GPU_Shadow2D.h/.cpp`, owned by `GPU` as a `unique_ptr` that's null while off, like `Stats2D` and `CapReads`. It hooks in at:
  - `GPU::Snapshot2DScanline` → `NoteLine(job)`;
  - `GPU::Snapshot2DSprites` → `NoteSprites(job)`;
  - `GPU::MakeVRAMFlat_2DBGCoherent` and `MakeVRAMFlat_2DOBJCoherent` → `NoteFlatDirty`, between `DeriveState` and the flat copies. That's where `Stats2D->NoteBGDirty` sits, since nothing else can see those sets once `DeriveState` clears the bits;
  - reset and load: `GPU::Reset`, `GPU::DoSavestate`, `InitFramebuffers`.
- **GL, only inside the Blit (`GLCompositor::RenderFrame`):** the uploads, the OBJ pass and the composite pass. They follow `LateBlit` and `BlitAfterReadback` for free, so in capture frames they run after the readback, off the line-192 map's critical path.
- **Why the Blit can read the recorder's data:** the Blit always runs by line 215, and the flats only change at the next BG snapshot (line 0) or line 262's OBJ snapshot. A frame stamp guards it: data from another frame sends every line to CPU.
- **Palette and OAM** are never read live at the Blit, since games write them in VBlank before a line-215 Blit. The recorder keeps its own copies, driven by `GPU::PaletteDirty` and `OAMDirty`, which are set in `WritePalette`/`WriteOAM` and read by nothing else.

### Per-line state

- **Where:** a block in the accelerated framebuffer after the wing planes (`GPU2D::AccelShadowStateStart`, at the old `AccelFramebufferWords`), 32 words a line, 24 KB per screen buffer. The recorder writes it into `job.Framebuffer[num]`. Like the wing planes it swaps with POWCNT, belongs to the composited frame with threaded 2D and the Blit after the readback, and is zeroed by `BlankFrame` and `InitFramebuffers`. `LogHash2D` only hashes the 192 lines, so HASH2D is unaffected.
- **Fields:** the BG `UnitState` serialized, plus the sprite line's OBJ state, which `NoteSprites` captures one HBlank earlier and keeps pending (sprite line 0 is built at VCount 262 of the frame before):
  - word 0: flags (unit, Enabled, ForcedBlank, LayerEnable, OBJEnable, VCount == Line, has-page-log, ineligible reason);
  - DispCnt, BGCnt[4], BGX/YPos[4], `BGX/YRefInternal`[2], BGRotA–D[2];
  - Win0/1 coords, WinCnt[4], Win0/1Active;
  - BG and OBJ mosaic sizes, `BGMosaicLine`, `OBJMosaicY`;
  - BlendCnt, EVA, EVB, EVY, the backdrop color as of the line, band and version indices;
  - wide: 2 bits per layer for BG0–3 and OBJ (0 = transparent past the edge, the default; 1 = draw; 2 = edge repeat, reserved for WideMelon 5a), and left/right extents per BG (u8 each, for `melon.wide.bg`).
  - Master brightness, display mode and X scroll stay in the control word.
- **Upload:** `LineStateTex`, `RGBA32UI` 8×384, about 48 KB a frame, with each half's rows laid out like `ScreenTex`. Signed and 16-bit fields are masked and sign-extended in GLSL (`& 0xFFFFu`), per the Adreno note at the end of `kHiresCaptureFS_Main`.

### Memory and the mid-frame rule

- **`MemTex`:** one `R32UI` texture, 256 texels (1 KB) a row. It holds the flats (A BG 512 rows, B BG 128, A OBJ 256, B OBJ 128, BG ext palettes 32+32, OBJ ext palettes 8+8), then palette rows, then page-log rows. About 1,424 rows (1.4 MB), under GLES's 2048-row minimum.
- **Base upload:** `glTexSubImage2D` per run of pages in the union of the dirty sets since the last upload. The sets come from the `NoteFlatDirty` hook ORing the derived bits, so there's no second tracker. The source is the flats, which hold the state as of the frame's last line. A generation counter (process-wide, so a new recorder can't repeat an old value) forces a full 1.4 MB upload after a reset, a load, a renderer switch or a new compositor.
- **Page log (increment 1b, not built):** when a page is dirty at line L > 0 and its bytes really changed (a memcmp against a copy taken before the flat copy), the old page is kept as "valid for lines [previous change, L)". Palette pages (4 × 512 bytes) go through the same log from the recorder's copy.
  - At the Blit the log goes to `MemTex` rows, plus `PageLogTex` (`R32UI`, one row per changed page, 192 entries, each a history row or "base"). Only lines flagged has-page-log pay for the lookup in the shader.
  - Cap: 512 physical pages (256 KB) per frame. On overflow at line L, lines 0..L−1 go to CPU, since their version of that page wasn't kept. Lines from L on are still right.
  - Until 1b, lines before the frame's last real change are CPU (reason `memchanged`).
- **OAM never goes to the GPU.** The recorder turns each sprite line's OAM and OBJ state into sprite instances (OAM index, row range, decoded attributes, affine parameters). It splits a sprite's row range wherever OAM or the OBJ state that matters (DispCnt OBJ bits, OBJEnable, the screen half) changes. They're uploaded as a vertex buffer with `glVertexAttribDivisor`, not a UBO (GLES only guarantees 16 KB). Cap: 2048 instances; lines past that go to CPU.

### Passes and targets

1. **OBJ pass,** set up beside `PrepareFilter2D`'s targets, before the output is bound.
   - It draws instance quads into `ObjTex` (`RG32UI`, ViewWidth × 384 at ObjScale) with a 16-bit depth buffer.
   - R holds the CPU's `OBJLine` pixel format exactly: color or palette index, 0x8000/0x1000 for direct and standard palette, priority 0x30000, opaque 0x40000, mosaic 0x100000, and the flag byte.
   - Depth is `prio*128 + index` with `GL_LESS`, which is the "lowest (priority, index) wins" rule of `SoftRenderer::DrawSpritePixel`. Upstream's `2DSpriteVS.glsl` uses the same key.
   - OBJ-window sprites (increment 4) are a second draw into G only (`glColorMaski`, depth off), then `glInvalidateFramebuffer` on the depth. Increment 1 left them out: they can't matter with windows off, and per-channel `glColorMaski` on RG32UI is a driver risk.
   - ObjScale is 1 until increment 3. From then it's the 3D scale, capped at 4, on frames with a visible rotated sprite.
2. **Planes pass** (`SHADOW2D_PLANES`, `RunShadow2DPlanes`), right after the OBJ pass, also before the output is bound. It draws the drawn row runs at 1x into `PlanesTex` (`RGBA32UI`, ViewWidth × 384, no gap between the screens, the wings too on lines that draw them).
   - Each fragment is one native pixel. It evaluates the layers in the CPU's priority order (`DrawScanlineBGMode<N>`) and pushes a 3-entry stack the way `DrawPixel_Accel` does, including the backdrop, the 3D placeholder 0x40000000 and sprites through the `InterleaveSprites` palette lookup.
   - Then it builds the planes: unit A takes the 4-way branch of `DrawScanline_BGOBJ`'s accelerated path, and unit B takes `ColorComposite(top, second)` with plane 1 = 0 and plane 2 = 0x07000000. It writes planes 0–2 as words, and in A whether a 2D layer drew the pixel.
3. **Composite pass,** a program on the same output FBO, right after the compositor's `glDrawArrays` and before `CopyHeldWings` and `RunHiresCapture`.
   - Quads cover only the row runs drawn this frame. Each fragment works from highp `gl_FragCoord` (as `FilterSubPixel` does), reads its native pixel's planes and runs the existing `CompositePixel`, with the 3D fetched at the fragment's real position (`Fetch3D`, the WIDE variant for wings) and master brightness from the control word. Blending with 3D, fades and brightness stay exact by construction. A wing pixel no 2D layer drew goes to `WingPixel()`.
   - Text BGs and normal sprites use the native X (`floor(sub/scale)`), so their planes don't depend on the sub-pixel and working them out once per native pixel is exact at any scale. Affine BGs (increment 2) will need integer sub-pixel coordinates (`ref*S + subX*A + fy*B`, floor-divided by `256*S`), so their layers will have to be worked out at the output's scale.
   - **The 1x planes are exact only while every drawn layer is a text BG or a normal sprite.** Affine BGs (increment 2) and ObjScale above 1 (increment 3) need sub-pixel planes (a planes target at that scale, or those layers worked out in the composite) or a guard that keeps such lines off the 1x path.
4. **Check** (`check` mode). Every 30 frames the middle 256 columns of `PlanesTex` are read back with `glReadPixels` and compared word for word with `gpu.Framebuffer[backbuf]` on drawn lines. That's the only synchronous readback, and only in check mode. On a checked frame the planes pass starts from a sentinel (0xDEADBEEF) instead of discarding `PlanesTex`, so a texel it misses can't pass on the last frame's planes.
   - Engine A's plane 1 under the backdrop is a leftover of the previous pass unless `EngineBNoHold` clears it (see the `DrawScanline_BGOBJ` comment). `EngineBNoHold` is on in every build since 11-16; with it off (the harness without `--bnohold`), unit B lines with an alpha blend that has the backdrop as 1st target stay CPU (reason `bleftover`).

### GPU or CPU per line

- **Feature-eligible** is decided by the recorder from the line's state, for the increments that have landed. The base rules: display mode 1, enabled, not forced blank, VCount == Line, not capture-armed (engine A), screens on. With increments 1 and 5: text BGs only, no windows (`DispCnt & 0xE000`), no BG or OBJ mosaic (decided from the mosaic bit alone, whatever the size), and no rotated sprite on the line. Hi-res display lines are display mode 2 or bitmap BGs, so `dispmode` or `bgtype` catches them before the `hires` reason.
- **Drawn** is decided at the Blit, in the compositor's uploaded copy of word 0 (bit 31). A line is drawn if it's feature-eligible, the page log covers it, and it isn't a hi-res display line (`HiresDisplayFlag` after `PrepareHiresDisplay`), held or white. Then the mode's policy applies:
  - `1`: only lines that gain something: scale 2 or more with a real rotation or scale on a BG or sprite, or 2D the wide config sends into the wings;
  - `all`: every feature-eligible line.

  It's never a uniform mask, per the Adreno gotcha in MODS.md.
- **No control word bit is used.** Bits 19–20 are the only ones free in every mode (8–12 are only clear in a wide view, 13 is the compositor's held flag), so they stay free for later.

### X outside 0..255 from the start

- The shader's native X is a signed int, so text BGs wrap through their tilemap, affine BGs run `x*A` past the edge, and `ObjTex` spans the wings with sprites at their signed 9-bit X.
- Each layer's wide mode decides whether it shows outside 0..255. The default is off, so the wings look exactly as today.
- On a line where some layer draws past the edge, the shadow quad covers the wings too. A wing pixel with a 2D layer goes through `CompositePixel` with the 3D at the wing's X. One with only backdrop and 3D calls the existing `WingPixel()`, so its edge checks (Contra 4's squeezed quad) are kept.
- Windows outside 0..255 use the edge column's mask, matching the CPU's edge patch in `DrawScanline_BGOBJ` (`WindowMask[0] = WindowMask[1]`).
- Held wing lines keep the held copy. `CopyHeldWings` runs after the shadow pass, so held wings include its 2D.
- WideMelon step 6's `melon.wide.obj(i, x)` plugs into instance building, and `melon.wide.bg` into the per-BG extents.

### Texture units and cost

- **Units:** the compositor uses 0 ScreenTex, 1 3D, 2 HeldWings, 3 HiresBank, 4–6 the capture pass, 7 `MakeHiresTexture`'s scratch binding, 8 HiresStream, 9 FilterMap and 10 WingPlanes. The shadow programs draw with those bindings in place and add 11 `MemTex`, 12 `LineStateTex`, 13 `ObjTex`, 14 `PageLogTex` and 15 `PlanesTex`.
- **Mobile cost:** two extra render target switches per frame (the OBJ and planes passes) and no per-BG layer targets (upstream has 22 in `GPU2D_OpenGL.cpp`). Drawn rows are composited twice, once by the normal pass and again by the shadow. Giving the normal pass row runs too is the first optimization if cost bites.

## Increments

Order (user, 11-09): text BGs and sprites (1), then blending (5), windows (4), affine BGs (2), affine sprites (3), bitmap BGs (7), mosaic (6) last. 1b waits until a game needs it, most likely Golden Sun with bitmap BGs: the replay logs show it would gain almost nothing now (`memchanged` held back lines only in SoulSilver's boot, 130–258 of 115,200). Sizes are rough line counts; a phone step is one install-and-test round.

Device-free checks for every increment:
- a host build of the core;
- `.claude/shadercheck/check.sh`, with `dump.cpp` updated by hand;
- `tools/swi-harness/wide_check.sh` unchanged with the recorder on (`SWI_HARNESS_SHADOW2D=on|both`);
- HASH2D unchanged with the recorder on and off (harness `--accel --hash2d --shadow2d N`, plus `--threaded2d`);
- the shader replay (`Shadow2DReplay.cpp`, a C++ copy of the OBJ and composite shaders compared with the CPU's planes) from the phase 0 states: 0 plane mismatches and 0 stale `MemTex` words. Scripts are in `logs/gpu2d-shadow/` (`host_checks.sh`, `replay_batch.sh`).

Phone proof for every increment: `check` at 1x and 4x with 0 mismatches on OpenGL and compute, on the Fold and the OnePlus; `all` vs `0` exact-frame pairs at 4x; and a cost A/B that looks at presented fps and render ms, not only emulation fps.

1. **Text BGs and normal sprites, plus the whole frame. Done**, results below.
1b. **Page log:** mid-frame memory versions, palette included.
   - Files: `GPU_Shadow2D.cpp` and shaders (about 250), plus a harness `--shadow-replay` (about 150 tool lines). Every 16th line it keeps a full copy of the flats and palette, then at the Blit compares them with what the base plus the log rebuilds for that line.
   - Proof: replay passes from savestates (Golden Sun, Contra 4, Kirby, White 2). `check` gives 0 on scenes with `bgv` changes, and the log shows those lines kept on the GPU.
   - 1 phone step.
2. **Affine BGs, hi-res.** `DrawBG_Affine` and the tiled `DrawBG_Extended` (the kind most rotated games use), including the no-wrap and ext palette cases.
   - Files: shaders and recorder rules, about 230.
   - Proof: `check` at 1x on Advance Wars A (plain affine), then Metal Slug 7 B, Mario Kart B, Advance Wars B and Kirby B. At 4x and 8x, only the rotated layers differ from off (a diff mask), and they look sharp. The Mario Kart state used so far is all text BGs, not the rotated race scene.
   - 2 phone steps.
3. **Affine sprites,** with ObjScale at the 3D scale (capped at 4) on frames that have one.
   - Files: about 200.
   - Proof: Kirby A (`sprrot` up to 4) and Mario Kart B, `check` 0, plus a cost check of `ObjTex` at 8x wide.
   - 1–2 phone steps.
4. **Windows.** A closed form of `CalculateWindowMask`'s scan (carry-in from `Win0Active`/`Win1Active`, x2 checked before x1), the OBJ window from `ObjTex.g`, WINOUT, and the edge patch under WideView or WideCheck. The window mask is a constant `0xFF` in the shader today, so this only has to compute it per pixel.
   - Files: about 150.
   - Proof: Metal Slug 7 B, Kirby B and Advance Wars' dialogs, `check` 0, exact pairs.
   - 1 phone step.
5. **Blending. Done**, results below.
6. **Mosaic.** BG mosaic (latched refs, the X table) and OBJ mosaic with `ApplySpriteMosaicX`'s latch rule (at most 16 fetches back a pixel).
   - Files: about 250.
   - No game in the step 1 set uses it, so first find a scene with a harness scan for `bgmos`/`objmos`. If none turns up, skip it and mosaic stays CPU.
   - 1 phone step.
7. **Bitmap BGs, hi-res.** The bitmap modes of `DrawBG_Extended` and `DrawBG_Large`.
   - Files: about 150.
   - Proof: Golden Sun on both engines (needs 1b), `check` 0, sharp at 4x.
   - 1–2 phone steps.
   - **7b, hi-res capture Phase 2, if it fits.** A bitmap BG whose range resolves to one bank A–D with a valid copy samples `HiresBankTex` (unit 3). Bank validity has to catch writes through the BG, OBJ and ARM7 mappings too ([hires-capture.md](hires-capture.md)). About 250 lines. First log White 2's battle registers to confirm it's a bitmap BG.

### Increment 1 results (11-09)

Decisions taken with it (the plan's defaults): the page log counts physical pages with a 512 cap; mode `1` draws only lines with a real rotation or scale or 2D in the wings; drawn lines lose MMPX on their non-rotated layers when "Smooth 2D edges" is on; compute is allowed, opt-in.

**What landed** (about 2,700 lines with the harness tools):
- Core: `GPU_Shadow2D.h/.cpp` (recorder), hooks in `GPU.cpp/.h`, `GPU2D.h` (`AccelShadowStateStart`), `GPU_OpenGL.cpp/.h` (`PrepareShadow2D`, `RunShadow2D`, `CheckShadow2D`, `LogShadow2D`, `FreeShadow2D*`) and `GPU_OpenGL_shaders.h`, reset calls in `GPU3D_OpenGL.cpp` and `GPU3D_Compute.cpp`.
- Harness: `--accel`, `--hash2d`, `--shadow2d N` and `--threaded2d` in `main.cpp`; `SWI_HARNESS_SHADOW2D` for `wide_check.sh`; `Shadow2DReplay.cpp`.
- App: `gpu2d` and `gpu2dwide` in `DebugProperties`. `.claude/shadercheck`: 4 new programs.
- `PageLogTex` isn't created yet (1b).

**Checks:** all device-free checks passed (shadercheck 75 ok; HASH2D identical over 1,200 frames on 8 games plus threaded 2D on Kirby and White 2; replay 6.67 M eligible lines with 0 mismatches, and a deliberate BG X+1 gave 2.6 M, so the compare is live). On the phones, `check` gave 0 mismatches and 0 failures at 1x and 4x, OpenGL and compute, on the OnePlus (`perf_logs/2026-11-09-shadow2d/oneplus/`) and the Fold (user-run field build, harder settings: "Smooth 2D edges", upscaled capture, threaded 2D and the inner screen's wide view all on). `all` vs `0` exact pairs had 0 differing pixels, with no row patterns, so the mediump texcoord worry didn't show on the Adreno 640. HASH2D on the OnePlus with threaded 2D on and off matched `0` over 600 frames.

**WideMelon 342:** `gpu2dwide 0` keeps the wings identical to `0`. `gpu2dwide 1` draws policy `1` lines with the middle exact, and Advance Wars' intro letters, bars and banner carry into the wings. In Kirby and White 2 the wings looked the same as off; that's worth a look in WideMelon step 4.

**Cost:** the recorder averages 0.39 ms a frame on the OnePlus at OpenGL 4x (95–153 µs on the Fold), under the 1 ms stop line. Uploads average 45 KB a frame when drawing (max 104).

### Increment 5 results (11-09)

About +360/−40 lines: GLSL copies of `ColorComposite`, `ColorBlend4/5`, `ColorBrightnessUp/Down`; the three-entry stack is back (increment 1 kept two); unit A's planes come from `DrawScanline_BGOBJ`'s four branches. No new programs, so `GPU_OpenGL.cpp` and `dump.cpp` didn't change. Harness `--bnohold` flag added.

- **Checks:** all device-free checks passed (logs in `logs/gpu2d-shadow/inc5/`), with replay at 0 mismatches on all 20 scenes with `EngineBNoHold` off and on. Deliberate bugs are caught: EVA/EVB swapped (Mario Kart, Contra 4, Phantom Hourglass, DQ9) and EVY+1 on darken (NSMB). Not covered by any scene: brighten (effect 2), same code shape as darken.
- **Phones:** `check` 0 mismatches and 0 failures in all 32 OnePlus runs and all 24 Fold runs (NSMB, Mario Kart, White 2, Contra 4, Advance Wars, Kirby, Phantom Hourglass; OpenGL and compute, 1x and 4x), no crashes or GL errors (`perf_logs/2026-11-09-shadow2d-inc5/`). `bleftover` can't fire with `EngineBNoHold` on, so only the harness replay tests it. The recorder costs 200–480 µs a frame on the OnePlus and 100–160 µs on the Fold.

Eligible lines in the replay, increment 1 → 5 (first-match reasons, so window and bgtype now show what blending used to hide):

| Scene | Inc 1 | Inc 5 | Still CPU |
|---|---|---|---|
| Mario Kart, NSMB, White 2, Kirby states | 0–100% | 100% | – |
| DQ9, Platinum, SoulSilver boots | 42–65% | 93–96% | dispmode |
| Advance Wars boot | 20% | 84% | window 8%, bgtype 7% |
| White 1 boot | 67% | 83% | dispmode 14% |
| TWEWY boot | 0% | 75% | window 21% |
| Rune Factory boot | 10% | 72% | bgtype 19%, window 7% |
| Kirby boot | 67% | 71% | bgtype 21% |
| Contra 4 boot | 0% | 70% | dispmode 23% |
| NSMB boot | 0% | 60% | bgtype 39% |
| Phantom Hourglass boot | 0% | 54% | bgtype 23%, capture 22% |
| Golden Sun cave | 0% | 50% | bgtype 50% (engine B bitmap) |
| Animal Crossing boot | 0% | 49% | window 35%, bgtype 14% |
| Metal Slug 7 boot | 0% | 38% | bgtype 40%, capture 22% |
| Golden Sun battle | 0% | 0% | bgtype, dispmode |

On the phones, drawn lines went from 27% to 93% on White 2's title, 65% to 93% on Advance Wars, 0% to 78% on NSMB and 0% to 52% on Contra 4. Still on CPU there: Mario Kart bgtype 32% and window 31%; Kirby bgtype 45% and window 19%; Contra 4 dispmode 39% and memchanged 8%.

### Fixed: the OnePlus display stall in mode `all` (11-17)

- **Symptom:** at OpenGL 4x on the OnePlus 7 Pro (Adreno 640), emulation stayed near 60 fps in mode `all`, but NSMB's world map and White 2's overworld presented about 4 fps, at about 250 ms of render time per presented frame. The Fold (much newer Adreno) had no problem. The `check` runs missed it because their fps is emulation speed.
- **Cause:** the composite worked out the whole 2D layer stack (state fetches, up to four text BGs with three dependent `MemTex` fetches each, the sprite) at every output pixel, 16 times per DS pixel at 4x, though the result only depends on the native pixel. That cost 14.2 ms of GPU a frame on the map (`gltimers` `cx_shadow`), which put the emulator's GPU work at about 16.9 ms a frame, over a frame's time before the presenter's own draw. Uploads and the OBJ pass cost under 0.1 ms.
- **Why it collapsed instead of slowing down:** nothing on OpenGL makes the CPU wait for the GPU. The presenter's waits are GPU-side (`eglWaitSyncKHR`) and the emulator only flushes, so the overrun queued up frame after frame: render ms per presented frame climbed from about 50 to 300 ms over 20 s, and `GLTIME` fell behind (`untimed` up to 66 a second).
- **Fix:** the planes pass (see "Passes and targets") works the planes out once per native pixel at 1x, and the composite only reads them. Exact by construction for text BGs and normal sprites; byte-identical on the desktop in 5 games and 3 views, and `check` 0 mismatches on the phone.
- **Cost of the fix:** both shadow programs (the planes pass and the composite) compile on the first frame that draws a line, a one-time hitch.
- **OnePlus, OpenGL 4x, `all`:** NSMB map 2.5 → 59.9 presented fps, render 310 → 2.0 ms, `cx_shadow` 14.2 → 2.0 ms, the planes pass 2.1 ms. White 2 overworld 2.6 → 59.9 presented fps, render 309 → 2.3 ms, `cx_shadow` 14.3 → 1.9 ms. `check` 59 checks, 0 mismatches.
- **Final OnePlus build (`perf_logs/2026-11-17-shadow2d-final-oneplus/`):** OpenGL 4x, NSMB map and White 2 overworld at 59.9 presented fps, render about 2 ms, `check` 0 mismatches. Compute 4x with pacing, `0` → `all`: no stall, `compx` +1.7 ms of GPU; NSMB 59.9 fps, render 7.2 → 12.4 ms; White 2 59.9 → 58.9 emulation fps, work 11.6 → 14.8 ms; `check` 0 mismatches.
- **GLTIME dropped sets while the shadow ran (fixed, timing only):** in `all`, `bad=` dropped about 22% of sets on the Fold and nearly all of NSMB's steady map scene on the OnePlus (about 120 a second), while White 2 there was clean. Cause: Adreno reorders the independent ObjFB and PlanesFB passes inside the single `cx_sobj` query, so its start lands after its end. Fix (branch `fix/shadow2d-gltime`): `RunShadow2DObj` and `RunShadow2DPlanes` start a new `CompShadowObj` query right after their framebuffer bind, so `cx_sobj` is three back-to-back queries (uploads, OBJ pass, planes pass) summed into the same column, and `cx_sobj_n` still counts a Blit once. OnePlus, OpenGL 4x, `all` (`perf_logs/2026-11-17-shadow2d-gltime-oneplus/`): NSMB map `bad=` 119 a second → 0, every set timed, `cx_sobj` 2093 µs (it read 0 before); White 2 still clean, `cx_sobj` 1992 → 2014 µs. The Fold wasn't re-run.
- **Fold (11-17, Adreno 750, inner screen, plugged in, `perf_logs/2026-11-17-shadow2d-fix-fold/`):** `check` 0 mismatches on OpenGL and Compute in NSMB and White 2, no GL errors, 60 presented fps in every mode. `all` costs +1.1 ms (NSMB) and +1.7 ms (White 2) of GPU at OpenGL 4x.
- **Pacing:** a test switch that held the emulator thread before each Blit until the previous one finished (about 6 ms of waiting a frame) turned the collapse into 55 fps with every frame presented, but left the GPU cost as it was. It only changes how an overrun shows. Whether OpenGL wants such a safety net (Compute has `GPU::ComputePace`) is a separate decision.
- Data: `perf_logs/2026-11-17-shadow2d-stall-oneplus/` (the stall with `gltimers`), `perf_logs/2026-11-17-shadow2d-fix-oneplus/` (the fix, the cost split and the pacing test) and `perf_logs/2026-11-17-shadow2d-final-oneplus/` (the final build, OpenGL and compute). The 11-09 numbers are in `perf_logs/2026-11-09-shadow2d-inc5/oneplus-cost/`; on compute, that stall came from before Compute render pacing.

## Switches

- **`debug.melonade.gpu2d`:**
  - `0` (default);
  - `1`: lines that gain something;
  - `all`;
  - `check`: all, plus the 1x readback every 30 frames;
  - `log`: `1` plus a once-a-second `GPU2D` line (lines drawn, lines on CPU by reason, upload KB, log pages, recorder µs, check results).

  It isn't called `shadow2d`, because `shadowlog` already means 3D shadows.
- **`debug.melonade.gpu2dwide`:** `1` makes every layer draw past the edge. Test only.
- **No user setting** until increments 2, 4 and 5 make something visible. Then "Sharper rotated 2D" under Settings → Display, OpenGL and compute, default off, hidden on software, plumbed like "Smooth 2D edges".
- **Compute** runs the same code, since `ComputeRenderer::Blit` uses the same `GLCompositor`. It's GPU-bound on the OnePlus, so it stays opt-in there.

## Risks and when to stop

- **Recorder cost on the emulator thread.** The OnePlus at OpenGL is CPU-limited. Stop and rethink if the recorder plus uploads add more than about 1 ms a frame in mode `1`. Off, it has to cost nothing (a null pointer).
- **GPU cost.** Stop if mode `1` drops the Fold below 60 at OpenGL 4x in Metal Slug 7 or Mario Kart, or costs the OnePlus more than the filter does (3.8 Mclk a frame). Removing the double composite of drawn rows comes first.
- **Exactness.** An Adreno integer quirk that a phone step or two can't fix in `check` keeps that feature on CPU. A mismatch the recorder can't explain (state not captured) means a stricter line rule, never "close enough".
- **Memory.** `ObjTex` at 8x and 342 wide is about 34 MB, hence the ObjScale cap of 4.
- **Filter.** With "Smooth 2D edges" on, drawn lines lose MMPX on their non-rotated layers. Remapping native-resolution layers through the filter map would fix it at more cost.
- **Out of scope, these lines stay CPU:** VCount writes (lines from there on), display modes 0, 2 and 3, the FIFO, and capture.

Code to read first: `GPU.cpp` (`Snapshot2DScanline`, `Snapshot2DSprites`, `MakeVRAMFlat_2DBGCoherent`/`2DOBJCoherent`, `StartScanline` line 192, `StartHBlank` line 215), `GPU_OpenGL.cpp` (`GLCompositor::RenderFrame`, the `PrepareFilter2D` pattern), `GPU_OpenGL_shaders.h` (`kCompositePixelFS`, `WingPixel`, `Fetch3D`, `FilterSubPixel`), `GPU2D.h` (`AccelWingPlanesStart`, `LineJob`, `UnitState`), `GPU2D_Soft.cpp` (the reference: `DrawScanline_BGOBJ`, `DrawSpritePixel`, `InterleaveSprites`, `DrawBG_*`), `GPU_Shadow2D.cpp`, `GPU_2DStats.h` and `tools/swi-harness/main.cpp`.
