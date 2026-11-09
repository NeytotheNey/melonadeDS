# GPU 2D step 3: the shadow renderer

Plan for step 3 of [plan-gpu-2d.md](plan-gpu-2d.md), written 11-08. Increment 1 is built and tested on the OnePlus and the Fold, on branch `gpu-2d-shadow` (11-09, results below). Increment 5 (blending) was moved up and passes the device-free checks; its phone step is next. CPU 2D stays the real result. The shadow pass redraws lines it can prove it matches, at output resolution, on the GPU, for display only. Nothing in `GPU2D_Soft.cpp` changes, so CPU output, HASH2D, capture and threaded 2D can't change. It has to draw X outside 0..255 from the start for WideMelon steps 4–7 ([plan-widemelon.md](plan-widemelon.md)).

## What step 1's logs say for this

From the raw `logs/emulator/gpu2dstats/*.log`, not only the table in plan-gpu-2d.md:

- **Most rotated layers aren't plain affine BGs.** Only Advance Wars A rotates plain affine BGs (`affrot`, some lines without blending or windows). Metal Slug 7 B, Mario Kart B, Advance Wars B and Kirby B rotate extended BGs with a 16-bit tilemap (`extrot`). Golden Sun rotates bitmap BGs on both engines (`bmprot`).
- **Nearly every other rotated line has BLDCNT blending on**, and Metal Slug 7 B also has a window on every line. So outside Advance Wars A, the affine increment shows little until windows and blending land.
- **Golden Sun changes BG VRAM pages on every line:** `bands` and `bgv` peak at 191 per frame, with uploads averaging about 120 KB and peaking at 268 KB a frame. These are dirty pages, not changed bytes (`GPU::CopyLinearVRAM` copies and reports anything dirty), so they may be rewrites of the same values.
- So neither rule from the old sketch fits the design case. Falling back to CPU for the rest of the frame means Golden Sun never shows hi-res. Bands mean about 191 draws and uploads per engine per frame. The plan logs the pages that really change instead, and phase 0 checks this on gameplay.

## Phase 0: gameplay counters

The step 1 runs were title screens and menus. Before any renderer code, run the counters on gameplay: Golden Sun: Dark Dawn first (overworld, a battle with Psynergy, the world map, menus), then Kirby, Metal Slug 7, a Mario Kart race and Contra 4.

- **Counter additions** (`GPU_2DStats.h`, about 40 lines):
  - `real`: pages dirty mid-frame whose bytes actually changed (a memcmp of the 512-byte page before and after the flat copy);
  - `log`: distinct changed pages per frame, avg and max, which is what the page log below would store.
- **Reaching gameplay safely.** The swi-harness never writes saves (`WriteNDSSave` is a no-op in `tools/swi-harness/main.cpp`) and already loads a `.mlN` from a copy (`--state`; `wide_check.sh` copies into `$OUT/states`). Add a `--stats2d` mode that runs N frames from a savestate with `Set2DStatsLogging(true)` (about 40 tool lines). That needs gameplay savestates. Making a state on a phone is safe; only loading one overwrites the `.sav`. The alternatives are playing a debug build with `debug.melonade.gpu2dstats` on, or the AVD with a throwaway `.sav` copy and scripted taps.
- **What it decides:**
  - whether `real` pages stay small enough for the page log and its cap (Golden Sun's 268 KB peak of dirty pages is already over the 256 KB cap below);
  - whether Golden Sun's gameplay uses rotated bitmaps (increment 7) or tiled affine BGs.

### Phase 0 results (PC harness, 11-09)

Phase 0 is done. Golden Sun's world map and menus weren't measured (no state for them), and the battle run may not include a Psynergy cast.

Run headless with the new counters (`real`, `log`, `logev`, `realpal`; `logbothev` sums both engines) and harness modes (`--stats2d N`, `--state` on its own, `--tap KEY@FRAME[+N]`, `--touch`, `--sav`). Golden Sun used the user's states (cave, battle start, battle end with the spinning camera); the others used the user's states or button scripts from boot. Data and screenshots are in `logs/gpu2dstats-phase0/`. Harness notes: use `--bios free` for the app's states (the official NTR BIOS hangs Mario Kart on a course load); Kirby's and NSMB's states were made in DSi mode with DSP HLE (config word `0x00010001`) and load with the harness's existing DSi setup.

- **Golden Sun's mid-frame writes are real.** Engine B rewrites the same 2 pages with new bytes on every line: 382 real changes a frame against 382 dirty, in every scene. Distinct pages stay at 2, but the log needs an entry per change, so about 191 KB a frame from engine B alone. Engine A changes almost nothing outside battle effects.
- **Over the cap in battles.** Both engines together peak at 575 changes (287.5 KB) a frame in the battle, against the plan's 512 pages / 256 KB; 4 of the battle run's 33 sixty-frame windows went over. The random battle in the cave state peaked at 485. Walking and the spinning camera sit at 382–404.
- **Golden Sun's layers:** engine B draws rotated 8-bit bitmap BGs on every line (two in battle) with blending on every line. Engine A is text BGs plus 3D, with captures on about half its lines in battle. No windows, mosaic or rotated sprites.
- **Every other game changes nothing mid-frame in gameplay.** Mario Kart race, Metal Slug 7 mission 1 and Kirby's Spring Breeze had zero changes; Contra 4's stage 1 had 7 frames with any, at most 8 OBJ pages.
- **Their layers:** Mario Kart B and Metal Slug 7 B rotate two extended BGs with blending on every line, and Metal Slug 7 B has a window on every line (Mario Kart A has a window and OBJ-window sprites on every line). Contra 4 and Kirby are text BGs with blending; no rotated BGs in these stretches. No game used mosaic. At most one rotated sprite on screen.

**Golden Sun's 2 pages (11-09, logs in `logs/gpu2dstats-phase0/gs-pages/`).** They're one 256-byte bitmap row, counted twice because bank H is mirrored at 0x06200000 and 0x06210000. An ARM9 HBlank DMA (64 words, destination reloaded) copies the next row of a 256x192 8-bit picture from main RAM into that row every line, re-armed at VBlank. The visible bitmap BG samples exactly that row on every line (cave: BG3 at row 15; battle: BG2 at row 63), so the per-line versions are what the screen shows. About 250 of the 256 bytes change per line. Its "rotation" is nominal: the matrix is identity (A=256, B=C=0) with a fixed Y, so these are 1:1 pre-rendered pictures streamed through a line buffer, and hi-res gives them nothing. Step 1's `bmprot`/`rotbg` count any affine BG, so they overstate real rotation.

What this changes in the plan:

- **Golden Sun isn't the hi-res design case.** Engine B's layers gain nothing from the shadow renderer, so under mode `1` they stay CPU and never touch the page log or its cap. "Gains something" has to mean a real rotation or scale (B or C nonzero, or A or D other than 256), not just an affine BG. Engine A's battle effects (up to about 190 changes) weren't looked at.
- **The page log:** log by physical bank page, not by mirror, which halves Golden Sun's count. With that, and its 1:1 layers left on CPU, the 512-page cap holds for everything measured. A per-line row log (at most 48 KB a frame) is only worth it if mode `all` has to cover line-buffer games.
- **Increment order:** blending is on every rotated line in all three rotating games, and windows on Metal Slug 7's, so windows and blending should come right after text BGs, before affine (open decision 2). Mosaic can drop to last or be skipped.
- **The real hi-res cases are Mario Kart B and Metal Slug 7 B** (rotated extended BGs with blending, plus a window in Metal Slug 7) and Advance Wars. Increment 7 (bitmap BGs) drops in priority unless a game with truly rotated bitmaps turns up.

## Architecture

### Where things happen

- **Emulator thread, while each `LineJob` is built:** a new recorder, `Shadow2DRecorder` in `src/GPU_Shadow2D.h/.cpp`, owned by `GPU` as a `unique_ptr` that's null while off, like `Stats2D` and `CapReads`. It hooks in at:
  - `GPU::Snapshot2DScanline` → `NoteLine(job)`;
  - `GPU::Snapshot2DSprites` → `NoteSprites(job)`;
  - `GPU::MakeVRAMFlat_2DBGCoherent` and `MakeVRAMFlat_2DOBJCoherent` → `NoteFlatDirty`, between `DeriveState` and the flat copies. That's where `Stats2D->NoteBGDirty` sits, since nothing else can see those sets once `DeriveState` clears the bits;
  - reset and load: `GPU::Reset`, `GPU::DoSavestate`, `InitFramebuffers`.
- **GL, only inside the Blit (`GLCompositor::RenderFrame`):** the uploads, the OBJ pass and the composite pass. They follow `LateBlit` and `BlitAfterReadback` for free, so in capture frames with `blitafterread` they run after the readback, off the line-192 map's critical path.
- **Why the Blit can read the recorder's data:** the Blit always runs by line 215, and the flats only change at the next BG snapshot (line 0) or line 262's OBJ snapshot. A frame stamp guards it: data from another frame sends every line to CPU.
- **Palette and OAM** are never read live at the Blit, since games write them in VBlank before a line-215 Blit. The recorder keeps its own copies, driven by `GPU::PaletteDirty` and `OAMDirty`, which are set in `WritePalette`/`WriteOAM` and read by nothing else today.

### Per-line state

- **Where:** a new block in the accelerated framebuffer after the wing planes (`GPU2D::AccelShadowStateStart`, at today's `AccelFramebufferWords`), 32 words a line, 24 KB per screen buffer. The recorder writes it into `job.Framebuffer[num]`. Like the wing planes it swaps with POWCNT, belongs to the composited frame with threaded 2D and the Blit after the readback, and is zeroed by `BlankFrame` and `InitFramebuffers`. `LogHash2D` only hashes the 192 lines, so HASH2D is unaffected.
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
- **Base upload:** `glTexSubImage2D` per run of pages in the union of the dirty sets since the last upload. The sets come from the `NoteFlatDirty` hook ORing the derived bits, so there's no second tracker. The source is the flats, which hold the state as of the frame's last line. A generation counter forces a full 1.4 MB upload after a reset, a load, a renderer switch or a new compositor.
- **Page log (the mid-frame rule):** when a page is dirty at line L > 0 and its bytes really changed (a memcmp against a copy taken before the flat copy), the old page is kept as "valid for lines [previous change, L)". Palette pages (4 × 512 bytes) go through the same log from the recorder's copy.
  - At the Blit the log goes to `MemTex` rows, plus `PageLogTex` (`R32UI`, one row per changed page, 192 entries, each a history row or "base"). Only lines flagged has-page-log pay for the lookup in the shader.
  - Cap: 512 pages (256 KB) per frame, to be confirmed by phase 0. On overflow at line L, lines 0..L−1 go to CPU, since their version of that page wasn't kept. Lines from L on are still right.
- **OAM never goes to the GPU.** The recorder turns each sprite line's OAM and OBJ state into sprite instances (OAM index, row range, decoded attributes, affine parameters). It splits a sprite's row range wherever OAM or the OBJ state that matters (DispCnt OBJ bits, OBJEnable, the screen half) changes. They're uploaded as a vertex buffer with `glVertexAttribDivisor`, not a UBO (the 16 KB limit). Cap: 2048 instances; lines past that go to CPU.

### Passes and targets

1. **OBJ pass,** set up beside `PrepareFilter2D`'s targets, before the output is bound.
   - It draws instance quads into `ObjTex` (`RG32UI`, ViewWidth × 384 at ObjScale) with a 16-bit depth buffer.
   - R holds the CPU's `OBJLine` pixel format exactly: color or palette index, 0x8000/0x1000 for direct and standard palette, priority 0x30000, opaque 0x40000, mosaic 0x100000, and the flag byte.
   - Depth is `prio*128 + index` with `GL_LESS`, which is the "lowest (priority, index) wins" rule of `SoftRenderer::DrawSpritePixel`. Upstream's `2DSpriteVS.glsl` uses the same key.
   - OBJ-window sprites are a second draw into G only (`glColorMaski`, depth off). `glInvalidateFramebuffer` on the depth afterwards.
   - ObjScale is 1 until increment 3. From then it's the 3D scale, capped at 4, on frames with a visible rotated sprite.
2. **Composite pass,** a new program on the same output FBO, right after the compositor's `glDrawArrays` and before `CopyHeldWings` and `RunHiresCapture`.
   - Quads cover only the row runs drawn this frame.
   - Each fragment works from highp `gl_FragCoord` (as `FilterSubPixel` does). It evaluates the layers in the CPU's priority order (`DrawScanlineBGMode<N>`) and pushes a 3-entry stack the way `DrawPixel_Accel` does, including the backdrop, the 3D placeholder 0x40000000 and sprites through the `InterleaveSprites` palette lookup.
   - Then it builds the planes: unit A takes the 4-way branch of `DrawScanline_BGOBJ`'s accelerated path, and unit B takes `ColorComposite` with plane 1 = 0 and plane 2 = 0x07000000.
   - Then it runs the existing `CompositePixel`, with the 3D fetched at the fragment's real position (`Fetch3D`, the WIDE variant for wings) and master brightness from the control word. Blending with 3D, fades and brightness stay exact by construction.
   - Text BGs and normal sprites use the native X (`floor(sub/scale)`), so they're pixel-identical to CPU at any scale. Affine BGs use integer sub-pixel coordinates (`ref*S + subX*A + fy*B`, floor-divided by `256*S`), which equals the CPU at S = 1.
3. **Check variant** (`SHADOW2D_PLANES` define). The same function writes planes 0–2 as words into a 256×384 `RGBA32UI` target at 1x, at any user scale. Every 30 frames it's read back with `glReadPixels` and compared word for word with `gpu.Framebuffer[backbuf]` on drawn lines. That's the only synchronous readback, and only in check mode.
   - Engine A's plane 1 under the backdrop is a leftover of the previous pass unless `EngineBNoHold` clears it (see the `DrawScanline_BGOBJ` comment). The shadow writes 0, so run the check in debug builds, where `EngineBNoHold` is on.

### GPU or CPU per line

- **Feature-eligible** is decided by the recorder from the line's state, for the increments that have landed. The base rules: display mode 1, enabled, not forced blank, VCount == Line. Increment 1 adds:
  - text BGs only;
  - no windows (`DispCnt & 0xE000`);
  - `BlendCnt & 0x3FC0 == 0`, so `ColorComposite` always returns val1;
  - no BG or OBJ mosaic, and no rotated sprite on the line.
- **Drawn** is decided at the Blit, in the compositor's uploaded copy of word 0 (bit 31). A line is drawn if it's feature-eligible, the page log covers it, and it isn't a hi-res display line (`HiresDisplayFlag` after `PrepareHiresDisplay`), held or white. Then the mode's policy applies:
  - `1`: only lines that gain something: scale 2 or more with a rotated BG or sprite, or 2D the wide config sends into the wings;
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

- **Units:** the compositor uses 0 ScreenTex, 1 3D, 2 HeldWings, 3 HiresBank, 4–6 the capture pass, 7 `MakeHiresTexture`'s scratch binding, 8 HiresStream, 9 FilterMap and 10 WingPlanes. The shadow programs draw with those bindings in place and add 11 `MemTex`, 12 `LineStateTex`, 13 `ObjTex` and 14 `PageLogTex`. 15 stays spare.
- **Mobile cost:** one extra render target switch per frame (the OBJ pass) and no per-BG layer targets (upstream has 22 in `GPU2D_OpenGL.cpp`). Drawn rows are composited twice, once by the normal pass and again by the shadow. Giving the normal pass row runs too is the first optimization if cost bites.

## Increments

Sizes are rough line counts. A phone step is one install-and-test round. The device-free checks for every increment:
- a host build of the core;
- `.claude/shadercheck/check.sh`, with `dump.cpp` updated by hand;
- `tools/swi-harness/wide_check.sh` unchanged with the recorder on;
- HASH2D unchanged.

1. **Text BGs and normal sprites, plus the whole frame.** This builds the recorder, the line block, base uploads, the OBJ pass, the composite and check programs, the `GPU2D` log, and the wide plumbing with a forced debug mode. Until 1b, lines before the frame's last real change are CPU.
   - Files: `GPU_Shadow2D.h/.cpp` (new, about 400), `GPU.cpp/.h` hooks (60), `GPU2D.h` layout (15), `GPU_OpenGL.cpp/.h` (`PrepareShadow2D`, `RunShadow2D`, `CheckShadow2D`, `LogShadow2D`, `FreeShadow2D*`, about 450), `GPU_OpenGL_shaders.h` (about 450 GLSL), reset calls in `GLRenderer`/`ComputeRenderer` next to `ResetFilter2D` (10), app `DebugProperties.cpp/.h` (25). About 1,400 in all.
   - Proof:
     - `check` at 1x and 4x gives 0 mismatches over 100+ checks per game, on OpenGL and compute, on the Fold and the OnePlus. Games: Advance Wars (menu and map), NSMB gameplay, White 2's title and overworld (its hi-res display lines must stay CPU), and Phantom Hourglass (3D on both screens, wing hold).
     - `all` vs `0` at 4x: exact-frame pairs with 0 differing pixels.
     - HASH2D with threaded 2D on and off, `all`, 600 frames: identical.
     - Fold at WideMelon 342: the wings are exact vs off, and `gpu2dwide 1` puts tilemaps and sprites in the wings with the middle exact.
     - The `GPU2D` log's recorder µs and upload KB on the OnePlus at OpenGL 4x.
   - 3 phone steps.
1b. **Page log:** mid-frame memory versions, palette included.
   - Files: `GPU_Shadow2D.cpp` and shaders (about 250), plus a harness `--shadow-replay` (about 150 tool lines). Every 16th line it keeps a full copy of the flats and palette, then at the Blit compares them with what the base plus the log rebuilds for that line.
   - Proof: replay passes from savestates (Golden Sun, Contra 4, Kirby, White 2). `check` gives 0 on scenes with `bgv` changes, and the log shows those lines kept on the GPU.
   - 1 phone step.
2. **Affine BGs, hi-res.** `DrawBG_Affine` and the tiled `DrawBG_Extended` (the kind most rotated games use), including the no-wrap and ext palette cases.
   - Files: shaders and recorder rules, about 230.
   - Proof: `check` at 1x on Advance Wars A (plain affine, some lines without blending), then Metal Slug 7 B, Mario Kart B, Advance Wars B and Kirby B, on the lines without blending or windows. At 4x and 8x, only the rotated layers differ from off (a diff mask), and they look sharp.
   - 2 phone steps.
3. **Affine sprites,** with ObjScale at the 3D scale (capped at 4) on frames that have one.
   - Files: about 200.
   - Proof: Kirby A (`sprrot` up to 4) and Mario Kart B, `check` 0, plus a cost check of `ObjTex` at 8x wide.
   - 1–2 phone steps.
4. **Windows.** A closed form of `CalculateWindowMask`'s scan (carry-in from `Win0Active`/`Win1Active`, x2 checked before x1), the OBJ window from `ObjTex.g`, WINOUT, and the edge patch under WideView or WideCheck.
   - Files: about 150.
   - Proof: Metal Slug 7 B, Kirby B and Advance Wars' dialogs, `check` 0, exact pairs.
   - 1 phone step.
5. **Blending.** The full `ColorComposite`: the four 3D branches, semi-transparent and bitmap-sprite alpha, and the window effect bit 0x20.
   - Files: about 200.
   - Proof: fades, White 2's overworld, Contra 4's Konami fade and Golden Sun A, `check` 0, exact pairs.
   - 1–2 phone steps.
6. **Mosaic.** BG mosaic (latched refs, the X table) and OBJ mosaic with `ApplySpriteMosaicX`'s latch rule (at most 16 fetches back a pixel).
   - Files: about 250.
   - No game in the step 1 set uses it, so first find a scene with a harness scan for `bgmos`/`objmos`. If none turns up, skip it and mosaic stays CPU.
   - 1 phone step.
7. **Bitmap BGs, hi-res.** The bitmap modes of `DrawBG_Extended` and `DrawBG_Large`.
   - Files: about 150.
   - Proof: Golden Sun on both engines (needs 1b and 5), `check` 0, sharp at 4x.
   - 1–2 phone steps.
   - **7b, hi-res capture Phase 2, if it fits.** A bitmap BG whose range resolves to one bank A–D with a valid copy samples `HiresBankTex` (unit 3). Bank validity has to catch writes through the BG, OBJ and ARM7 mappings too ([hires-capture.md](hires-capture.md)). About 250 lines. First log White 2's battle registers to confirm it's a bitmap BG.

Total: about 3,200 lines, inside the old 2,500–3,500 estimate.

### Increment 1 results (11-09)

Branch `gpu-2d-shadow` in both repos, from `widemelon`, uncommitted. The open decisions went with the defaults: the order is text BGs and sprites, then windows, blending, affine BGs, affine sprites, bitmap BGs, mosaic last; the page log counts physical pages with a 512 cap; mode `1` draws only lines with a real rotation or scale or 2D in the wings; drawn lines lose MMPX; compute is allowed, opt-in.

**What landed** (about 2,700 lines with the harness tools):
- Core: `GPU_Shadow2D.h/.cpp` (recorder), hooks in `GPU.cpp/.h`, `GPU2D.h` (`AccelShadowStateStart`, 32 words a line), `GPU_OpenGL.cpp/.h` and `GPU_OpenGL_shaders.h` (upload, OBJ pass, composite, check, log), reset calls in `GPU3D_OpenGL.cpp` and `GPU3D_Compute.cpp`.
- Harness: `--accel`, `--hash2d`, `--shadow2d N` and `--threaded2d` in `main.cpp`; `SWI_HARNESS_SHADOW2D=on|both` for `wide_check.sh`; `Shadow2DReplay.cpp`, a C++ copy of the OBJ and composite shaders compared with the CPU's planes.
- App: `gpu2d` and `gpu2dwide` in `DebugProperties`.
- `.claude/shadercheck`: 4 new programs.

**Changes from the plan:**
- The composite keeps only the top two stack entries, since the third never reaches the planes without blending. Increment 5 brings it back.
- OBJ-window sprites aren't built or drawn. They can't matter with windows off, and this drops the per-channel `glColorMaski` on RG32UI, which is a driver risk. Increment 4 brings them back.
- `PageLogTex` isn't created yet (1b).
- Also kept on CPU: capture-armed engine A lines and screens-off lines. Mosaic is decided from the mosaic bit alone, whatever the size.
- The upload generation comes from a process-wide counter, so a new recorder can't repeat an old value.
- Hi-res display lines never show the `hires` reason. They're display mode 2 or bitmap BGs, so `dispmode` or `bgtype` catches them first.

**Device-free checks (all pass):**
- Host core build.
- `shadercheck`: all 75 lines ok.
- `wide_check.sh` with the recorder on and off: identical results.
- HASH2D with the recorder on and off over 1,200 frames: identical on 8 games (Mario Kart, Golden Sun cave, Advance Wars, Contra 4, White 2, Phantom Hourglass, NSMB, Kirby), plus threaded 2D on Kirby and White 2.
- Shader replay from the phase 0 states: 6.67 M eligible lines with 0 plane mismatches and 0 stale `MemTex` words. A deliberate BG X+1 gave 2.6 M mismatches, so the compare is live.
- Scripts and logs are in `logs/gpu2d-shadow/` (`host_checks.sh`, `replay_batch.sh`).

**Eligible lines (replay):**
- Kirby: 67% from boot, 100% from the states.
- White 1, SoulSilver and DQ9 boots: 58–67%.
- White 2 overworld: 50%.
- Advance Wars menus: 20%.
- NSMB, Mario Kart, Golden Sun and Phantom Hourglass: 0%, because of blending, BG types or capture. Nearly everything else waits for increments 4 and 5.

**OnePlus 7 Pro, plugged in** (`perf_logs/2026-11-09-shadow2d/oneplus/`):
- **`check`** gave 0 mismatches and 0 failures at 1x and 4x, on OpenGL and on compute:

| Scene | Checks (GL 4x / GL 1x / compute 4x / compute 1x) | Drawn |
|---|---|---|
| Kirby boot, title and demo | 131 / 133 / 129 / 131 | 34% |
| White 2 title | 141 / 142 / 158 / 142 | 27% |
| White 2 overworld | 121 / 121 / 81+77 / 121 | 100% |
| Advance Wars intro | 163 / 164 / 163 / 164 | 65% |
| Phantom Hourglass | 0 | 0% |

  Phantom Hourglass drew nothing, as expected, and looked right.
- **`all` vs `0` at OpenGL 4x** (non-wide):
  - Every pair that landed on the same frame had 0 differing pixels: Advance Wars 6 of 16; the GPU-drawn bottom screen in 16 of 16, 12 of 12 and 12 of 12.
  - The other pairs differed by animation, not row patterns, so the reviewer's mediump texcoord worry didn't show up on the Adreno 640.
- **HASH2D:** `all` with threaded 2D on and with it off, 600 frames of Kirby's boot, matched `0` frame for frame. Mode `0` alone splits between two outcomes from frame 313 on, so it isn't the shadow.
- **WideMelon 342** at OpenGL 4x:
  - `all` with `gpu2dwide 0`: wings identical to `0` on every exact pair (23).
  - `gpu2dwide 1`: policy `1` lines get drawn (Kirby 34%, Advance Wars 73%, White 2 overworld 100%), and the middle stays exact. Advance Wars' intro letters, bars and banner carry into the wings.
  - In Kirby and White 2 the wings looked the same as off. That's worth a look in WideMelon step 4.
  - `check` with `gpu2dwide 1`: 0 mismatches. One compute 4x pair: exact.
- **Cost, OpenGL 4x on Kirby:**
  - The recorder averages 0.39 ms a frame (max 0.56), under the 1 ms stop line.
  - Uploads average 45 KB a frame when drawing (max 104).
  - fps and frame time are within noise of `0` (59.9 fps either way).

**Fold, `check`** (user-run field build `melonade-gpu2d-shadow-inc1-check-2026-11-09.apk`, which defaults to `check`; read from the app's debug logs `melonade_debug_20261109-113706.txt` and `-115336.txt`):
- 0 mismatches and 0 failures in every config, with no crashes or GL errors.
- The settings were harder than the OnePlus runs: "Smooth 2D edges" on, upscaled capture on, threaded 2D on, and the inner screen's wide view (308, or 342 in one run) with `gpu2dwide 0`.

| Scene | Checks (GL 1x / GL 4x / compute 1x / compute 4x) | Drawn |
|---|---|---|
| Advance Wars, wide | 195 / 177 / 170 / 163 | 21–25% |
| Kirby, 256 | 276 / 188 / 214 / 354 | 76–88% |
| White 2 title and overworld, wide | 413 / 388 / 251 / 465 | 52–53% |
| Golden Sun, compute 4x | 11 | 1% |

- The recorder averaged 95–153 µs a frame.
- Phantom Hourglass wasn't run on the Fold; it draws nothing in this increment (OnePlus).

Increment 1 is done.

### New order (11-09)

The replay logs above show the page log would gain almost nothing now: `memchanged` held back lines only in SoulSilver's boot (130–258 of 115,200), while blending kept most lines on CPU in nearly every game. So the user moved blending (increment 5) first. Then windows (4) and the rest. 1b waits until a game needs it, most likely Golden Sun with bitmap BGs (increment 7).

### Increment 5 results (11-09, device-free only)

Uncommitted on `gpu-2d-shadow` in the core, about +360/−40 lines: `GPU_OpenGL_shaders.h` (GLSL copies of `ColorComposite`, `ColorBlend4/5`, `ColorBrightnessUp/Down`; the three-entry stack is back; unit A's planes come from `DrawScanline_BGOBJ`'s four branches; unit B is `ColorComposite(top, second)`, 0, 0x07000000), `GPU_Shadow2D.cpp/.h` (the rule below), `Shadow2DReplay.cpp` (mirrors the shader), and a harness `--bnohold` flag. No new programs, so `GPU_OpenGL.cpp` and `dump.cpp` are unchanged. The window mask is a constant `0xFF` in the shader, so increment 4 only has to compute it per pixel.

- **What still stays on CPU for blending:** unit B lines with an alpha blend that has the backdrop as 1st target, when `EngineBNoHold` is off (release builds, and the harness without `--bnohold`). There B's pass starts with whatever unit A left in plane 1, and the CPU blends the backdrop with that. The review found it; reason `bleftover` (old `R_Blend` number). It fired only in Advance Wars' boot (6.6% of lines).
- **Checks, all pass** (logs in `logs/gpu2d-shadow/inc5/`): host build; shadercheck 75 ok; `wide_check.sh` with the recorder on and off identical; HASH2D on and off identical on the 8 games, plus threaded 2D on Kirby and White 2; replay 0 plane mismatches and 0 stale mem on all 20 scenes, with `EngineBNoHold` both off and on. Deliberate bugs are caught: EVA/EVB swapped (Mario Kart, Contra 4, Phantom Hourglass, DQ9) and EVY+1 on darken (NSMB).
- **Not covered by any scene:** brighten (effect 2), same code shape as darken; and the review's exact leftover case, which the rule covers anyway.

Eligible lines in the replay, increment 1 → 5 (EngineBNoHold on; Advance Wars is 78% with it off):

| Scene | Inc 1 | Inc 5 | Still CPU |
|---|---|---|---|
| Mario Kart state, NSMB state, White 2 state, Kirby states | 0–100% | 100% | – |
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

Reasons are first-match, so window and bgtype now show what blending used to hide. The Mario Kart state is all text BGs with no window, not the rotated race scene from phase 0, so increments 2 and 4 still matter there.

**OnePlus 7 Pro, plugged in** (11-09, `perf_logs/2026-11-09-shadow2d-inc5/oneplus/`, `summary_t5.txt`):
- **`check`:** 0 mismatches and 0 failures in all 32 runs, with no crashes or GL errors. Each cell is checks / drawn %:

| Scene | GL 4x | Compute 4x | GL 1x | Compute 1x |
|---|---|---|---|---|
| NSMB boot, title, cutscene, map | 188 / 78% | 145 / 72% | 201 / 79% | 199 / 79% |
| NSMB 1-1 gameplay | 193 / 62% | – | – | – |
| Mario Kart title, menu, fade, attract demo | 71 / 34% | 41 / 28% | 71 / 35% | 71 / 34% |
| White 2 intro, title, overworld | 236 / 80% | 174 / 74% | 244 / 80% | 244 / 80% |
| Contra 4 Konami fade, title, intro | 184 / 52% | 176 / 52% | 188 / 51% | 186 / 51% |
| Advance Wars intro | 188 / 93% | 171 / 92% | 191 / 93% | 191 / 93% |
| Kirby boot | 131 / 34% | 131 / 34% | 133 / 35% | 131 / 34% |
| Phantom Hourglass | 8 / 5% | 8 / 6% | 8 / 5% | 8 / 5% |

- **Drawn vs increment 1:** White 2's title went from 27% to 93%, Advance Wars from 65% to 93%, NSMB from 0% to 78%, Contra 4 from 0% to 52%. Still on CPU: Mario Kart bgtype 32% and window 31%; Kirby bgtype 45% and window 19%; Contra 4 dispmode 39% and memchanged 8%.
- **`all` vs `0` at GL 4x:** no row patterns. The GPU-drawn bottom screen was exact in every White 2 overworld pair (12) and every NSMB file-select pair (8). The other differences were animation or a different fade step.
- **Cost:** the recorder averages 200–480 µs a frame (the worst second was 730 µs), under the 1 ms line. GL holds 57–60 fps. Compute 4x is GPU-bound as before (about 30 fps on NSMB's map and White 2's overworld).
- `bleftover` can't fire in debug builds, so the phone doesn't test it. That's covered by the harness replay with `EngineBNoHold` off.

The Fold's run is a user-run field build that defaults to `check` (`melonade-gpu2d-shadow-inc5-check-2026-11-09.apk` in the NAS drop folder). Then commit and move on to windows (increment 4).

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
- **Compute** runs the same code, since `ComputeRenderer::Blit` uses the same `GLCompositor`. It's GPU-bound on the OnePlus, so it stays opt-in there, with its cost noted in the plan doc.

## Branch

`widemelon`, or a `gpu-2d-shadow` branch cut from it in both repos. `gpu-2d` is already an ancestor of `widemelon`, and the design builds on WideMelon's pieces: the block after `AccelWingPlanesStart`, `WingPixel` and `WingPlanesTex`, the hold order and the window edge patch.

## Risks and when to stop

- **Recorder cost on the emulator thread.** The OnePlus at OpenGL is CPU-limited. Stop and rethink if the recorder plus uploads add more than about 1 ms a frame in mode `1`. Off, it has to cost nothing (a null pointer).
- **GPU cost.** Stop if mode `1` drops the Fold below 60 at OpenGL 4x in Metal Slug 7 or Mario Kart, or costs the OnePlus more than the filter does (3.8 Mclk a frame). Removing the double composite of drawn rows comes first.
- **Exactness.** An Adreno integer quirk that a phone step or two can't fix in `check` keeps that feature on CPU. A mismatch the recorder can't explain (state not captured) means a stricter line rule, never "close enough".
- **Golden Sun.** If phase 0 shows its gameplay really changes more than the cap per frame, the page log doesn't save it, and bands or a CPU-assisted design are needed before increment 7.
- **Memory.** `ObjTex` at 8x and 342 wide is about 34 MB, hence the ObjScale cap of 4.
- **Filter.** With "Smooth 2D edges" on, drawn lines lose MMPX on their non-rotated layers.
- **Out of scope, these lines stay CPU:** VCount writes (lines from there on), display modes 0, 2 and 3, the FIFO, and capture.

## Open decisions (user)

1. **Phase 0's route to gameplay:** gameplay savestates for a headless `--stats2d` harness run, playing a debug build with `gpu2dstats` on, or the AVD with a throwaway `.sav`.
2. **Increment order:** most rotated scenes have blending on, and Metal Slug 7 has windows, so affine alone mostly shows up only in `check`. Move windows and blending ahead of affine or right after it, and bitmap BGs ahead of mosaic, since Golden Sun uses rotated bitmaps and no game in the set used mosaic?
3. **Mid-frame rule:** the page log with a 256 KB cap and CPU for the lines before an overflow, instead of bands or a per-frame fallback. Confirm, or pick the cap after phase 0.
4. **Mode `1` policy:** draw only lines that gain something, or every eligible line?
5. **Filter interplay:** accept losing MMPX on non-rotated layers of drawn lines for now, or remap native-resolution layers through the filter map, which costs more?
6. **Compute and the user setting:** allow it on compute (opt-in) or hide it there, and the setting's name and when it appears.
7. **Branch:** `widemelon` directly, or `gpu-2d-shadow` cut from it.

Code to read first: `GPU.cpp` (`Snapshot2DScanline`, `Snapshot2DSprites`, `MakeVRAMFlat_2DBGCoherent`/`2DOBJCoherent`, `StartScanline` line 192, `StartHBlank` line 215), `GPU_OpenGL.cpp` (`GLCompositor::RenderFrame`, the `PrepareFilter2D` pattern), `GPU_OpenGL_shaders.h` (`kCompositePixelFS`, `WingPixel`, `Fetch3D`, `FilterSubPixel`), `GPU2D.h` (`AccelWingPlanesStart`, `LineJob`, `UnitState`), `GPU2D_Soft.cpp` (the reference: `DrawScanline_BGOBJ`, `DrawSpritePixel`, `InterleaveSprites`, `DrawBG_*`), `GPU_2DStats.h` and `tools/swi-harness/main.cpp` (phase 0).
