# Code notes that aren't obvious

Facts about the code that you can't easily see from reading it.

## Layout editor

- The user plays on the folded cover screen in portrait, with gesture navigation. Touches that start in the side `systemGestures` insets don't grab items, so a side swipe is back.
- `LayoutEditorManagerView` is also used on external displays (`ExternalLayoutEditorPresentation`), so dialogs go through `showInCurrentWindow()` and icon tints are set in code, not `app:tint`.
- Snapping maths is in `LayoutEditorSnapping.kt` (pure functions). The grid origin is the editor's center. Revert goes back to the last Save, not to when the editor opened.
- Per-control opacity isn't applied in game (`RuntimeLayoutView.updateInputs()` overwrites it with the layout-wide opacity), which is why the opacity slider is screens only. A gamepad can't focus the pill or panel; the D-pad nudges instead.

## Video filters

- The frame texture is 256N x 386N at N× upscale: top screen v in [0, 192/386], a 2-row gap (never written), bottom screen [194/386, 1]. `screenBandFunctions()` (`ShaderProgramSource.kt`) clamps samples to the fragment's own screen.
- Every filter uses the real texture size except LCD and Scanlines, which stay on the 256x386 DS grid. (xBR2/Scale2x on the DS grid tore BW2's 3D-drawn overworld sprites at 2x.)
- A `VideoFilter` is a screen shader plus optional pre-passes (`FilterPrePass(source, scale, nativeResolutionOnly)`), run into FBOs by `VideoFilterPipeline`. MMPX = a 2x native-only pre-pass + Sharp. DS screen colors inserts `#define COLOR_CORRECTION` into screen shaders only; screenshots and thumbnails stay uncorrected.
- Phone screenshots are in Display P3 ("Vivid"), so measured colors won't match the color-correction formula exactly.
- Left alone: an external display's `DSRenderer` never frees its GL objects. Scanlines isn't in the settings list (same upstream). Later candidates: xBR-lv2, SMAA, Lottes CRT (use the DS size for `res`).

## Frame pacing

- `app/src/main/cpp/renderer/FramePacing.h` (`DisplayVsyncTracker`, `VsyncFramePacer`), `emulate()` in `MelonDSAndroidJNI.cpp`, `FrameQueue::getPresentFrame`, the Choreographer renderers. 60 Hz lock: `EmulatorActivity.updateDisplayModeLock`.
- Pacing only runs with no fast-forward, a recent vsync, and refresh/60 close to a whole number 1–4; otherwise it falls back to the old timer. Constants (0.5 ms grid offset, 2% clamp, 1/8 gain, P/2 tolerance) are untuned; re-check in a simulation if the logic changes.
- The presenter's waits on the emulator's frames are GPU-side (`eglWaitSyncKHR`), so nothing there makes the emulator thread wait for the GPU. That's the core's job: `GPU::ComputePace` (a fence per Compute render) and `GPU::GLPace` (a fence per OpenGL Blit, `GLRenderer::UpdatePace`/`WaitForPace`) hold the emulator thread while two frames are unfinished, fast-forward included. Capture readback maps already sync, so the pace waits only matter in frames without one.

## Core

- The TWEWY fix (core `2385ca5d`) came from WatermelonDS's core commit `845587e0`; we left out its SPI save rework, extra jump-cycle changes and DSi SCFG changes. Old savestates log "section NDCF not found" once.
- Loading a save state rewrites the game's real `.sav` (`CartRetail::DoSavestate` calls `Platform::WriteNDSSave`). Back up the `.sav` before any test that loads a state.
- Precise depth (`video_precise_depth`, `GPU::PreciseDepth`, desktop `--precise-depth`): Z-buffered vertex depth is `(Z * 0x800000) / W + 0x7FFE00` instead of GBATEK's `((Z * 0x4000) / W + 0x3FFF) * 0x200`, computed once in `GPU3D::SubmitPolygon`, so soft and Compute get it through `Polygon::FinalZ`. OpenGL packs depth into 16 bits plus a shift, which drops up to 8 low bits; with the setting on, `GLRenderer`'s `PackDepth` puts them in spare bits of the attribute word and `kRenderVS` adds them back. Off is bit-exact with the old code on all three renderers. Latched at the polygon RAM swap with the view width (`GPU3D::PreciseDepthOn`) and kept per polygon (`Polygon::PreciseDepth`), not in savestates. Nobody knows if the DS keeps more precision, which is why it's an opt-in enhancement. Compute's DS-style Z interpolation (`InterpolateZZBuffer`, like soft's `Interpolator::InterpolateZ`) drops low bits too, so with the setting on its Z-buffer InterpSpans and Rasterise programs get an exact `InterpolateZPrecise`, inserted only then (sources with it off are unchanged); a toggle recompiles like ComputeOpt. Soft still truncates. See `docs/plan-precise-depth.md`.

## Known issues left alone

- **Races (pre-existing):** quick save/load don't wait for `isThreadReallyPaused` like reset and rewind do; `takeScreenshot` (IO) reads `instance` unlocked and can't simply lock, because it waits on the emulator thread; exiting while a game is still loading can race `stopEmulation`; the exit dialog's 3 s auto-exit could call `stopEmulation` twice and double-delete `androidCameraHandler`; `GetRuntimeAchievements` doesn't take `runtimeLock`; `getRewindWindow` returns raw buffer pointers that outlive the lock.
- **Audio/pacing:** the mic stream (enableMic/disableMic) isn't under `audioOutputMutex`; a FilterAudioStream (only if Oboe resamples) can be freed under its error thread; ADPF hints miss the inner stream when Oboe resamples; paced → timer fallback with the extra buffer on skips a frame; a save write that fails after open has already truncated the old file.
- **Perf logger cosmetics:** `display_refresh_rate_changes` and `_last_hz` ignore paused samples while events don't; a rate is labeled by its first reading (e.g. "120.07"); `display_expected_hold_refreshes` is one average across rates.
- **Save states and FreeBIOS updates:** a state saved while a CPU was inside FreeBIOS can break after a FreeBIOS change that moves code. DQ9's 17:45 state hit an ARM9 abort on 11-02. Fresh states are fine.
- **Threaded 2D** is opt-in (Settings → Display → "Threaded 2D rendering"), hash-identical to sync; keep the 2 ms barrier spin. Never hash-checked with OpenGL on the phone. Its plan's steps 5 and 6 were never done and aren't needed unless the feature comes back.
- A black cover screen after fold was seen once with an intermediate build and never again. If it shows up, grab the debug log around the fold.
- **Decided:** keep upstream's unfinished multiplayer strings. MelonMix and DraStic script compatibility are dropped (see `MODS.md`).
