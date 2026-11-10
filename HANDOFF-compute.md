# Handoff: Compute renderer efficiency

The plan and every result so far are in [docs/plan-compute-efficiency.md](docs/plan-compute-efficiency.md). Read that first. This file is the current state and the next steps. Build, install and phone-connection commands are in `HANDOFF.md`; the short version of the numbers is in `PERF-NOTES.md` under "Compute efficiency on the OnePlus 7 Pro".

## State (11-10)

Branch `compute-efficiency` in the app and in the core, both cut from the `gpu-2d-shadow` tips (app `fa5c5dbf`, core `aa597f11`), because that line already holds WideMelon's changes to Compute. Committed 11-10: the core is at `6cecadbc` and the app commit after `fa5c5dbf` points to it. Check `git log origin/compute-efficiency..compute-efficiency` in both repos to see whether that's pushed. All of it is tested on the OnePlus 7 Pro only. Nothing has run on the Fold.

The goal: less GPU time, memory and battery from Compute, mainly for the OnePlus at 4x. 60 FPS is the aim, not a hard target.

- **Render pacing: done, on by default.** `GPU::ComputePace` = 2. A fence follows each 3D render; when two are pending the render is skipped (the previous 3D image stays), or the emulator waits when it can't skip (capture frames, an active Smooth 30 FPS in-between). NSMB's map at 4x went from 2.1 to 60.0 presented FPS, with Smooth 30 FPS on or off.
  - Core: `GPU3D_Compute.cpp/.h` (`UpdatePace`, `WaitForPace`, `CanSkipForPace`, `RedrawSkippedFrame`, `ResetPace`, `PaceStale`), `GPU.cpp` (the line-0 redraw and the POWCNT redraw), `GPU3D_HalfCamera.cpp` (`CanSkipRender`).
- **`GPU::ComputeOpt`, default 95.** A sum of bits, each one an output-identical saving:
  - 1: skip the software divide for linear spans in Z-buffer programs.
  - 2: load only the span and polygon fields the coverage test needs.
  - 4: skip second-layer stores the final pass doesn't read unless AA or fog is on.
  - 8: a dead branch and a folded constant.
  - 16: a VRAM write only defeats the identical-frame skip when a cached texture really changed. White 2's overworld: 52 → 30 renders a second (Smooth off).
  - 32: **off by default.** Skip a render when a swap repeats the previous scene exactly (`GPU3D::MatchRenderScene`). It halves renders again but stutters; see "Next steps".
  - 64: pacing may skip with Smooth 30 FPS on when HalfCamera has nothing to interpolate.
- **Memory: done.** The compositor's own output textures are only made when no frontend texture is set (`GLCompositor::PrepareOutput`), and the app's frame queue reuses finished frames first and frees ones idle for 1800 frames (`FrameQueue.cpp/.h`). It settles at 4–5 textures, not 9.
- **Bug fixes with no switch:** rendering off (or geometry off, reset, load) draws the empty scene once and then skips, in all three renderers (`GPU3D::VBlank`, `SetEnabled`, `RenderedSinceVBlank`); `Texcache::CheckInvalid` wraps its dirty-word index correctly; the compressed-texture palette hash covers every byte a texel reads; `ComputeRenderer::Reset` forces a render.
- **Instrumentation, debug builds only:** `GPU3D_ComputeStats.h` and the switches below.
- **Docs:** `docs/plan-compute-efficiency.md` (new), a line in `docs/README.md`, a section and two open questions in `PERF-NOTES.md`.
- **Not part of this work, uncommitted in the app:** `HANDOFF-gpu-2d.md`, `docs/plan-gpu-2d-shadow.md`, `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties`. Leave them out of these commits.

### Where the OnePlus stands (Compute 4x, final build, Smooth 30 FPS on, defaults)

| Scene | Before | Now |
|---|---|---|
| NSMB map, presented FPS | 2.1 | 60.0 |
| White 2 title, FPS | 31.4 | 32.3 |
| White 2 overworld, FPS | 59.9 | 60.0 |

White 2's title is dual-screen 3D: a new swap every frame with a capture, so nothing can be skipped and it's plainly GPU-bound. 2x holds 60 there.

## Debug switches added

All `debug.melonade.<name>`, read at game start, documented in `DebugProperties.h`.

- `computepace N`: 0 old behavior, 1 wait, 2 skip or wait (default). Setting it logs `COMPPACE`.
- `computeopt N`: the bits above. 0 is the old shaders and skips; 127 turns the repeated-scene skip on.
- `computewhy`: `COMPWHY`, why each render wasn't skipped (`flush`, `flushsame`, `texsaved`, `swapsaved`, `midsame`, `halfcam`, `paced`, `stale`).
- `computesync`: `COMPSYNC`, per-pass time by `glFinish` and wall clock. Inflates everything about 2.5x, so read shares only.
- `computepeaks`: `COMPPEAK`, peak work tiles, spans and variants. It syncs every frame; ignore FPS.
- `computetile N`: force 8, 16 or 32 px tiles.
- `computetimers`: `COMPTIME`, GL timer queries. **Reads zero for compute passes on Adreno 640**; only the compositor buckets mean anything there.
- Always logged in debug builds: `FRAMEQ textures=N/9 WxH reason=...` when the frame queue's texture count changes.

## Next steps

1. **Merging:** the branch sits on top of `gpu-2d-shadow`, so it can't reach `material3` ahead of that line without a rebase. The shadow work touches `GPU3D_Compute.cpp` by two lines, so a rebase onto `widemelon` should be easy.
2. **The user's eyes on skipped frames.** Pacing leaves the previous 3D image up for about 7 frames a second on NSMB's map (up to 21 in Mario Kart's demo), with 2D still updating. Nobody has judged whether it looks juddery; `screenrecord` is blocked over adb on the OnePlus. If it does, `computepace 1` is the fallback (wait only: 51.6 FPS on the map).
3. **Why `computeopt` bit 32 stutters.** White 2's overworld, Smooth on: 60 → 30 renders a second and the GPU ends about 9 °C cooler, but FPS drops 59.97 → 58.36 with about 100 long frames per 30 s. The long frames are pacing waits (0.91 correlation with `wait_ms`); with `computepace 0` they become dropped presents instead (55.8 presented). So the GPU falls behind when half the renders are skipped. The guess is its clock drops in the gaps. A perfetto `gpu.counters` trace of `computeopt 127` against 95 would say. It's the remaining battery win for 30 FPS games with Smooth 30 FPS on.
4. **The Fold.** Left for the end by the user. It answered adb at 192.168.1.32 but sat at its PIN lock screen, so nothing was installed. Wanted: a sanity run at defaults, HASH2D on White 2's title, and graphics PSS at Compute 4x (696 MB on an older build), which decides the next item.
5. **Right-size the work buffers (plan step 3): on hold.** The OnePlus showed 174,944 kB of graphics memory at 4x, less than the buffers' nominal 229 MiB, so it doesn't seem to charge for untouched pages. Peak use was 33% of the tile work buffer and 14% of the span buffer. Only worth doing if the Fold's memory is really committed. The CPU's bounding-box count (`variantTileBound`) is an upper bound that could size the buffers without a readback.
6. **Pack attr into color and use one tile buffer instead of three.** Color uses 23 bits and attr 9, so they fit one word. Guess from reading: 10–20% of DepthBlend, 5–10% of Rasterise, and a third less tile memory. Not started. It's the best remaining idea for the title.
7. **Smaller gaps:** a scene with anti-aliasing or fog for bit 4 (Mario Kart's demo isn't frame-deterministic); a long plugged-in run; a real battery number (run 5's 240 mA was one plugged-in pass); the program binary cache (plan step 6).

Ruled out, don't retry: 16 px tiles at 4x (title 31.45 → 28.17), tile-grouped DepthBlend, GL timer queries on this Adreno.

A lead for the GPU 2D work: `HANDOFF-gpu-2d.md` lists an open OnePlus stall in shadow mode `all` on OpenGL (NSMB map, 321 ms render, 2.4 presented FPS). That has the same shape as the Compute stall pacing fixed, and the OpenGL renderer has no pacing.

## Testing notes

- **Everything is in `perf_logs/2026-11-09-compute-timers/`.** `summary.txt`, `summary_run2.txt` … `summary_run6.txt` and `summary_final.txt` hold the tables. `runs/` has one folder per run (logcat `lines.txt`, perf-logger CSVs, screenshots). `backup/` has the save backups and hashes.
- **Scripts to start from:** `scripts/ct_run_v6.sh` and `ct_chain_v5.sh` (cold start, set renderer, scale and props, reach a scene, pull logs), the `chain_*.txt` run lists, `ctsum6.py` and `ctsumf.py` (medians), `hashalign5.py` (exact-frame HASH2D compare of cold-boot title runs, handles midnight), `shotdiff.py` (masked screenshot compare), `slowframes.py` and `waitcorr.py` (slow frames against pacing waits).
- **Scenes:** White 2's title by cold boot with no input (frame-deterministic, so HASH2D compares frame by frame, but it only sees 3D at 1x through the capture). White 2's overworld and NSMB's world map by continuing the save with taps. No savestate was ever loaded; keep it that way.
- **Quick recipe:** one 30 s window per config, fresh app start, no perfetto. Runs 1–4 were unplugged, 5–6 and the final one plugged in. `computewhy` and `computetimers` cost nothing measurable; `computesync` and `computepeaks` cost a lot.
- **The OnePlus as left:** `app-final.apk` (sha1 `3cf45a96`) is installed, the same code as the working tree. Prefs are back to as-found: OpenGL, 2x, perf logging off, Smooth 30 FPS on. All `debug.melonade.*` props are cleared and the 11 save hashes match. Compute and 4x have to be set for each run.
- **APKs kept per run** in the same folder: `app-run3.apk` … `app-run6.apk`, `app-final.apk`.
- **PC harness:** it builds without the GL renderer, so it can't check Compute. It did match HEAD on 58,200 software-renderer frames over 21 games after the shared `RenderFrameIdentical` change, but no game there powers the 3D engine off, so that branch is covered by review and the White 2 bag check only.

## Leftovers

- **Scratch files that couldn't be deleted:** the review's harness builds, outputs and two savestate copies in `C:\Users\neyto\AppData\Local\Temp\claude\1369be8e5d078131\dce78297-015e-4f18-9215-edc3a9fc78d9\scratchpad`. Safe to delete.
- **Fold:** a copy of its installed APK was pulled to `perf_logs/2026-11-09-compute-timers/fold/backup/`. Nothing was changed on the phone.
- **OnePlus `/data/local/tmp`:** the run scripts may have left files there; not checked. Everything needed is already pulled.
- **Things to distrust:**
  - The POWCNT redraw runs GL inside an IO write handler, on the assumption a context is always current there.
  - Every Compute render now does a `glFenceSync` and a `glFlush`, in all builds.
  - With `computeopt` bit 4 on, layer 2 of the final tile buffer is stale when AA and fog are off. Nothing reads it today.
  - The size `static_assert` guarding `MatchRenderScene`'s field list wouldn't catch a `bool` added into padding.
  - Release builds make a handful of empty virtual calls a frame for the stats hooks.
  - The desktop Qt build of the lazy compositor outputs has never been built.
