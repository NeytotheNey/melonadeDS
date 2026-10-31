# Plan: borrow from upstream's blackmagic3, don't port it

## Context

Upstream melonDS merged "blackmagic3" (PR #2553, 172 commits) on 2026-01-31. It is mainly a **new OpenGL 2D renderer** (`GPU2D_OpenGL.cpp`, ~1.9k lines plus ~1k lines of 2D GLSL) and a restructure where one `Renderer` object owns both 2D units and the 3D renderer. On top of that it adds hi-res display capture, 3D clear bitmap support, some 2D accuracy fixes, a more accurate VCount state machine and POWCNT1 bit 0.

We compared upstream master (2026-10-27) against our core (`melonDS-android-lib`, branch `melonade`: melonDS 1.1 + rafaelvcaetano's Android changes + our ~1.7k lines across GPU2D, GPU3D_OpenGL, GPU3D_Compute and GPU_OpenGL). Paths below marked `U/` are upstream, `O/` are ours.

**Decision: don't port. Borrow.** The reasons:

- **The hi-res capture is not separable from the GL 2D renderer.** Upstream's captures live in GL texture arrays (`U/GPU_OpenGL.h:97-132`) and are consumed by the GL 2D renderer when a game shows the capture as a BG or OBJ (`U/GPU2D_OpenGL.cpp:1175-1205`). That is the dual-screen-3D case (White 2's battles). With our software 2D, every such frame would need a downscale + synchronous readback before `GPU2D_Soft` reads the bank, which is no better than today's one PBO map at line 192 (`748c5dd`, `c598b27`). Only capture→3D-texture and capture→VRAM-display-mode could gain, at a cost of ~1k lines of porting.
- **A port rewrites exactly our files.** `GPU.SetRenderer3D`, `GetRenderer3D()` casts, `Accelerated`, `SetOutputTexture`, `GPU.Framebuffer`/`FrontBuffer` (`MelonInstance.cpp:354-443, 845-881`, `ScreenshotRenderer.cpp:29-31`), the `GLCompositor`, WideMelon end to end, threaded 2D and deferred capture would all go.
- **Nothing upstream builds on GLES as-is.** All `U/OpenGL_shaders/*.glsl` are `#version 140`, with implicit int→float conversions, `glBindFragDataLocation`, `glDrawBuffer`, `glMapBuffer`, `glClearDepth` and `GL_UNSIGNED_SHORT_1_5_5_5_REV` readbacks. The 3D shaders (~450 lines) are mechanical to convert; the 2D ones (~1k lines) are the bulk.
- **It isn't finished.** Open since its first week: #2560 (SnemulDS performance regression), #2559 (renderer switching bugs), #2555 (FMV tearing). Upstream's only GL commit since March is a field-initialisation cleanup (2026-10-27). rafaelvcaetano's Android core hasn't taken it (last commit 2026-05-09, melonDS 1.1).
- **Mobile performance is unknown.** It trades CPU 2D for per-line GPU state and VRAM uploads on a tile-based GPU. Our numbers are measured; theirs on a phone are not.

**Revisit a port when** upstream tags a release containing it *and* rafaelvcaetano rebases the Android core onto that release. At that point the port replaces threaded 2D rather than coexisting with it, so until then threaded 2D stays opt-in and gets no more than `PLAN-threaded-2d-overhead.md` already covers.

What we take instead, in four batches below. Everything in batches 1–3 is independent of the renderer rework and keeps our structure. Batch 4 changes savestates.

## Rules

- Same workflow: coder subagent → fresh reviewer subagent → fixes → build → phone check where noted. One batch is one or a few commits, in the core fork first, then the submodule bump in melonadeDS.
- **Hash check.** Accuracy fixes are *meant* to change the 2D output, so the HASH2D compare is used differently here: run threaded on vs off after each change (must still match each other), and keep a before/after screenshot of the affected scene.
- **Measure on the phone**, same recipe as `PERF-NOTES.md` (fresh start, same spot, 60 s, OpenGL 4x). Any performance item that doesn't move `work_avg_ms` or GPU time stays out.
- **Savestates:** batches 1–3 add no state. Batch 4 bumps `SAVESTATE_MAJOR` (`O/Savestate.h:27`, currently 13) once, for all its fields together, with the rewind and savestate paths retested.
- Keep our fixes on anything we touch: the fog/edge pass detaching the depth-stencil attachment (`O/GPU3D_OpenGL.cpp:1141-1143, 1189`), `glMapBufferRange`, `glClearDepthf`/`glDepthRangef`, and the BGRA swizzles.

## Batch 1: free wins (small, no savestate change)

1. **Green LSB.** Upstream uses palette bit 15 as the 6th green bit (`U/GPU2D_Soft.cpp:316, 364`). Ours has the line commented out in `DrawPixel_Normal` (`O/GPU2D_Soft.cpp:929`) and missing from `DrawPixel_Accel` (`:937-946`) and the backdrop (`:776-779`). Set it in all three, and **mask direct-colour bitmap BGs with `& 0x7FFF`** at the call site (`O/GPU2D_Soft.cpp:1329-1330`; upstream `:724`, sprites `:1001`), otherwise every direct-colour pixel gets a wrong LSB. No framebuffer format change: the soft path still expands 6→8 bits, and the GL compositor already reads 6-bit integers from the 256*3+1 line format. ~5 lines.
2. **OBJ Y mosaic underflow clamp.** `U/GPU2D_Soft.cpp:1096-1104`: after `ypos = (OBJMosaicLine - ypos) & 0xFF`, `if (ypos >= boundheight) ypos = 0;`. Ours (`O/GPU2D_Soft.cpp:1709, 1733`) reads rows far outside the sprite. 2 lines; `boundheight` for rotscale, `height` for normal.
3. **OBJ mosaic counter gating.** Only advance the counter when DISPCNT bit 12 (OBJ enable) is set and the unit is powered (`U/GPU2D.cpp:564, 587-605`; ours `O/GPU2D.cpp:579-594, 665`). 3 lines.
4. **Compute capture framebuffer bug.** `O/GPU3D_Compute.cpp:258/282` create and free `CaptureFrameBuffer` with `glGenBuffers`/`glDeleteBuffers`, then bind it with `glBindFramebuffer` (`:1177`). On GLES that is `GL_INVALID_OPERATION`, and the read goes to whatever framebuffer was bound. Use `glGenFramebuffers`/`glDeleteFramebuffers`. (Inherited from the Android base, `431ab4b`.)
5. **8-bit register writes.** Byte writes to DISPSTAT/VCOUNT fall into `GPU2D_A.Write8` and are dropped (`O/NDS.cpp:3296-3300`); upstream routes them (`U/NDS.cpp:3157-3160`, `U/GPU.cpp:1365-1391`). Same for 8-bit DISPCAPCNT, the display FIFO (`val*0x0101`) and MASTER_BRIGHT with the `0xC01F` mask (`U/GPU.cpp:408-445`; ours drops 0x6C/0x6D in `Unit::Write8` and stores MasterBrightness unmasked, `O/GPU2D.cpp:408`). ~30 lines.
6. **POWCNT1 bit 0 (LCD enable) and engine-A-off output.** Upstream latches `ScreensEnabled` at frame start and fills each line black when clear (`U/GPU.cpp:1130`, `U/GPU_Soft.cpp:143-157`); ours only logs (`O/GPU.cpp:902`). A disabled engine A outputs black, B white, with master brightness and capture still applied (`U/GPU2D_Soft.cpp:121-130`, `U/GPU_Soft.cpp:166-250`); ours renders A from stale registers and forces B to white skipping brightness (`O/GPU2D_Soft.cpp:184-210`). Soft path: a `LineJob` flag plus the fill, ~25 lines. Accel path: force brightness-down 16 in the control word (the shader's `pixel -= (pixel*16+0xF)>>4` gives 0), which avoids a new shader bit. Skip the `ScreensEnabled` savestate field for now; one frame of wrong output after a load is fine.

Phone check: White 2 overworld and battle screenshots before/after (green LSB is visible in gradients), a game with mosaic sprites if one is handy, compute renderer capture scene still correct after item 4.

## Batch 2: GL 3D renderer performance

These are the parts of upstream's GL renderer that help a phone and don't need the rework. Measure each on its own.

1. **Skip identical frames and upload only dirty texture VRAM.** Upstream's GL returns early when the texcache has nothing new and `RenderFrameIdentical` is set (`U/GPU3D_OpenGL.cpp:1276-1280`). Our compute already does this (`O/GPU3D_Compute.cpp:654`); our GL always re-renders (`O/GPU3D_OpenGL.cpp:1194ff`) and re-uploads all 512 KB of texture VRAM plus a CPU-converted palette every frame (`:1268-1312`, the "SUCKY" block). Use `VRAMDirty_Texture`/`VRAMDirty_TexPal` to upload only dirty 128 KB banks and palette slots, and skip the whole frame when nothing changed. Don't skip when WideMelon's `ResizeTargets` changed anything. ~60 lines. This is the one most likely to show up in `work_avg_ms` and GPU time, so do it first.
2. **Two shader programs instead of eight.** Upstream has a Z and a W program with `uniform int uRenderMode` (`U/OpenGL_shaders/3DRenderFS.glsl:84, 159-189`); ours compiles 8 (`O/GPU3D_OpenGL.cpp:139-160`, `RenderShader[flags]`). Fewer program switches per frame and fewer compiles on first run (less first-use hitching on Android). ~80 lines.
3. **GL texture cache (decide after measuring 1 and 2).** Upstream's GL now decodes textures through `TexcacheOpenGL` into RGBA8UI array textures and samples with one `texture()` call (`U/GPU3D_OpenGL.cpp:484-576, 777-808`, `3DRenderFS.glsl:125-132`); our fragment shader decodes every format with 2–6 `texelFetch`es plus branching (`O/GPU3D_OpenGL_shaders.h:281-497`). Our compute already uses the same texcache, so the loader exists. Costs: CPU decode when textures change, batches split on texture, more state per batch. If ported, strip the capture-texture branches (`captureinfo`, `Capture128/256`) and use sampler objects like our compute does (`O/GPU3D_Compute.cpp:241-252`) instead of upstream's per-batch `glTexParameteri`. ~300–400 lines plus a render FS rewrite. Only worth it if a GPU profile shows the texture decode as a real fragment cost on the phone after items 1–2.

Phone check: White 2 at the usual spot, OpenGL 4x, before/after per item; also a texture-heavy 3D game. Keep `_summary.txt` from each run.

## Batch 3: 3D clear bitmap

DISP3DCNT bit 14. Our software renderer supports it (`O/GPU3D_Soft.cpp:1657-1689`); GL ignores it (`O/GPU3D_OpenGL.cpp:1327-1360`, `// TODO: clear bitmap` at 1329) and compute ignores it (`O/GPU3D_Compute.cpp:927-935`, DepthBlend `main()` in `O/GPU3D_Compute_shaders.h:1451-1460`). Both get the flat `ClearAttr1` colour instead of the bitmap.

Semantics, same as the software path: colour from texture VRAM slot 2 (0x40000), depth from slot 3 (0x60000); X/Y scroll from ClearAttr2 bits 16–23 / 24–31 wrapping at 256; colour bit 15 → alpha 31 else 0; depth bits 0–14 → `z*0x200+0x1FF`, bit 15 → fog flag; polyID from ClearAttr1.

1. **GL, the cheap way.** Our GL already uploads texture VRAM into `TexMem` (R8UI, 1024x512) every frame (after Batch 2 item 1, only dirty banks), so slots 2 and 3 are already on the GPU. Add a `ClearShaderBitmap` whose fragment shader `texelFetch`es the 2+2 bytes from `TexMem` at `x=(fragX/scale+xoff)&255`, `y=(fragY/scale+yoff)&255`, writes colour `/vec4(63,63,63,31)`, `gl_FragDepth = depth24/2^24`, polyID to `oAttr.r` and the fog bit to `oAttr.b`. Draw it with the same full-screen quad and depth/stencil state as the plain clear. No new textures or uploads. ~60–80 lines, versus upstream's two 256x256 textures and CPU conversion (`U/GPU3D_OpenGL.cpp:109-110, 227-243, 1276-1325, 1406-1428`). WideMelon: wrap at 256 across the wide view.
   Known gap, also in upstream: the translucent-against-zero-alpha pass tests `RenderClearAttr1` alpha, not per-pixel bitmap alpha (`U/GPU3D_OpenGL.cpp:984`). Accept it.
2. **Compute, upstream's way.** Change `Texcache::Update` to also report dirty slots 2/3 as colour/depth bits (`U/GPU3D_Texcache.h:95-124`; compute is our only texcache user). Add `ClearBitmapOffset` to `MetaUniform` (`U/GPU3D_Compute.h:203-204`; the std140 offset is fine), two `GL_R32UI` 256x256 textures with REPEAT (`U/GPU3D_Compute.cpp:229-247`), the upload loop (`:642-686`), and the `glBindSampler(0/1, 0)` before DepthBlend (`:1193-1198`) so the rasteriser's samplers don't override REPEAT. Shader: ~15 lines in DepthBlend `main()` (`U/GPU3D_Compute_shaders.h:1478-1494`) with ES fixes: `(1U<<14)`, `1.0/float(ScreenWidth)`, and add `precision highp usampler2D;` to our header (`O/GPU3D_Compute.cpp:44-49`). ~90 lines.
3. **Finding a test game.** Add a one-shot debug log when bit 14 is set (`GPU3D::Write32` DISP3DCNT path) and play through the library; compare the software renderer's output with GL/compute in that scene.

## Batch 4: 2D accuracy that needs a savestate bump

Do these together, one `SAVESTATE_MAJOR` bump. Each fits our snapshot model: compute the new per-line state in `Unit::PreDrawScanline`, put it in `UnitState` so the job sees it, and keep the renderer a function of (snapshot, memory).

1. **DISPCNT enable latency and forced blank** (`U/GPU2D.cpp:566-573`, state `DispCntLatch[3]`, `LayerEnable`, `OBJEnable`, `ForcedBlank` in `U/GPU2D.h:71-74`). Turning layers on or forced blank off takes 2 lines (OBJ: 1 line); turning off is immediate. Ours reads `DispCnt` directly (`O/GPU2D_Soft.cpp:654-693, 764, 1653`). Sprites must use the value as of the sprite snapshot. ~30 lines. With it: counters keep running during forced blank, and affine refs advance whenever `LayerEnable` has the BG bit (`U/GPU2D.cpp:638-655`); ours skips both (`O/GPU2D.cpp:626-663`).
2. **Affine/extended/large BG vertical mosaic.** Upstream latches the reference point at the start of each mosaic row (`BGMosaicLatch`, `U/GPU2D.cpp:578-585`) while the running ref keeps adding rotB/rotD. Ours subtracts `BGMosaicY*rotB` from the current ref (`O/GPU2D_Soft.cpp:1176-1179, 1267-1270, 1504-1507`), which is wrong when rotB/rotD or the refs change mid-row (HDMA effects). Split into running ref, reload value and the snapshotted internal; latch in `PreDrawScanline`; delete the three subtract blocks. ~40 lines. Take **text BG vertical mosaic** with it: `yoff = BGYPos + BGMosaicLine` with a latched VCount (`U/GPU2D_Soft.cpp:397-403`, counter `U/GPU2D.cpp:612-636` using `==` and `&0xF`; ours `O/GPU2D_Soft.cpp:990-997`, `O/GPU2D.cpp:656-662`). ~10 lines.
3. **Sprite X mosaic and the OAM-order pixel model.** Upstream draws sprites in OAM order 0..127 and an opaque pixel replaces the old one only if that is transparent or strictly lower priority; transparent pixels over empty space record IsSprite/Mosaic/priority (`U/GPU2D_Soft.cpp:1065, 1117-1142`). `ApplySpriteMosaicX` then also latches when the current or latched pixel is non-mosaic or the BG priority is lower (`:938-982`). Ours draws back to front by priority group (`O/GPU2D_Soft.cpp:1672-1674`) with no priority rule in `ApplySpriteMosaicX` (`:1559-1581`). Bit layout already matches (opaque 0x40000, IsSprite 0x80000, Mosaic 0x100000; fix the wrong comment at `:1556`). Contained in `DrawSprites`, `DrawSprite_*` and `ApplySpriteMosaicX`. ~80–100 lines.

Phone check: HASH2D threaded on vs off still matches; savestate save/load and rewind across the bump; before/after screenshots in a mosaic-heavy scene.

## Parked

- **VCount state machine** (`U/GPU.cpp:1066-1079, 1179-1182, 1217-1338`): 9-bit wrap with the frame ending only at 262/511, increment-then-override ordering, edge-triggered VBlank/VMatch IRQs via `SetDispStatIRQ`, capture armed at VCount 0/192, register reset at the 262 HBlank. Nothing depends on the rework, but it is a hand merge into our `StartScanline`/`StartHBlank`, which also carry `Sync2D`, `Blit`, `VCount144`, `Flush2DDeferred` and `VBlankEnd` (`O/GPU.cpp:948-1007, 1534-1642`), plus savestate fields. Only `fix vcount writing causing hangs (#2710)` suggests a visible payoff. Do it only for a game that needs it.
- **Hi-res display capture.** See Context. Revisit with the port.
- **GL 2D renderer / full port.** See Context for the trigger.

## Verification

- Build per `HANDOFF.md`, install over wireless adb.
- Per batch: the phone checks listed above, plus the standing set: White 2 capture scene at OpenGL 4x holds 60 fps (`c598b27` baseline), renderer switch soft↔GL↔compute mid-game, fold/unfold, screenshot, savestate and rewind.
- Perf: `_summary.txt` and simpleperf at the usual spot for Batch 2, threaded 2D on and off, compared against the 10-28 clean A/B.
