# Plan: move 2D rendering to its own thread

## Context

Profiles on the phone (Pokémon White 2, OpenGL 4x, 10-27) show the 2D software renderer (`GPU2D::SoftRenderer`) at 26–29% of the emulator thread, about 3.3 ms of the ~11 ms each frame takes. It runs on the emulator thread at every HBlank, so the CPU emulation waits for it.

Two ways to get it off that thread:

- **GPU (rejected for now).** Upstream melonDS merged a new OpenGL 2D renderer on 2026-01-31 (PR #2553, "blackmagic3": 71 files, +8.4k/−4.5k lines, plus a renderer restructure). Our core is melonDS 1.1 plus rafaelvcaetano's Android changes, and doesn't have it. Porting it means a big merge into the Android core, converting ~15 desktop GLSL 1.40 shaders to GLES, and taking on unfinished work (VRAM streaming, a Golden Sun hack). It also has a user report of odd rendering in White 2, and upstream development is paused until at least November. Worth revisiting if rafaelvcaetano merges it into the Android core.
- **Worker thread (this plan).** Self-contained in the core, opt-in, and falls back to today's exact behaviour.

**Expected result:** up to ~3 ms per frame off the emulator thread, less on games that write VRAM, palette or OAM while the screen is drawing. **What it won't do:** cut total CPU use or heat. The same work runs on another core, plus a little sync overhead. White 2 already holds 60 fps with ~5 ms to spare, so this is headroom for heavier games, not something you'd feel today.

## Design

The worker draws a line using a snapshot of that line's registers and reads VRAM/palette/OAM live. Any write to that memory waits for the worker to catch up first (a "barrier"). Everything that changes Unit state stays on the emulator thread, so the worker is just a function of (snapshot, memory).

1. **Line jobs.** At `GPU::StartHBlank(N)` (`src/GPU.cpp:878-919`), instead of calling `DrawScanline`/`DrawSprites`, the emulator thread:
   - makes the VRAM flat copies coherent (the `DeriveState`/`CopyLinearVRAM` calls now at the top of `DrawScanline` and `DrawSprites`, `src/GPU2D_Soft.cpp:115-132, 1604-1613`). `DeriveState` clears the shared `VRAMDirty` bits, so it must stay on this thread;
   - copies both Units' registers into a job (`DispCnt`, `BGCnt`, scroll, affine, windows, mosaic, blend, `MasterBrightness`, `CaptureCnt`, ~200 B each), plus `GPU.VCount` (the BG pass reads it), the 3D X position and the framebuffer pointers (a POWCNT write can swap them mid-frame);
   - pushes the job onto a single-producer/single-consumer ring of 192 entries.

   The worker runs the existing `DrawScanline(N, A/B)` and `DrawSprites(N+1, A/B)` against the snapshot. `OBJLine`/`OBJWindow`/`NumSprites` stay worker-owned, which is fine because jobs run in order.
2. **State the renderer writes back moves to the emulator thread:** the per-line `BGXRefInternal/BGYRefInternal += rotB/rotD` (`GPU2D_Soft.cpp:1178-1179, 1399-1400, 1493-1494`), the BG mosaic Y counter (844-850), the `Win0Active/Win1Active` X carry-over from `CalculateWindowMask` (`GPU2D.cpp:660-704`, which needs a cheap end-of-line helper), and `CaptureLatch`. These are simple per-line updates, like `UpdateMosaicCounters` already is.
3. **Barrier** `GPU2D_Sync()`: a no-op when the worker is idle (an atomic counter check). Otherwise it spins briefly, then waits. Called from:
   - all `WriteVRAM_*` (`src/GPU.h:151-423`), `WritePalette`, `WriteOAM`, and VRAM mapping changes;
   - `SetPowerCnt`;
   - CPU reads of an LCDC bank while capture is latched;
   - `StartScanline(192)`, before `GPU3D.Blit`;
   - `DoSavestate`, `Reset`, `Stop`, `BlankFrame`, `InitFramebuffers`/`SetRenderer3D`.

   DMA writes go through the same functions. During VBlank the worker is idle, so writes there cost nothing.
4. **Fall back to synchronous for the whole frame** (v1, keeps it simple) when capture is latched (`DoCapture` writes VRAM and needs the GL PBO map, which must stay on the GL thread), when display mode 3 (main-memory FIFO) is used, or with the software 3D renderer. Only OpenGL and Compute get threaded 2D.
5. **Thread:** `Platform::Thread_Create` (`src/Platform.h:252-272`, pthreads in `app/src/main/cpp/PlatformAndroid.cpp:323-452`). Name it `GPU2DThread` and add it to the ADPF performance hint session (`app/src/main/cpp/performancehint/`). Don't use `Semaphore_TryWait` with a timeout: it passes a relative time where an absolute one is expected (`PlatformAndroid.cpp:399-406`). Fixing that is a side item.
6. **Setting:** Video → "Threaded 2D rendering", off by default, plumbed like `threadedRendering` (`Configuration.h`, `MelonDSAndroidConfiguration.cpp:155-181`, `MelonInstance.cpp:673`, `RendererConfiguration.kt`, `SharedPreferencesSettingsRepository.kt`). Applies at the next frame boundary.
7. **Perf logger:** add barrier stalls per frame, time spent waiting, and fallback frames to `_summary.txt`.

## Repo logistics (first step, needs your OK)

The core is the `melonDS-android-lib` submodule, which points at rafaelvcaetano's repo (detached HEAD). Before any core commit: create a private fork (e.g. `NeytotheNey/melonDS-android-lib`), add it as a remote, branch from the current commit, and repoint `.gitmodules` in melonadeDS. All core work lives on that branch.

## Steps

1. Submodule fork and branch (above).
2. Move the Unit state updates (Design 2) to the emulator thread, with no threading yet. Output must be byte-identical (checked with step 5's compare mode).
3. Split coherence out of `DrawScanline`/`DrawSprites`, add the job snapshot, and run jobs inline (still synchronous).
4. Add the worker, ring and barriers, plus the fallbacks and setting.
5. **Compare mode** (debug only): render each line both ways and compare per-frame checksums of the 2D framebuffers, logging the first differing line. Emulation is deterministic, so any difference is a bug.
6. Perf logger counters.

Coding goes through a subagent, with a fresh subagent reviewing each step's diff. Each step gets its own commit.

## Verification

- Build per HANDOFF.md and install over wireless adb.
- Run compare mode on White 2 (same spot as the profiles) and a few games with split screens, HDMA effects or display capture: zero mismatches.
- Perf log plus simpleperf at the same White 2 spot, OpenGL 4x, threaded on vs off. Expect `work_avg_ms` down ~2–3 ms, the 2D share gone from `EmulatorThread`, a new `GPU2DThread` around 3 ms, and judder unchanged.
- Savestate save/load, rewind and screenshots while threaded. Pause/resume, renderer switch, and fold/unfold.
