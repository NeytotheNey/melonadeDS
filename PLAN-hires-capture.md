# Plan: hi-res display capture for VRAM display mode, on our software 2D

## Status (2026-11-01)

This is a plan only, with no code yet. It was written against core `melonade` at `1bd01df` and app `material3`. Paths marked `O/` are our core's `src/`, `U/` is desktop melonDS at `C:\Dev\melonDS` (`src/`), and `WA/` is WatermelonDS's app (`C:\Dev\WatermelonDS\app\src\main\cpp`). Phase 1 is the White 2 title screen. Phase 2 (bitmap BGs) starts only after Phase 1 has passed on the phone and a register log of the target scene has been taken.

## Context

**What happens on the White 2 title screen.** This was measured on the emulator with temporary register logging on 2026-11-01. DISPCAPCNT `C033040C` blends engine A's BG+3D output (EVA 12) with bank D (EVB 4) and writes the result back to bank D, 256x192, every frame. Engine A shows bank D in display mode 2 (DISPCNT `001E0918`). Engine A is on the bottom screen (POWCNT1 `020F`) and moves to the top after ~30 s idle (`820F`). The intro scenes before it use no capture. The native path does this correctly:
- In mode 2, `DrawScanline` copies bank D into the line (`O/GPU2D_Soft.cpp:236-262`).
- `DoCapture` composites BG+3D on the CPU from the native 3D readback (`:452-511`, the "TODO: handle 3D" at `:443`).

But the compositor only ever sees the 256-wide VRAM pixels. Its mode-2 path is just "pixel, then master brightness" (`O/GPU_OpenGL_shaders.h:160-258`). So Kyurem stays native at any scale, while WatermelonDS's Vulkan build shows it sharp.

**The blackmagic3 claim, checked.** `PLAN-blackmagic3.md:28` says that with our software 2D, only capture→3D-texture and capture→VRAM display could gain.
- The VRAM-display half holds. Mode-2 pixels are drawn only by the compositor, which can take them from a hi-res copy instead.
- The bitmap-BG half was argued from upstream's model, and it doesn't carry over. Upstream's capture lives only on the GPU (`U/GPU.cpp:1556-1633`, block flags plus `SyncVRAMCapture`), so software 2D would need a readback before reading the bank.
- In this plan **native capture stays authoritative and unchanged**, and the hi-res image is only a shadow copy. Software 2D keeps reading native VRAM, so nothing needs a readback.
- What the bitmap-BG case needs instead is a per-pixel source tag from `GPU2D_Soft` plus a compositor lookup. That is what WatermelonDS does on its software 2D: `WA/renderer/VulkanFaithfulShader.comp:1452-1475` reads `sourceX/sourceY/slot` per pixel.
- It fits our format without widening it. For pixels with no 3D involvement (compmode 7, every engine B pixel), plane 1 is never read (shader `:169-237`). It isn't always 0: the 3D-on-bottom path writes `ColorComposite` there with effect 7 (`O/GPU2D_Soft.cpp:965-977`), so a tag must overwrite it. So bitmap BGs are possible later (Phase 2) and not structurally parked.

**References, not ports.**
- Upstream's validity rule is the model for ours: a capture at a different offset/size drops the old one, and a CPU write drops the whole capture (`U/GPU.cpp:1556-1633`).
- WatermelonDS's per-line recipe (`WA/renderer/VulkanOutput.cpp:1741`, `resolveCaptureRecipe` at `VulkanFaithfulShader.comp:1024`) is the same three-plane composite we already have in `BGOBJLine`. That confirms the source-A approach.
- Its lineage, epochs and the Empty/PendingSeal/Certified slots (`VulkanOutput.cpp:1162, 3109`) are what we replace with one coarse rectangle per bank. `PLAN-vulkan.md:34, 172` stands: no 2D rewrite, no lineage.

## Decision

1. The GL compositor keeps one hi-res image per capture bank (A–D, the only capture targets).
2. At line 192, right after it draws the frame, it runs a small "capture pass": it redoes that frame's captures at 3D scale from the 2D planes the CPU recorded, the hi-res 3D image and the previous hi-res copy.
3. Mode-2 lines whose bank copy is valid sample it instead of the native line.
4. Any doubt means native. Native capture, the CPU's view of VRAM, savestates and the software renderer are untouched.

## Design

**Where the image lives.** The compositor (`O/GPU_OpenGL.h`) gets `HiresBank[4]` plus one scratch texture:
- Each is `GL_R16UI`, 256S x 256S, holding the same BGR555 + alpha-bit word as VRAM.
- Rows are bank rows (the whole 128 KB bank is 256 rows of 256), so capture offsets 0–3 (64-row steps) map directly.
- Textures are allocated lazily the first time a bank gets a hi-res capture, and freed on a scale change (`SetRenderSettings`, `O/GPU_OpenGL.cpp:281-310`). A view-width-only change keeps them.
- Separate textures, not an array: the pass renders into the scratch texture and swaps handles. Feedback captures (source B = destination bank) read the old texture and write the new one with no copy.
- Integer texels, so the pass can use exactly `DoCapture`'s 5-bit math. At 1x the result should match native bit for bit (see Verification).

**Bank state, CPU side, in the compositor.** `{Valid, RowStart, Rows}` per bank:
- A pass sets it to exactly the captured rectangle.
- Anything outside that rectangle counts as not hi-res. This is upstream's rule, with no copying of old rows.

**Source A when engine A shows mode 2.** `DrawScanline` always runs `DrawScanline_BGOBJ` first (`O/GPU2D_Soft.cpp:216-217`). So the three accelerated planes (val1/val2/val3) exist on every line whatever the display mode, even though in mode 2 they never reach the framebuffer.
- In `DoCapture`, when `CurJob->Accelerated && CurJob->HiresCapture`, copy `BGOBJLine[0..767]` into a per-frame **capture record** *before* the CPU composite overwrites plane 0 (`:509`).
- Per line, also record: `CaptureCnt`, `CurJob->RenderXPos`, the source B bank and row, and the 256 native source B words as `DoCapture` reads them (`:514-530`).
- The record lives in `GPU` (public, emulator thread only; lazily allocated, ~680 KB). Capture lines are always drawn on the emulator thread (`O/GPU.cpp:1153-1156`, `Begin2DJob` `:1363-1389`), so threaded 2D never touches it.
- The pass recomposites each pixel with the compositor's dispmode-1 branch, moved into a shared GLSL function. Its RGB formulas already match `ColorBlend5`/`ColorBlend4`/brightness up/down (`O/GPU2D_Soft.h:77-128`).
- **Alpha is not the same.** The shader runs alpha through the blend maths, while the CPU functions return `0xFF000000` (opaque). So the shader can give alpha 0 where the CPU is opaque (compmode 3 fade-down with 3D a=1 and evy≥9, compmode 1 with low EVA/EVB, `O/GPU_OpenGL_shaders.h:186-221`). That would drop the captured alpha bit, and the next frame's `aB` would remove the B term from the trail (`O/GPU2D_Soft.cpp:610-614`). The shared function returns a separate opaque flag with the CPU's rules, and the capture uses that.
- It fetches 3D with the compositor's X scroll and WideMelon centre offset (`Fetch3D`, `O/GPU_OpenGL_shaders.h:75-88`). The capture program bounds-checks the fetch and returns 0 outside the texture, like the CPU (`O/GPU3D.cpp:2597-2612`). The non-wide `Fetch3D` has no such check (`:84-86`), and a 3D X scroll would read past the edge.
- 3D colour decode in the capture program: `ivec4(round(texel * 255.0)) >> ivec4(2,2,2,3)`. That is what both readbacks do (`O/GPU3D_OpenGL.cpp:1795-1803` shifts by 2 and 3; compute's raw `LowResFB`). Classic GL draws polygons in float, so the stored bytes are arbitrary and a `c/63` round trip isn't exact. This stays in the capture program only. Changing the shared `Fetch3D` would change mode-1 output with the setting off.
- Capture source A = 3D only (bit 24) skips the planes.

**Source B.** Per line, the CPU decides at pass time:
- Use the hi-res copy of the source B bank only if all of these hold:
  - the copy is valid over the row read;
  - that bank was not written since the last pass;
  - if source B is the destination bank, the source row equals the destination row.
- The row read is `L` in mode 2 (no offset in mode 2, `:528-529`), otherwise `L + 64*srcoffset`.
- Otherwise use the recorded native words, upscaled nearest. The FIFO source always uses the recorded words.

So the first frame of the title trail blends hi-res A with native B, and every frame after is fully hi-res.

**Sizes.** 256x64, 256x128 and 256x192 at any offset that doesn't wrap past row 255 are eligible. 128x128 (linear addressing, two lines per bank row) and wrapping rectangles are not in Phase 1. The pass still runs its bookkeeping and marks the destination invalid.

**Validity (when to fall back to native).** `GPU` keeps `u8 HiresCapWrites` (any write to a bank since the last Blit) and `u8 HiresCapWritesInCapture` (written while `GPU2D_A.CaptureLatch`, i.e. during this frame's capture). Both are cleared right after `GPU3D.Blit` (`O/GPU.cpp:1651`). A third mask, `u8 NativeCapturedSincePass`, is set by `DoCapture` for its destination bank on every capture line, including the early return when the destination isn't LCDC (`O/GPU2D_Soft.cpp:437`). Only the pass that consumes that capture's record clears it. A bank with the bit still set is native. The events:

| Event | Effect |
|---|---|
| CPU or DMA write to an LCDC bank | `WriteVRAM_LCDC` sets the bit (`O/GPU.h:189-193`), banks 0–3 only. Every ARM9 path goes through it: `NDS.cpp:2191, 2251`, the DSi 8-bit VRAM write `DSi.cpp:1596` (NDMA goes through `ARM9Write16/32`), the JIT slow path `ARMJIT_Memory.cpp:1378`. VRAM is not fastmem-mapped (`:252-272`). DMA has no direct path, and Lua uses `ARM9Write`. |
| VRAMCNT A–D change | `MapVRAM_AB`/`MapVRAM_CD` set the bit after their `oldcnt == cnt` early return (`O/GPU.cpp:450-453, 511-516`). This covers any write made while the bank was mapped elsewhere. |
| Write during this frame's capture | That frame's capture into the bank is not trusted: destination becomes invalid. Writes in VBlank only invalidate the old copy. |
| Capture ineligible (size, wrap, `Abort3DLine`, `VCount != Line`, destination/offset/size changing between lines, missing lines) | Pass skipped, destination invalid. |
| Record never consumed (a VCount write skipped line 192, so no Blit) | `NativeCapturedSincePass` stays set for that bank, so it stays native until a later pass covers it. Checking only when the next record starts isn't enough: if the next frame doesn't capture, the old hi-res copy would be shown over a bank native capture already overwrote. |
| Savestate load or rewind | `GPU::DoSavestate` sets all four bits; `SavestateLoaded()` (`O/GPU.cpp:337`, `O/GPU3D_OpenGL.cpp:1777`, `O/GPU3D_Compute.cpp:417`) also resets the bank state next to `ResetWingHold`. |
| Reset, `Stop`, renderer switch, scale change | Bank state dropped (a renderer switch builds a new compositor anyway). |
| Setting toggled | All bits set. |

CPU reads still get native data (`ReadVRAM_LCDC`, `O/GPU.h:119-124`), so games that read captures back are unaffected.

**Display mode 2 reading it.** Control-word bits 5–7 are the only free ones: master brightness is masked to `0xC01F` (`O/GPU2D.cpp:344-347, 460`), 8–12 are the wing EVY, 13 is the compositor's wing hold, 14–22 are taken, 23–31 are the 3D X. In `DrawScanline`'s accelerated block (`O/GPU2D_Soft.cpp:297-303`):
- For `dispmode == 2 && CurJob->HiresCapture && LCDC-mapped && VCount == Line`, the 2D code sets bits 5–6 = bank and bit 7 = candidate. `FillScreensOff` (`:403`) and forced blank overwrite the whole word, so they stay black or white.
- Bits 5–7 can arrive stale: `DoSavestate` loads MasterBrightness unmasked (`O/GPU2D.cpp:195`), and older states predate the `0xC01F` mask. So mask with `0xC01F` when building the word (`O/GPU2D_Soft.cpp:301`), and have the compositor clear bits 5–7 on every non-candidate line, as it does for bit 13.
- `GLCompositor::RenderFrame` (`O/GPU_OpenGL.cpp:465-529`) already uploads a rewritten copy of the control words for the wing hold (`WingCtrl`, `PrepareWingHold` `:415-430`). It now does so whenever a candidate exists, and clears bit 7 on lines that fail any of these:
  - the bank is valid and covers the row;
  - the bank was not written since the last pass;
  - this frame has no capture into the displayed bank at a row offset other than 0. At offset 0, display line L reads row L before capture line L writes it, so it always sees the previous frame's copy, as native does.
- Phase 1 binds one bank texture (unit 3): the bank of the first candidate line. Other banks stay native.
- Shader: in `main()`, `dispmode == 2 && (mbright.r & 0x80) != 0` → `texelFetch(HiresBankTex, coord)`, unpack 5-bit to 6-bit (`<<1`, as `:247-249`). It then falls into the existing master brightness block. A `uHiresCap` uniform gates it.
- Precision: the non-wide shader is `mediump`, and at 8x fp16 can't hold `fTexcoord * u3DScale` above x=128. Derive the integer coordinate from `gl_FragCoord` instead, and keep the R16UI word as `uint` (0x8000 overflows a mediump int).
- **Wings in mode 2 stay black, as today.** `WingPixel` only shows 3D for bit 18, which is only set in mode 1 (`:322-339`). `PrepareWingHold` only reads bits 18/21/22, so it is unaffected. Wide capture wings are parked.

**Ordering, timeline of frame N** (deferred capture on):
1. Line 215 of N−1: the 3D frame F renders into the hi-res target.
2. Line 0: `PrepareCaptureFrame` reads F back (`O/GPU2D_Soft.cpp:412-428`).
3. Lines 0–191 are held.
4. Line 192: `Sync2D` draws them. Each line reads native bank D (N−1's capture) into the framebuffer, then `DoCapture` writes native bank D and the record (`O/GPU.cpp:1426-1441, 1650`).
5. `Blit`: the compositor draws, sampling `HiresBank[D]` (N−1's hi-res). Then the capture pass reads the record, F's hi-res texture (still bound on unit 1 by `SetupAccelFrame`) and `HiresBank[D]`, renders into scratch and swaps.
6. Line 215: the next 3D render overwrites F *after* the pass in GL command order. The compositor already relies on that ordering for both renderers, compute's image stores included.

The pass must run after the frame's draw (the display shows the pre-capture copy) and before line 215 and the next compositor draw. The end of `RenderFrame` satisfies all three, and it runs only in frames that captured.
- `defercap 0` gives the same per-line order.
- earlyflush, blitflush (`O/GPU.cpp:1655`; the flush now also submits the pass) and capspin (the next frame's readback simply sits behind the pass on the GPU) are unchanged.
- The pass limits itself to the destination rows with a viewport, not a scissor: classic GL calls `glScissor` at `O/GPU3D_OpenGL.cpp:1003` assuming the scissor test is off (it only enables it at `:872/882`). It restores the draw framebuffer like `CopyHeldWings` (`:461-462`) and sets the viewport back explicitly, since `CopyHeldWings` never touches the viewport.
- The frame queue is not involved: bank images are emulation state that persists across frames, not per-slot output.

**Renderers.**
- Classic GL and compute need no changes: the pass sits in the shared compositor and uses `SetupAccelFrame`'s binding (`O/GPU3D_OpenGL.cpp:1810`, `O/GPU3D_Compute.cpp:1541`).
- Software 3D: `job.Accelerated` is false, so no record, no bits, no compositor. That path must stay byte-identical.
- Planned Vulkan: it inherits this free if its AHB target is what `SetupAccelFrame` binds at line 192, it writes `c/63` with `.bgra` (`PLAN-vulkan.md` item 4), and its GL→Vulkan semaphore is taken after `Blit` returns (item 5 already says "after Blit"). Add "White 2 title sharp" to its Phase 2 checklist.
- No Vulkan-specific code here.

**Setting, default, savestates.**
- User setting "Upscaled display capture" under Settings → Video, shown for OpenGL/compute, default **on**. Plumbed like `threaded2dRendering`: `RendererConfiguration.kt:10`, `SharedPreferencesSettingsRepository.kt:117`, `MelonDSAndroidConfiguration.cpp:55,106`, `Configuration.h:89`, applied in `MelonInstance::updateRenderer` near `:1019`.
- Into `GPU.HiresCapture`, snapshotted into `LineJob` in `Start2DJob` (`O/GPU.cpp:1033-1047`), because the 2D thread draws mode-2 lines.
- Debug prop `debug.melonade.hirescap` (`MelonInstance.cpp:71-133` pattern):
  - `0` off;
  - `1` on;
  - `log` adds a once-a-second `HIRESCAP` line (passes, hi-res lines shown, skips by reason);
  - `check` forces the pass at 1x and compares the bank texture with native VRAM.
- At 1x the pass is skipped otherwise.
- Savestates: nothing saved, no `SAVESTATE_MAJOR` bump. After a load, one or two frames show native while the trail rebuilds from native B, which decays by 3/4 each frame.

## Rules

- The same workflow: coder subagent → fresh reviewer → fixes → host build → `.claude/shadercheck/check.sh` (add the capture program and the changed compositor to `gl_dump.cpp`) → phone. Core first, then the submodule bump.
- Native capture, `DoCapture`'s VRAM writes, `GetLine`, deferred capture, early flush, blitflush and capspin are not changed in behaviour. The record copy is the only addition to `DoCapture`.
- The renderer is still a function of (snapshot, memory). The record is output like the framebuffer, written only on the emulator thread. HASH2D threaded on vs off must still match. The new control bits make hashes differ from before the change, but only with the setting on and only in mode 2.
- Anything not covered by the validity rules → native.

## Phase 1: VRAM display mode (~650 lines, then a phone session)

1. **Core bookkeeping** (`O/GPU.h/.cpp`, ~90): `HiresCapture`, the two write masks and their hooks, the record struct with lazy allocation, start-of-frame and unconsumed handling, clearing after `Blit`, savestate and reset.
2. **2D** (`O/GPU2D.h:155-181`, `O/GPU2D_Soft.cpp`, ~60): `LineJob::HiresCapture`, the mode-2 control bits, the record in `DoCapture`, the eligibility flags. Update the bit comments at `O/GPU2D_Soft.cpp:307-314` and `O/GPU_OpenGL.h:88-91`.
3. **Compositor** (`O/GPU_OpenGL.cpp/.h`, `O/GPU_OpenGL_shaders.h`, ~350 + ~150 GLSL):
   - shared composite function;
   - capture program (fullscreen quad with the viewport over the destination rows);
   - the two record textures (planes + 2 control texels per line as RGBA8UI 770x192; source B as R16UI 256x192, only the captured rows uploaded, B skipped when every line uses hi-res B);
   - bank textures, scratch, swap;
   - control-word rewrite merged with `WingCtrl`;
   - the mode-2 shader branch;
   - reset hooks.
4. **App** (~60): the setting, the prop, the switches log line, a `PerformanceLogger`/`DebugLog` field.
5. **`check` mode** (debug builds, ~60): every 30 frames, read back the destination rows (`GL_RGBA_INTEGER`/`GL_UNSIGNED_INT`, which GLES 3.x always allows for R16UI) and compare with `GPU.VRAM[bank]`. Log the mismatch count.

## Phase 2: bitmap BG consumers (dual-screen 3D; ~400 lines; starts after a register log)

First log DISPCAPCNT/DISPCNT/BGxCNT/VRAMCNT in a White 2 battle and in another dual-screen-3D game, as was done for the title. `PLAN-blackmagic3.md:28` calls the battles dual-screen 3D; that isn't verified, nor whether they use a sub BG bitmap or sub OBJ.

If it is a direct-colour bitmap BG:
- In `DrawBG_Extended`, a BG is a candidate when its VRAM resolves to one physical bank A–D for that line, the affine part is identity, and mosaic is off.
- After `ColorComposite`, a pixel whose val1 came from that BG unmodified (colour effect 0, compmode 7) writes a tag into plane 1: bank, src X, src Y.
- The compositor samples the bank image for tagged pixels, then applies master brightness. Blended pixels and sprites over it stay native.
- Validity must then also catch writes through BG/OBJ/ARM7 mappings: set the bank bits in `WriteVRAM_ABG/BBG/AOBJ/BOBJ/ARM7` from the mapping mask.
- No same-frame conflict exists, since a bank can't be both the capture target (LCDC) and a BG.
- Several banks per frame need more than one bound texture, so this uses a 4-sampler if-chain.

## Parked, with triggers

- **Capture → 3D texture** (128x128 captures sampled by polygons): touches both 3D renderers and the planned Vulkan one (upstream's `captureinfo`/`Capture128/256` in its 3D shader). Trigger: a game whose capture texture effect looks clearly worse at 4x.
- **Bitmap OBJ consumers:** Phase 2's tag in the sprite path. Trigger: a dual-screen game using sub OBJ.
- **Wide capture wings in mode 2:** bank images at view width, wings composited 3D-only like `WingPixel`, 3x the memory. Trigger: WideMelon users on a mode-2 3D scene.
- **128-wide and wrapping captures, display of a bank captured this frame at a non-zero offset, more than one bank shown in mode 2 in one frame.** Trigger: the `HIRESCAP` log shows a game hitting one of these.
- **Compositor 3D decode:** `Fetch3D` truncates `c/63*63`, which on paper gives c−1 for some values against the CPU's `>>2`. Unverified; fix it only if the `check` mode or a screenshot shows it.

## Risks

- **Fold performance.** CPU: the record memcpy (~0.6 MB per capture frame) plus ~0.7 MB of `glTexSubImage`, versus ~1.2 MB of ScreenTex already. Watch `work_avg_ms` in the capture scene; skip B uploads once B is hi-res. A cheaper record if it shows: in mode 2, ScreenTex planes 1–2 go unused (`O/GPU.cpp:1491-1493`), so val2/val3 can ride there and the record keeps only val1 plus the B words (~0.2 MB). GPU: one 256S x 192S pass with ~6 fetches per fragment (3.1 M fragments at 8x). Watch GPU busy % and `gpuclk`; the 10-31 numbers show headroom at 4x. The capture readback at the next line 0 now waits behind the pass; check the CAPWAIT log on compute.
- **Memory.** R16UI 256S x 256S is 2 MiB per bank at 4x and 8 MiB at 8x. Typical use is destination + scratch, so 4/16 MiB; the worst case of 5 textures is 40 MiB at 8x (compute already uses ~700 MB).
- **Accuracy.** The game never sees hi-res data. The risk is a stale shadow copy shown on screen after a write path we missed. Mitigations: the coarse invalidation, the `HIRESCAP` reasons, and the 1x `check` mode.

## Verification

- **Host:** core build, 140 unit tests, shadercheck with the new program.
- **Emulator (software renderer only, no GLES 3.2):** White 2 title savestate, HASH2D for 600 frames on the old APK vs the new one, setting on, threaded 2D on and off. They must be identical, because the software path is untouched. Also check that the setting is hidden or ignored for software.
- **Phone, OpenGL then compute:**
  1. White 2 title at 1x (`check` mode: 0 mismatches), 4x and 8x: Kyurem and its trail sharp, including after the 30 s POWCNT1 swap.
  2. White 2 battle and overworld capture scenes: no regression, still 60 fps at 4x.
  3. A game that uses mode 2 without capture (CPU-drawn bank, FMV): stays native, `HIRESCAP` shows no passes.
  4. WideMelon on: centre sharp, wings black as before, wing hold in a dual-screen game unchanged.
  5. Renderer switch soft↔GL↔compute mid-scene; scale change mid-scene.
  6. Savestate save/load and rewind on the title.
  7. Screenshot (frame texture, so it should be sharp).
  8. Setting off: identical to today.
  9. `work_avg_ms` and GPU busy % in the title and battle against the current build, recorded in `PERF-NOTES.md`.

## Couldn't verify

- Whether White 2's title writes bank D or rewrites VRAMCNT_D each frame. The `log` mode will show it; either would keep it native.
- The battle mechanism (see Phase 2).
- The Fetch3D truncation, from shader maths only.

## Review (2026-11-01)

A fresh reviewer checked this plan against core `1bd01df`. Its fixes are folded in above: the alpha/opaque flag, the 3D decode, `NativeCapturedSincePass`, the Fetch3D bounds check, viewport instead of scissor, masking bits 5–7, `gl_FragCoord` coordinates, and reference fixes. It confirmed the ordering (the line-192 draw samples N−1's copy, matching native at offset 0), that the hi-res 3D texture is still bound and unchanged at the pass for both renderers, that deferred capture/earlyflush/blitflush/capspin are unaffected, that every VRAM write goes through `WriteVRAM_*`, that bits 5–7 are free, and that the RGB formulas match the CPU.
