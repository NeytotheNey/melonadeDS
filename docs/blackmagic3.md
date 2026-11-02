# blackmagic3: borrowed, not ported (done)

Upstream melonDS merged "blackmagic3" (PR #2553) on 2026-01-31: mainly a new OpenGL 2D renderer and a restructure where one `Renderer` owns both 2D units and the 3D renderer, plus hi-res display capture, 3D clear bitmap support, 2D accuracy fixes, a VCount state machine and POWCNT1 bit 0. We compared upstream master (`melonDS-emu/melonDS` at `1e654443`, 2026-10-27) with our core and took the parts that don't need the rework. The work is merged: app `material3` (merge `af137c8e`), core `melonade` (`87594b0`, 19 commits, plus `1bd01df` for the follow-ups below). Phone-verified on the Z Fold 8: GL and compute shaders link, HASH2D threaded on/off identical over ~2,890 frames on both, v13 savestates rejected, v14 save/load, renderer switching mid-game. The full plan and handoff, with the per-item file references and phone checklists, are in git history: `git show ac79b584:PLAN-blackmagic3.md` and `ac79b584:HANDOFF-blackmagic3.md`.

## Decision: no port

- The hi-res capture isn't separable from the GL 2D renderer: upstream keeps captures in GL texture arrays that only its GL 2D renderer reads. With our software 2D that needs a readback per frame. (For VRAM display mode this turned out not to hold; see [hires-capture.md](hires-capture.md).)
- A port rewrites exactly our files: the renderer hookup in `MelonInstance.cpp`, the `GLCompositor`, WideMelon end to end, threaded 2D and deferred capture.
- Nothing upstream builds on GLES as is (`#version 140` shaders, desktop-only GL calls), it wasn't finished (#2555, #2559, #2560 open since its first week), and its mobile performance is unknown.

**Reopen when** upstream tags a release containing it *and* rafaelvcaetano rebases the Android core onto that release. A port then replaces threaded 2D instead of living beside it; until then threaded 2D stays opt-in with no more work planned.

## What landed

1. Green LSB (palette bit 15 as the 6th green bit, direct-color bitmaps masked with `0x7FFF`), OBJ Y mosaic clamp and counter gating, 8-bit DISPSTAT/VCOUNT/DISPCAPCNT/FIFO/MASTER_BRIGHT writes, POWCNT1 bit 0 and engine-off output, forced-blank fill of the 3D compositing slices in accelerated mode.
2. The GL renderer skips identical frames and uploads only dirty texture VRAM rows (`TexMemDirtyAll`, `TargetsChanged`); two render programs (Z/W, `#define WBuffer`) with `uniform int uRenderMode` and a per-program `uDepthBias` instead of eight; the software 3D renderer forces a frame after `Reset`.
3. 3D clear bitmap (DISP3DCNT bit 14): GL samples slots 2/3 straight from `TexMem`, compute uploads two 256x256 R32UI textures and fetches them in DepthBlend. Both fold in the WideMelon X origin `(TargetWidth-256)/2` and wrap at 256. A one-shot `Log(Debug)` fires the first time a game sets bit 14.
4. DISPCNT enable latency (2 lines, OBJ 1) and forced blank; affine BG refs latched per mosaic row; sprites drawn in OAM order with the hardware replace rule and the four X-mosaic latch rules. `Unit::PreDrawScanline` runs at every HBlank 0..262.

`SAVESTATE_MAJOR` is 14. `Unit::DoSavestate` adds, in order: `DispCntLatch[3]`, `LayerEnable`, `OBJEnable`, `ForcedBlank` after `DispCnt`; `BGXRefReload`, `BGYRefReload` after `BGYRefInternal`; `BGMosaicLatch` (Bool32), `BGMosaicLine` after `OBJMosaicYMax`. `ScreensEnabled` (POWCNT1 bit 0) is deliberately not saved; it is relatched every frame. Any new per-line Unit state needs the same treatment and a bump.

Invariant: the 2D renderer is a function of (per-line snapshot, memory). Anything it reads that the emulator thread changes per line goes into `UnitState`/`LineJob`, and nothing in `GPU2D_Soft.cpp` writes Unit state, so HASH2D threaded on vs off stays identical.

## Open follow-ups

- **GL texture cache (skipped after measuring).** On 10-31 a clean OpenGL 4x A/B in White 2 showed Batch 2 cut emulator-thread cycles per frame by 16% and GPU cycles by 11–15%, but the GPU stayed at 160 MHz and at most 43% busy, so fragment texture decode can't limit fps. Reopen only if a texture-heavy 3D game pushes the GPU clock up. If it goes ahead: strip upstream's capture-texture branches, use sampler objects like compute, and re-check the shadow-mask sequence in `RenderSceneChunk`, since batches then split on texture. Keep `.bgra`, the ES precision headers, the `layout(location)` outputs, the depth-stencil detach in the fog/edge pass, the `uDepthBias`/`uRenderMode` scheme, and our other GLES fixes (`glMapBufferRange`, `glClearDepthf`/`glDepthRangef`, the BGRA swizzles).
- **Affine ref reload rule.** Upstream reloads the running refs at the HBlank 262 PostDraw and only for layers with the `LayerEnable` bit; ours reloads both at line 0 in `VBlankEnd`. Change it only with a game that enables an affine BG mid-frame without rewriting BGxX/Y.
- **Clear bitmap test game.** No game has logged DISP3DCNT bit 14 yet. When one does, compare software vs GL vs compute there, then remove or demote the log. Kept on purpose, as upstream: GL's translucent-against-zero-alpha pass tests `RenderClearAttr1` alpha, not the per-pixel bitmap alpha.
- **Core `1bd01df`** (VCOUNT 9-bit mask on byte writes, a lone byte write now merging into the current `VCount` instead of the all-ones sentinel, a dead `line > 192` check removed) passed the host build and shader check. It had no targeted phone test, but it is in every build phone-tested since 2026-11-01 (hi-res capture and the polish pass).
- **Parked: the VCount state machine** (9-bit wrap, frame end only at 262/511, edge-triggered VBlank/VMatch IRQs, capture armed at VCount 0/192). It is a hand merge into our `StartScanline`/`StartHBlank`; do it only for a game that needs it (upstream's #2710 vcount-write hang fix is the only known payoff).

Device-free checks for core changes: a host build of the core (`cmake -S melonDS-android-lib -B <dir> -G Ninja -DBUILD_QT_SDL=OFF -DENABLE_JIT=OFF -DENABLE_LTO_RELEASE=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-I$PWD/melonDS-android-lib/src -DMELONDS_GL_HEADER='<frontend/glad/glad.h>'"`, then `ninja -C <dir> core`) and `bash .claude/shadercheck/check.sh [core path]`, which validates every GL and compute shader as GLSL ES 3.20 with `glslangValidator`. The compute shaders and their assembly are read from `GPU3D_Compute.cpp`, but the GL list in `.claude/shadercheck/dump.cpp` is written by hand, so update it if GL shader names or the source assembly in `GLRenderer::BuildRenderShader` change.

On the phone, for GL renderer changes: Phantom Hourglass floor shadows (the depth-equal margin), the White 2 capture scene at 60 fps, and WideMelon on and off. For anything touching 2D, HASH2D threaded on vs off; for savestates, save/load and rewind.
