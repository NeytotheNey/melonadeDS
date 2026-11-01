# Handoff: finishing the blackmagic3 borrowings

Read this, then `PLAN-blackmagic3.md`. The plan has the reasoning, the per-item file references and the phone checklist. This file says where things stand and what is left.

## Where things stand (2026-11-01)

- **Update, later the same day.** The two device-free follow-ups below are done in core commit `1bd01df` (VCOUNT 9-bit mask, dead `line > 192` check), submodule bumped here. That commit also fixes a second VCOUNT bug found on the way: a byte write with no write pending merged into the all-ones `NextVCount` sentinel, so a lone low-byte write became `0xFF00 | value`; it now merges into the current `VCount` instead (upstream merges into its persistent write latch; the difference only shows on the first byte of a split write). Host build and `.claude/shadercheck/check.sh` pass; not yet run on the phone. Also: `PERF-NOTES.md` does not exist in either repo or their history, so the Batch 2 A/B against the 10-28 numbers was never written down and the 10-28 numbers themselves live only in the earlier session's output. The next phone measurement should create that file.
- **Merged.** The borrow work (all four batches of the plan) is in `material3` (merge `af137c8e`) and in the core's `melonade` (`87594b0`, 19 commits). The branch `claude/friendly-heisenberg-3eipz2` in both repos is finished; start new work from `material3` / `melonade`, not from it.
- **Verified on the phone** (SM-F971U, Android 17): arm64 build, 140 unit tests, Migration4to5Test, GL and compute shaders link on first start, HASH2D threaded on/off identical over ~2,890 frames on GL and on compute, v13 savestates rejected, v14 save/load works, soft/GL/compute switching mid-game, and the post-rebase retest.
- **Decision on record:** no port of upstream's GL 2D renderer. The hi-res capture only pays off with 2D on the GPU; see the plan's Context for the triggers that reopen that decision.

## What landed, by batch

1. Green LSB, OBJ mosaic clamp and gating, 8-bit DISPSTAT/VCOUNT/DISPCAPCNT/FIFO/MASTER_BRIGHT writes, POWCNT1 bit 0 and engine-off output, forced-blank fill of the 3D compositing slices in accelerated mode.
2. GL renderer skips identical frames and uploads only dirty texture VRAM rows (`TexMemDirtyAll`, `TargetsChanged`); two render programs (Z/W) selected by `#define WBuffer` with `uniform int uRenderMode` and a per-program `uDepthBias`; the software 3D renderer forces a frame after `Reset`; TexMem/TexPalMem are rebound on every rendered frame.
3. 3D clear bitmap (DISP3DCNT bit 14): GL samples slots 2/3 straight from `TexMem`; compute uploads two 256x256 R32UI textures and fetches them in DepthBlend. Both fold the WideMelon X origin `(TargetWidth-256)/2` into the scroll and wrap at 256. A one-shot `Log(Debug)` fires the first time a game sets bit 14.
4. DISPCNT enable latency (2 lines, OBJ 1) and forced blank via `DispCntLatch[3]`/`LayerEnable`/`OBJEnable`/`ForcedBlank`, `Unit::PreDrawScanline` now runs at every HBlank 0..262; affine BG refs latched per mosaic row (`BGXRefReload` / running `BGXRef` / snapshot `BGXRefInternal`, `BGMosaicLatch`, `BGMosaicLine`); sprites drawn in OAM order with the hardware replace rule and the four X-mosaic latch rules. `SAVESTATE_MAJOR` is 14.

## What is left

### A. Batch 2 item 3: GL texture cache (measure first)

The only plan item not done. Upstream's GL renderer decodes textures through `TexcacheOpenGL` into RGBA8UI array textures and samples with one `texture()`; ours decodes every format in the fragment shader with 2–6 `texelFetch`es (`GPU3D_OpenGL_shaders.h`, `FinalColor`/texture functions). Our compute renderer already uses the same texcache, so the loader exists.

Do it only if a GPU profile on the phone shows the fragment texture decode as a real cost now that the 512 KB per-frame upload is gone. Recipe: White 2 usual spot, OpenGL 4x, fresh start, 60 s, `_summary.txt` + simpleperf per `PERF-NOTES.md`, plus a GPU-side view (Android GPU Inspector or the vendor profiler) on a texture-heavy 3D scene. If the fragment stage is not the bound, skip this item and note it in `PERF-NOTES.md`.

If it goes ahead: strip upstream's capture-texture branches (`captureinfo`, `Capture128/256`), use sampler objects like our compute does instead of upstream's per-batch `glTexParameteri`, keep `.bgra`, ES precision headers, `layout(location)` outputs, the depth-stencil detach in the fog/edge pass, and the `uDepthBias`/`uRenderMode` scheme. ~300–400 lines plus a render FS rewrite. Batches split on texture, so re-check the shadow-mask sequence in `RenderSceneChunk` after the change.

The Batch 2 measurement (plan checklist item 2) was not recorded: there is no `PERF-NOTES.md` anywhere. Do it with the recipe above and start the file with it, so the A/B against the 10-28 numbers exists.

### B. Small follow-ups (each under an hour)

- **Affine ref reload rule.** Upstream reloads the running refs at the HBlank 262 PostDraw and only for layers with the `LayerEnable` bit; ours reloads both at line 0 in `VBlankEnd` unconditionally (`GPU2D.cpp`). Visible only to a game that enables an affine BG mid-frame without rewriting BGxX/Y. Change only with a reproducing game.
- ~~**VCOUNT byte write mask.**~~ Done in core `1bd01df` (see the update above). Needs the usual phone smoke test only.
- **Clear bitmap test game.** No game has logged DISP3DCNT bit 14 yet. Keep playing through the library; when one fires, compare software vs GL vs compute in that scene, then remove or demote the log. Known gap kept on purpose (same as upstream): GL's translucent-against-zero-alpha pass tests `RenderClearAttr1` alpha, not the per-pixel bitmap alpha.
- ~~**Dead `if (line > 192) return;`**~~ Removed in core `1bd01df`.

### C. Parked, with triggers

- **VCount state machine** (`U/GPU.cpp` 1066–1079, 1179–1182, 1217–1338): 9-bit wrap with the frame ending only at 262/511, increment-then-override ordering, edge-triggered VBlank/VMatch IRQs, capture armed at VCount 0/192. Hand merge into our `StartScanline`/`StartHBlank`, which also carry `Sync2D`, `Blit`, `VCount144`, `Flush2DDeferred`, `VBlankEnd`. Do it for a game that needs it (upstream's "fix vcount writing causing hangs", #2710, is the only known payoff).
- **GL 2D renderer / hi-res capture / full port.** Reopen when upstream tags a release containing blackmagic3 *and* rafaelvcaetano rebases `melonDS-android-lib` onto it. A port replaces threaded 2D rather than coexisting with it.

## How to work on it

**Repos and branches.** App: `NeytotheNey/melonadeDS`, default branch `material3`. Core: `NeytotheNey/melonds-android-lib`, branch `melonade`, the submodule at `melonDS-android-lib`. Core work goes on a core branch first; the app branch then bumps the submodule pointer (`git update-index --cacheinfo 160000,<sha>,melonDS-android-lib` if the submodule is not checked out). Upstream reference: clone `melonDS-emu/melonDS` master shallowly somewhere outside the repos; the plan's `U/` paths are relative to its `src/`.

**Workflow that worked.** One coder subagent per batch with the plan section as the spec, then a fresh reviewer subagent on the commit range with no conclusions handed to it, then fixes, then the checks below. The reviewers found two real bugs on the way (texture units only rebound when dirty; stale 3D compositing flags in the forced-blank fill), so don't skip the review.

**Invariant to protect.** The 2D renderer is a function of (per-line snapshot, memory). Anything the renderer reads that the emulator thread mutates per line goes into `UnitState`/`LineJob`; nothing in `GPU2D_Soft.cpp` writes Unit state. HASH2D threaded on vs off must stay identical.

**Checks without a device.**
- Host build of the core (no NDK needed; `glad.h` stands in for GLES):
  ```bash
  cmake -S melonDS-android-lib -B /tmp/corebuild -G Ninja -DBUILD_QT_SDL=OFF -DENABLE_JIT=OFF -DENABLE_LTO_RELEASE=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-I$PWD/melonDS-android-lib/src -DMELONDS_GL_HEADER='<frontend/glad/glad.h>'"
  ```
  ```bash
  ninja -C /tmp/corebuild core
  ```
- Shader validation as GLSL ES 3.20 (needs `glslang-tools`): `bash .claude/shadercheck/check.sh [core path]`. It dumps every GL shader (with the `WBuffer` define assembly) and the compute programs (scale 1 defines) and runs `glslangValidator` on each, linking the GL pairs. Update `gl_dump.cpp`/`compute_dump.cpp` if shader names or the source assembly in `GLRenderer::BuildRenderShader` / `ComputeRenderer::CompileShader` change.

**Checks on the device.** Build per the existing recipe, install over wireless adb, then the plan's phone checklist. For anything touching 2D: HASH2D threaded on vs off. For anything touching savestates: save/load and rewind. For GL renderer changes: Phantom Hourglass floor shadows (depth-equal margin), White 2 capture scene at 60 fps, WideMelon on and off.

## Savestate note

`SAVESTATE_MAJOR` 14 added to `Unit::DoSavestate`, in order: `DispCntLatch[3]`, `LayerEnable`, `OBJEnable`, `ForcedBlank` after `DispCnt`; `BGXRefReload`, `BGYRefReload` after `BGYRefInternal`; `BGMosaicLatch` (Bool32), `BGMosaicLine` after `OBJMosaicYMax`. Any new per-line Unit state needs the same treatment and a bump. `ScreensEnabled` (POWCNT1 bit 0) is deliberately not saved; it is relatched every frame.
