# Handoff: Compute renderer efficiency

The plan and every result so far are in [docs/plan-compute-efficiency.md](docs/plan-compute-efficiency.md). Read that first. This file is the current state and the next steps. Build, install and phone-connection commands are in `HANDOFF.md`; the short version of the numbers is in `PERF-NOTES.md` under "Compute efficiency on the OnePlus 7 Pro".

## State (11-10)

This is branch `compute-efficiency-wm`: the `compute-efficiency` work (first cut from the `gpu-2d-shadow` tips, app `fa5c5dbf`, core `aa597f11`) rebased onto `widemelon` on 11-11. Everything through step 6 (packing attr into color) is committed and pushed: core `ff155ae7`, app `10b817ff`. Committed on 11-11, **not pushed**: the notes commit (app `1ed1b375`) and the checked buffer grows (core `56e5841d`, app `b9ce0e7e`).

**`compute-efficiency-wm` (11-11, both repos, not pushed):** the same work rebased onto `widemelon` without the GPU 2D shadow commits, so it can merge on its own. Core `5827f9c5` (on `77a2244d`), app `11b10c4c` (on `4e9fe2d0`), gitlinks mapped to the new core commits. It builds (arm64 debug); not phone-tested. Two conflicts: `GPU_OpenGL.cpp` `RenderFrame` (kept the compute timers, dropped `PrepareShadow2D` and the early `ctrlCopied`) and `DebugProperties.cpp`'s summary line (no `gpu2d` fields). One comment in `GPU3D_ComputeStats.h` was reworded so it no longer mentions the shadow. Its diff otherwise matches `compute-efficiency`'s. The app worktree it was built in is in the session scratchpad (`appwm`), safe to remove with `git worktree remove --force`.

**Checked buffer grows (11-11):** `WorkBufferData` drains GL errors (logging them as `COMPGLERR` in debug builds), allocates, then checks `glGetError` and `GL_BUFFER_SIZE`. A failed grow retries at the exact need, then at the old size, else the room goes to 0. A render that needs more room than exists is skipped with the previous 3D image kept and `ForceRender` set; retries back off (16 grow-wanting frames, doubling to 512). Logs `COMPGROW ... failed`. Reviewed, no bugs, built; never triggered, and on Adreno or Mali it may never fire, since those drivers often allocate lazily and fail later at dispatch. Known gaps from the review: a frame skipped for room still counts as a render in `COMPWHY` and the stats, and a HalfCamera redraw skipped for room leaves a capture that frame reading the in-between picture.

**Desktop build (11-11):** the core with OpenGL on (`BUILD_QT_SDL=OFF`, mingw64 GCC 15.1) compiles with no errors or warnings at `ff155ae7`, which covers the lazy compositor outputs and all the Compute files. The Qt frontend itself isn't built: Qt6, SDL2, libarchive and faad2 aren't installed (MSYS2 `mingw-w64-x86_64-{qt6-base,qt6-multimedia,qt6-svg,SDL2,libarchive,faad2,pkgconf}`, about 1–1.5 GB). From reading, desktop never calls `SetOutputTexture`, so it always takes the lazy path.

### Step 6 confirm run (11-11)

**OnePlus: passed.** The runs had all finished on 11-10 before the session died (`runs/pack2_*`, `app-pack2.apk`, not charging). `COMPTILES buffers=2 opt=223` at default; `computeopt 95` launches on 2 and rebuilds to `buffers=3 opt=95`. Title 33.15 / 32.26 / 33.07 FPS (default / 95 / default), NSMB map 59.96 FPS with render median 5.68 ms and 33 renders over 16 ms. HASH2D at default against `pack_hash_o95`: 0 mismatches over 1,255 frames. No errors in any of the five, all 11 save hashes OK. Line added to `PERF-NOTES.md`.

**Still open:**

1. **The OnePlus's state is unchecked.** On 11-11 it answered ping but adb refused 5555 and mdns showed no `_adb-tls-connect` for .214, so wireless debugging is off; someone has to turn it back on at the phone. Then check what's below.
2. **The Fold's half of the confirm run** (the user was away on 11-11): title at default and with `computeopt=95`, with graphics memory; NSMB map at default; title with `hash2d` at default against the Fold's earlier 95 hash run. Low risk, since the OnePlus passed and the A/B passed on the Fold.

The old notes from 11-10, for the phone check:

1. **Check the OnePlus first; the Fold is done.** The phone agent was killed before it restored anything. Fold, checked and fixed by hand afterwards (11-10): `app-pack2.apk` is installed, no props set, White 2's three Lua scripts re-enabled and perf logging turned back off (the run had left both changed); the user had already moved it to resolution 8 and set wide view again. The file as it was before that fix is `fold-pack/asfound/rom_data_before_fix.json`. OnePlus: `app-pack2.apk` installed, no props, app stopped; prefs and `rom_data.json` not checked. What to check on a phone: no `debug.melonade.*` prop set (`getprop | grep melonade`), prefs as-found (OnePlus: OpenGL, 2x, perf logging off, Smooth 30 FPS on; Fold: Compute, resolution 4, Smooth on, perf logging off, dithering and deband off, filtering none), and `rom_data.json` back to the user's own (the runs swap in a copy with White 2's Lua scripts and wide view off; that matters on the Fold, which the user plays on). As-found copies and save hashes are in `runs/pack_asfound/` (OnePlus) and `fold-pack/asfound/` (Fold). Work folders on the phones: `/data/local/tmp/cte` (OnePlus), `/data/local/tmp/ctf` (Fold).
2. **(OnePlus done 11-11, Fold open) Finish the confirm run of `app-pack2.apk`** (sha1 `c66df8af`, the working tree, default 223). It may or may not be installed on each phone; `pm path` plus `sha1sum` says. Any finished runs are in `runs/pack2_*` and `fold-pack/runs/pack2_*`. Wanted, Compute 4x, Smooth on, 30 s each:
   - OnePlus: title at default, with `computeopt=95`, at default again; NSMB map at default; title with `hash2d` at default against `runs/pack_hash_o95`.
   - Fold: title at default and with `computeopt=95`, with graphics memory; NSMB map at default; title with `hash2d` at default against the Fold's earlier 95 hash run.
   - Expect `COMPTILES buffers=2` at default, and `buffers=2` then `buffers=3` with 95; about 33.1 FPS against 32.2 on the OnePlus title; 0 HASH2D mismatches.
   - What this run adds over the A/B that already passed: a launch that starts on two buffers (the A/B build started on three and switched), and the 2 → 3 rebuild for `computeopt 95`.
3. **Then** add a line to `PERF-NOTES.md`, update this file, commit that (leave out `HANDOFF-gpu-2d.md`, `docs/plan-gpu-2d-shadow.md`, `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties`), and push core then app if they aren't. If the run fails, `computeopt 95` is the old layout; the default is one line in `GPU.h` plus `OptMask` and `TileLayers` in `GPU3D_Compute.h`.

It's tested on the OnePlus 7 Pro, and on 11-10 `app-final.apk` had a sanity, HASH2D and memory run on the Fold (see "The Fold" below).

The goal: less GPU time, memory and battery from Compute, mainly for the OnePlus at 4x. 60 FPS is the aim, not a hard target.

- **Render pacing: done, on by default.** `GPU::ComputePace` = 2. A fence follows each 3D render; when two are pending the render is skipped (the previous 3D image stays), or the emulator waits when it can't skip (capture frames, an active Smooth 30 FPS in-between). NSMB's map at 4x went from 2.1 to 60.0 presented FPS, with Smooth 30 FPS on or off.
  - Core: `GPU3D_Compute.cpp/.h` (`UpdatePace`, `WaitForPace`, `CanSkipForPace`, `RedrawSkippedFrame`, `ResetPace`, `PaceStale`), `GPU.cpp` (the line-0 redraw and the POWCNT redraw), `GPU3D_HalfCamera.cpp` (`CanSkipRender`).
- **`GPU::ComputeOpt`, default 223.** A sum of bits, each one an output-identical saving:
  - 1: skip the software divide for linear spans in Z-buffer programs.
  - 2: load only the span and polygon fields the coverage test needs.
  - 4: skip second-layer stores the final pass doesn't read unless AA or fog is on.
  - 8: a dead branch and a folded constant.
  - 16: a VRAM write only defeats the identical-frame skip when a cached texture really changed. White 2's overworld: 52 → 30 renders a second (Smooth off).
  - 32: **off by default.** Skip a render when a swap repeats the previous scene exactly (`GPU3D::MatchRenderScene`). It halves renders again but stutters; see "Next steps".
  - 64: pacing may skip with Smooth 30 FPS on when HalfCamera has nothing to interpolate.
  - 128: attr's 9 bits ride in the free bits of the color tile word, so there are two tile buffers, not three (step 6 below).
  - 256: **off by default.** Color and depth interleaved in one buffer. Needs 128. No faster than two buffers and no smaller.
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

### The Fold (11-10, `app-final.apk`, Compute 4x, Smooth on, defaults, plugged in, cover screen)

| Scene | FPS | presented | render median ms | frames over 16.7 ms |
|---|---|---|---|---|
| White 2 title | 60.00 | 60.0 | 0.46 | 0 |
| White 2 overworld | 60.00 | 60.0 | 0.75 | 0 |
| NSMB map | 60.00 | 60.0 | 0.68 | 0 |

- **Not GPU-bound anywhere.** Pacing never skips there (NSMB map `computewhy`: paced=0, 60 renders a second). No GL errors, no crashes.
- **HASH2D on the title matches the OnePlus exactly:** 0 mismatches over 838–860 overlapping frames against the OnePlus final and run 6 runs, Smooth on and off, and 1,952 frames between the Fold's own on and off runs.
- **The Fold does commit the Compute buffers**, unlike the OnePlus. Title, settled: Compute 4x graphics 477–483 MB (GL mtrack 378 MB), OpenGL 4x 205 MB (GL mtrack 118 MB). The gap of about 260 MB is about the buffers' full nominal size. The old build showed 696 MB.
- Peak use (`computepeaks`, title) matches the OnePlus: 61,416 of 196,608 work tiles (31%), 74,959 of 524,288 X spans (14%). Frame queue settles at 4 textures for both renderers.

## Debug switches added

All `debug.melonade.<name>`, read at game start, documented in `DebugProperties.h`.

- `computepace N`: 0 old behavior, 1 wait, 2 skip or wait (default). Setting it logs `COMPPACE`.
- `computeopt N`: the bits above. 0 is the old shaders and skips; 95 is the old three tile buffers; 479 is one tile buffer; 255 turns the repeated-scene skip on.
- Always logged in debug builds: `COMPTILES buffers=N opt=N scale=N tile=N maxblock=N` at each `ResizeTargets`. `maxblock` is the GPU's storage block limit: 512 MB on the OnePlus, 2 GB on the Fold.
- `computewhy`: `COMPWHY`, why each render wasn't skipped (`flush`, `flushsame`, `texsaved`, `swapsaved`, `midsame`, `halfcam`, `paced`, `stale`).
- `computesync`: `COMPSYNC`, per-pass time by `glFinish` and wall clock. Inflates everything about 2.5x, so read shares only.
- `computepeaks`: `COMPPEAK`, peak work tiles, spans and variants. It syncs every frame; ignore FPS.
- `computetile N`: force 8, 16 or 32 px tiles.
- `computetimers`: `COMPTIME`, GL timer queries. **Reads zero for compute passes on Adreno 640**; only the compositor buckets mean anything there.
- Always logged in debug builds: `FRAMEQ textures=N/9 WxH reason=...` when the frame queue's texture count changes.

## Next steps

1. **Merging:** done as `compute-efficiency-wm` (see State). It still needs a phone sanity run (Compute 4x title and NSMB map, HASH2D against `compute-efficiency`) before it's pushed or merged into `widemelon`.
2. **The user's eyes on skipped frames.** Pacing leaves the previous 3D image up for about 7 frames a second on NSMB's map (up to 21 in Mario Kart's demo), with 2D still updating. Nobody has judged whether it looks juddery; `screenrecord` is blocked over adb on the OnePlus. If it does, `computepace 1` is the fallback (wait only: 51.6 FPS on the map).
3. **Why `computeopt` bit 32 stutters.** White 2's overworld, Smooth on: 60 → 30 renders a second and the GPU ends about 9 °C cooler, but FPS drops 59.97 → 58.36 with about 100 long frames per 30 s. The long frames are pacing waits (0.91 correlation with `wait_ms`); with `computepace 0` they become dropped presents instead (55.8 presented). So the GPU falls behind when half the renders are skipped. The guess is its clock drops in the gaps. A perfetto `gpu.counters` trace of `computeopt 127` against 95 would say. It's the remaining battery win for 30 FPS games with Smooth 30 FPS on.
4. **The Fold: done for this build** (results above). Not done there: a run unplugged, and the main screen (all runs were on the cover screen).
5. **Right-size the work buffers (plan step 3): done, committed and pushed (core `90a08efc`, app `2896335b`), tested on both phones (11-10).** `GPU::ComputeGrow` (default on; `debug.melonade.computegrow 0` gives the old full sizes). Tile memory starts at 6 of 16 layers and X spans (plus the setup-index texture buffer) at a 16th. `GrowWorkBuffers` runs after polygon setup and before any upload or dispatch, fed `totalTileBound` and the exact setup-index count; it grows to need + 25%, rounded to a step, capped at full size, grow-only until `ResizeTargets`. No shader change: a review confirmed the GPU never bins outside the CPU's bound. Logs `COMPGROW`; `COMPPEAK` gained `room`, `pastroom`, `pastbound`, `xroom` (both "past" fields read 0 on every line on both phones).
   - Fold graphics, grow on / off: title 388 / 483 MB, overworld 428 / 510, NSMB map 312 / 466. FPS 60.00 in all six. OnePlus: FPS unchanged (title 32.24 / 32.22, unplugged), and its meminfo doesn't show the buffers at all.
   - Title and overworld grow once at the first 3D frame (work tiles 73,728 → 147,456, X spans 32,768 → 81,920); the Fold's title also grew X spans to 106,496 at 117 s. NSMB's map never grows, but its X span peak (32,710) is only 58 under the starting room.
   - HASH2D matched on every overlapping frame on both phones (Fold 4,536 from boot, including the frames right after the grow; OnePlus 1,226). A 4x → 6x switch mid-game reset and regrew cleanly.
   - Logs: `perf_logs/2026-11-09-compute-timers/fold-grow/` and `runs/grow_*` (OnePlus). APK `app-grow.apk` (sha1 `7ea1eb6a`), installed on both phones.
6. **Pack attr into color: done and committed (11-10). The A/B build passed on both phones; the build with it as the default passed on the OnePlus (11-11), the Fold's run is still open (see "Step 6 confirm run").** `computeopt` bit 128, default on. Attr's 9 bits (edge flags, coverage) go in the color word's free bits (6–7, 14–15, 22–23, 29–31), Rasterise masks color to `0x1F3F3F3F` before packing, and DepthBlend unpacks only in the opaque branch, the one place attr is read. The third tile buffer is never allocated. Bit 256 puts color and depth in one buffer; it's off because it measured the same as two.
   - OnePlus, 4x, Smooth on, not charging, `computeopt` 95 / 223 / 479: title 32.23 / 33.12 / 33.20 FPS (two passes each, within 0.15). NSMB map render median 7.26 / 5.87 / 5.73 ms, presents with a render over 16 ms 156 / 35 / 26. Overworld unchanged at 59.9.
   - Fold, 4x, graphics memory settled, 95 / 223: title 379 / 342 MB, overworld (a Pokemon Center now) 398 / 371, NSMB map 314 / 282. 479 is the same as 223. FPS 60.00 everywhere.
   - HASH2D on the title, 223 and 479 against 95: 0 mismatches (OnePlus 1,247 and 1,255 frames, Fold 1,766 and 1,859). No GL errors or crashes in 28 runs. Resolution 6 on the Fold with 479 rendered right.
   - The user's own check (11-10, after the runs, presumably the Fold; `app-pack.apk` or `app-pack2.apk`, not confirmed which): White 2's title at Compute 8x holds a stable 60 FPS. Turning deband on drops it, which is deband's known cost, not this change.
   - Two reviews found no bugs. The one-buffer fallback (buffer over the GPU's block limit) never triggered on either phone, so that path is untested.
   - Files: core `GPU3D_Compute.cpp/.h`, `GPU3D_Compute_shaders.h`, `GPU.h`; app `debug/DebugProperties.cpp/.h` (the prop's clamp went 127 → 511).
   - Logs: `runs/pack_*` (OnePlus), `fold-pack/` (Fold). APKs `app-pack.apk` (sha1 `8ef244aa`, bits off by default) and `app-pack2.apk` (sha1 `c66df8af`, default 223, the working tree).
7. **Smaller gaps:** a scene with anti-aliasing or fog for bit 4 (Mario Kart's demo isn't frame-deterministic); a long plugged-in run; a real battery number (run 5's 240 mA was one plugged-in pass); the program binary cache (plan step 6).

Ruled out, don't retry: 16 px tiles at 4x (title 31.45 → 28.17), tile-grouped DepthBlend, GL timer queries on this Adreno.

A lead for the GPU 2D work: `HANDOFF-gpu-2d.md` lists an open OnePlus stall in shadow mode `all` on OpenGL (NSMB map, 321 ms render, 2.4 presented FPS). That has the same shape as the Compute stall pacing fixed, and the OpenGL renderer has no pacing.

## Testing notes

- **Everything is in `perf_logs/2026-11-09-compute-timers/`.** `summary.txt`, `summary_run2.txt` … `summary_run6.txt` and `summary_final.txt` hold the tables. `runs/` has one folder per run (logcat `lines.txt`, perf-logger CSVs, screenshots). `backup/` has the save backups and hashes.
- **Scripts to start from:** `scripts/ct_run_v6.sh` and `ct_chain_v5.sh` (cold start, set renderer, scale and props, reach a scene, pull logs), the `chain_*.txt` run lists, `ctsum6.py` and `ctsumf.py` (medians), `hashalign5.py` (exact-frame HASH2D compare of cold-boot title runs, handles midnight), `shotdiff.py` (masked screenshot compare), `slowframes.py` and `waitcorr.py` (slow frames against pacing waits).
- **Scenes:** White 2's title by cold boot with no input (frame-deterministic, so HASH2D compares frame by frame, but it only sees 3D at 1x through the capture). White 2's overworld and NSMB's world map by continuing the save with taps. No savestate was ever loaded; keep it that way.
- **Quick recipe:** one 30 s window per config, fresh app start, no perfetto. Runs 1–4 were unplugged, 5–6 and the final one plugged in. `computewhy` and `computetimers` cost nothing measurable; `computesync` and `computepeaks` cost a lot.
- **The OnePlus as left:** see "Where step 6 stopped"; `app-pack.apk` or `app-pack2.apk` is installed. As-found prefs: OpenGL, 2x, perf logging off, Smooth 30 FPS on. 11 save hashes. Compute and 4x have to be set for each run. It wasn't charging during the step 6 runs (battery 91 → 83%), so those count as unplugged.
- **The step 6 scripts:** OnePlus `runs/pack_scripts/` (`ct_run_v8.sh`, which adds `COMPTILES` to `lines.txt`), Fold `fold-pack/scripts/` (`ctfp_run.sh`, `shotdiff_fold.py`).
- **The Fold changed between runs (11-10):** as-found is now Compute, resolution 4, dithering off, deband off, filtering none. Its White 2 save was played, so "continue" lands in a Pokemon Center, not the old overworld spot, and there are 46 save and state files, not 45. It was unplugged for the step 6 runs.
- **The Fold as left after the grow runs (old):** `app-final.apk` installed over its old build with data kept. Prefs restored to as-found (Compute, resolution 6, Smooth 30 FPS on, perf logging off, debug log on). `debug.melonade.*` props are empty (seven compute* props now exist with empty values until a reboot). All 45 save and state hashes match; copies are in `fold/backup/`. Its NSMB slots are "New", so "continue" starts a new game, which still reaches World 1's map. White 2 runs used a `rom_data` copy with its Lua scripts and wide view off, to match the OnePlus. Fold scripts are in `fold/scripts/` (`ctf_run.sh`, `ctf_chain.sh`).
- **HASH2D at 60 FPS loses lines:** the core's log limit (50 lines a second per call site) drops about 15% of HASH2D lines when a game runs at 60. A full frame-by-frame check on a fast phone needs that limit raised.
- **APKs kept per run** in the same folder: `app-run3.apk` … `app-run6.apk`, `app-final.apk`.
- **PC harness:** it builds without the GL renderer, so it can't check Compute. It did match HEAD on 58,200 software-renderer frames over 21 games after the shared `RenderFrameIdentical` change, but no game there powers the 3D engine off, so that branch is covered by review and the White 2 bag check only.

## Leftovers

- **Scratch files that couldn't be deleted:** the review's harness builds, outputs and two savestate copies in `C:\Users\neyto\AppData\Local\Temp\claude\1369be8e5d078131\dce78297-015e-4f18-9215-edc3a9fc78d9\scratchpad`. Safe to delete.
- **Fold:** a copy of its old APK is in `perf_logs/2026-11-09-compute-timers/fold/backup/fold-apk-before.apk`. `/data/local/tmp` was emptied on both phones on 11-10 (all old test leftovers, about 3.8 GB; only the Fold's `.studio` kept), so the run scripts have to push their tools again. The grow runs also left 50 `perf_*` files in `/sdcard/Documents/2DS/Logs`. Between the two Fold runs someone set its resolution to 7 and played White 2; that's now its as-found state.
- **Things to distrust:**
  - Packing masks color to 6/6/6/5 bits. `ClipSegment` truncates `factor_den` to 32 bits, so extreme clip coordinates could push a vertex color out of range; the old code let the stray bits through to the final pass, the new one cuts them off. Both are wrong there, differently. Nobody has found a game that does it.
  - A savestate stores `IsShadowMask` and `Attr` separately. If one ever disagreed, packing would render it differently from before (before, it read stale attr tiles).
  - `computeopt 95` now launches with two tile buffers and rebuilds for three at the first frame, so it costs a second shader compile.
  - `TileLayers`' initial value (2) is hard-coded to match `OptMask`'s default.
  - A buffer grow now checks for `GL_OUT_OF_MEMORY` (see State), but lazy drivers may never report it, and a grow still briefly holds old and new storage while earlier renders are in flight.
  - The POWCNT redraw runs GL inside an IO write handler, on the assumption a context is always current there.
  - Every Compute render now does a `glFenceSync` and a `glFlush`, in all builds.
  - With `computeopt` bit 4 on, layer 2 of the final tile buffer is stale when AA and fog are off. Nothing reads it today.
  - The size `static_assert` guarding `MatchRenderScene`'s field list wouldn't catch a `bool` added into padding.
  - Release builds make a handful of empty virtual calls a frame for the stats hooks.
  - The desktop Qt build of the lazy compositor outputs has never been built.
