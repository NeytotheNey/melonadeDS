# Handoff: performance and frame pacing

Branch `material3`, 15 commits on top of `8480c8fc` (Material 3 UI). Pushed to the private repo `origin` = github.com/NeytotheNey/melonadeDS (project name: melonadeDS). `upstream` = rafaelvcaetano/melonDS-android, fetch only (push URL set to `no_push`). Kept out of git with the local exclude: `build-*.log` and `perf_logs/`.

| Commit | What |
|---|---|
| `1350fdbc` | Opt-in performance logger |
| `ccaecf29` | Save file write moved outside the save buffer lock |
| `37bf7211` | Judder (frame-hold cadence) stat in the perf summary |
| `a337a61f` | Vsync-locked frame pacing, optional extra frame buffer |
| `a78e88ff` | Pacing stays locked across 60↔120 Hz switches |
| `db365576` | "Lock display to 60 Hz" setting (on by default) |
| `db01be7e` | Audio thread no longer busy-spins; failed stream open no longer crashes |
| `6bce535f` | Debug variant builds native code at -O2 instead of -O0 |
| `4c32a97d` | minSdk 37 (native TLS instead of emutls); dex kept compressed |
| `2726b987`..`ce86acb7` | Ported from upstream PRs: audio lifecycle fixes, save flush retry, DSiWare JNI leak, forward-only RTC sync, hold to fast-forward, mute on fast-forward |

## Build and install

- `JAVA_HOME="C:\Program Files\Java\jdk-25"`, SDK `C:\Tools\android-sdk`.
- `./gradlew assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false` (without `testOnly=false` the phone refuses the install).
- APK: `app/build/intermediates/apk/gitHubProd/debug/app-gitHub-prod-debug.apk`.
- Phone: Galaxy Z Fold `SM-F971U`, serial `R5GL64A7LKN`. adb isn't on PATH: `C:\Tools\android-sdk\platform-tools\adb.exe`. Over wireless debugging, run `adb mdns services`, `adb connect` to the `adb-R5GL64A7LKN` entry, and use that IP:port with `-s` (the port changes).
- Debug app ID `me.magnum.melonds.dev` installs beside the official app.
- minSdk is 37 (personal build, Android 17 only). NDK r28 tops out at API 35, so native code builds for 35 with a harmless AGP warning. `packaging.dex.useLegacyPackaging = true` keeps dex compressed (otherwise minSdk 28+ stores it raw and the APK doubles to ~98 MB); APK is ~38 MB.

## Performance logger

Settings → Performance → Diagnostics → Performance logging. Each session writes `_frames.csv`, `_display.csv`, `_memory.csv` and `_summary.txt` to `/sdcard/Android/data/me.magnum.melonds.dev/files/perf_logs`. Pull that folder into the repo root.

Summary keys worth comparing: `display_off_cadence_pct` (judder), `display_judder_worst_10s_pct`, `fps_1pct_low`, `interval_p99_ms`, `missed_frames`, `work_p95_ms`, `pacing_locked_frames_pct`. Ignore `display_unexpected_repeats`, which misses judder.

`thermal_status` doesn't see Samsung's CPU caps. Check logcat instead: `limitCPUFreq` for caps, `thermal_core` for AP/skin temps (in tenths of a °C). Analysis is quick Python over the CSVs (run with `py -I`); the scripts live in the session scratchpad and don't survive.

## Results (Pokémon White 2, same route)

| Run | Settings | Judder | Work avg / p95 | Notes |
|---|---|---|---|---|
| 10-26 19:46 | 2x, before pacing | 13.1% | 12.8 / 15.2 ms | baseline |
| 10-26 20:57 | 2x, pacing + buffer | 0.82% | 12.9 / 15.3 ms | |
| 10-26 21:47 | 2x, + 60 Hz lock, charging | 0.65% | 13.7 / 17.6 ms | CPU capped to 1.75 GHz at 185 s, AP 43–46 °C |
| 10-26 21:58 | OpenGL 1x, charging | 0.11% | 14.0 / 16.3 ms | capped at 2.1–2.4 GHz throughout, still fine |
| 10-27 08:07 | OpenGL 1x, spin fix, **unplugged** | 0.49% | 13.5 / 16.4 ms | 234 s, no cap, AP peaked 36 °C, 0% judder after warm-up |
| 10-27 08:24 | same, **native -O2** (unplugged) | 0.04% | 10.2 / 11.9 ms | 712 s, unfolded at 435 s; no cap, AP 38 → 32 °C; 4 missed frames total, warm-up burst gone |

All runs before 08:24 used native code built at -O0 (see below), so their work times are inflated.

## What we know

- **The debug build used to compile native code at -O0.** AGP's Debug CMake type adds no -O flag. The debug build type in `app/build.gradle.kts` now passes `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (-O2 -g, asserts off). Work per frame dropped 13.5 → 10.2 ms, so there's ~6 ms of headroom now.
- **CPU profile on -O2** (10-27 08:46, 60 s, `perf_logs/o2_profile.data`): the emulator thread is ~60% of a core, about 10 ms per frame (it was ~81% at -O0). Shares of that thread: 2D layers in software 30% (~3 ms/frame, was ~5 ms), memory/IO/DMA 13%, Adreno driver 10%, 3D geometry 8.5%, emutls + PLT + `pthread_getspecific` 7.8% (minSdk 24 forced emutls; minSdk is now 37 and emutls is gone, saving ~0.3 ms/frame; ~3.4% `@plt` remains), timers/scheduler 6%, JIT game code 4%, JIT dispatch 4%, sound 3%. Audio thread 1.4%.
- **Recording a profile:** `simpleperf record --app me.magnum.melonds.dev --duration 60 -e cpu-clock -f 1000 -o /data/local/tmp/x.data`, then `simpleperf report -i ... --comms EmulatorThread --sort dso,symbol` on the device. The user is remote-controlling from the same phone, so start it from a script that waits for `EmulatorActivity` in `dumpsys activity activities`.
- **Thermal throttling** hit while charging on the -O0 build. The -O2 run (12 min, unplugged) had no cap and the AP cooled to ~32 °C.
- **JIT warm-up** at 10–20 s was the worst window on -O0. On -O2 it's down to 3 missed frames.
- **Compute renderer** costs 1.5–2.5 ms more CPU than OpenGL. Sticking with OpenGL.
- Unfolding causes a one-off ~0.2 s stall (expected). Saving no longer causes slow frames. Memory is stable at ~1 GB RSS; graphics PSS grows over a session.

## Frame pacing code map

- `app/src/main/cpp/renderer/FramePacing.h` (`DisplayVsyncTracker`, `VsyncFramePacer`), `emulate()` in `MelonDSAndroidJNI.cpp`, `FrameQueue::getPresentFrame`, the Choreographer renderers.
- 60 Hz lock: `EmulatorActivity.updateDisplayModeLock`, `extensions/DisplayExtensions.kt`. Dropped during fast-forward.
- Settings: Video → Frame pacing (sync to refresh, extra frame buffer, lock 60 Hz). All on by default and apply live.
- Pacing only runs with no fast-forward, a recent vsync, and refresh/60 close to a whole number 1–4. Otherwise it falls back to the old timer.
- Untuned constants: 0.5 ms grid offset, 2% clamp, 1/8 gain, P/2 tolerance. The Python simulation used to check the scheduler is gone; re-port `FramePacing.h` if the logic changes.

## Open items

1. CPU targets, only if a heavier game needs it: 2D rendering on its own thread or the GPU (~3 ms, big job in the core submodule).
2. Thermal on -O2 while charging is untested. Ideas if it still caps: `sustained_performance_mode`, ADPF hints, audio time-stretching.
3. The extra frame buffer adds ~17 ms of input lag. On vs off hasn't been compared by feel, and buffer-off judder hasn't been measured.
4. Not measured on the device: the pacer fix with the 60 Hz lock off, front-buffer mode, an external display, pre-API-33 (`ChoreographerOldFrameRenderer`).
5. Known issues left alone: the mic stream (enableMic/disableMic) isn't under `audioOutputMutex`; a FilterAudioStream (only if Oboe resamples) can be freed under its error thread; ADPF hints miss the inner stream when Oboe resamples; paced → timer fallback with the buffer on skips a frame; the summary's settings block is only read at session start; a save write that fails after open has already truncated the old file.

6. Upstream PRs (surveyed 10-27). Ported as our own versions on 10-27: audio/save/JNI fixes from #1682, forward-only RTC sync (#1664/#1682), hold to fast-forward (#1608), mute on fast-forward (#1625). Phone test 10-27 10:33 passed: clock caught up after screen off, fast-forward silent, audio resumed after pauses, saves written, judder 0.07%. Note: fast-forward pushes the DS clock ahead (it runs on emulated time) and the forward-only sync leaves it there until the next boot/load. Maybe later: #1657 idle power (test fold), #1676 fast-forward rate matching (idea could extend to slowdowns), #1677 cheat search. Skip #1682 as a whole, #1666, #1524, #1606.

## Working notes

- Coding goes through a subagent, and a separate subagent reviews the diff from the code itself. Fixes go back to the coder, then build and install.
- The user plays on the phone, then asks for the logs to be pulled.
