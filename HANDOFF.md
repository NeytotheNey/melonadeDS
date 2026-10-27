# Handoff: melonadeDS

Branch `material3`, 18 commits on top of `8480c8fc` (Material 3 UI). Pushed to the private repo `origin` = github.com/NeytotheNey/melonadeDS (project name: melonadeDS); the two layout editor commits are not pushed yet. `upstream` = rafaelvcaetano/melonDS-android, fetch only (push URL set to `no_push`). Kept out of git with the local exclude: `build-*.log` and `perf_logs/`.

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
| `8b12c09d` | This handoff file |
| `116fa1ce` | Layout editor phase 1: floating menu button, gesture-nav fixes, Save, discard confirm |
| `5261e80c` | Layout editor: pill toolbar, expandable panel, corner resize handles |

## Build and install

- `JAVA_HOME="C:\Program Files\Java\jdk-25"`, SDK `C:\Tools\android-sdk`.
- `./gradlew assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false` (without `testOnly=false` the phone refuses the install). Incremental builds take ~10 s.
- Quick compile check: `./gradlew :app:compileGitHubProdDebugKotlin :app:processGitHubProdDebugResources -Pandroid.injected.build.abi=arm64-v8a`.
- APK: `app/build/intermediates/apk/gitHubProd/debug/app-gitHub-prod-debug.apk`.
- Phone: Galaxy Z Fold `SM-F971U`, serial `R5GL64A7LKN`. adb isn't on PATH: `C:\Tools\android-sdk\platform-tools\adb.exe`. Over wireless debugging, run `adb mdns services`, `adb connect` to the `adb-R5GL64A7LKN` entry, and use that IP:port with `-s` (the port changes).
- Screenshots: the phone has two displays, so `screencap` needs `-d <id>` from `dumpsys SurfaceFlinger --display-id`. When folded, the cover screen is the second ID and the inner one captures black.
- Debug app ID `me.magnum.melonds.dev` installs beside the official app.
- minSdk is 37 (personal build, Android 17 only). NDK r28 tops out at API 35, so native code builds for 35 with a harmless AGP warning. `packaging.dex.useLegacyPackaging = true` keeps dex compressed (otherwise the APK doubles to ~98 MB); APK is ~43 MB.

## Current work: layout editor

The user plays on the folded cover screen in portrait, with gesture navigation and no nav bar. The goal is an editor that's easy to use there, borrowing tools from the DraStic mod in `C:\Dev\drastic`.

### Done (phone-tested, committed)

- **Floating ⋮ menu button**, always visible. Tap opens the menu; long-press then drag moves it. Its position is saved per orientation as fractions of the view, in the default SharedPreferences under `layout_editor_menu_button[_external]_{portrait|landscape}_{x|y}`. The old bottom bar is gone.
- **Menu**: Add component, Properties, Background, Revert changes, Reset to default, Save (stays in the editor), Save and exit, Exit without saving (asks only if `hasUnsavedChanges()`).
- **Gesture navigation**: touches that start in the left/right `systemGestures` inset don't grab items, so a side swipe is back. ACTION_CANCEL restores the item. Back deselects the selected item, otherwise opens the menu.
- **Pill toolbar** next to the selected item: smaller, bigger (hold to repeat, 16 px steps), center horizontally, delete, expand, Done.
- **Expanded panel**, docked on the half away from the item, clear of the ⋮ button: one Size slider (Width/Height for Free screens; tap a number to type it), 4:3 / 16:9 / Free, 1x/2x/3x/Fit (n × 256 px wide), opacity and "above other screen" (screens only), center H/V, nudge arrows, delete. Collapse goes back to the pill. The last mode is saved as `layout_editor_item_panel_expanded`.
- **Corner handles** resize with the opposite corner fixed, keeping the aspect ratio when locked, and show "W × H" while dragging. There's a touch slop before resizing starts. Items under ~80 dp only get the bottom-right handle, so their middle stays draggable.

### Next: phase 2, grid and snapping

Port the snap grid from the user's DraStic mod (`C:\Dev\drastic\tools\smali-src\EditorGrid.java`, ~19 KB):

- Grid overlay with an on/off switch and Small/Medium/Large (16/32/48 dp), remembered in prefs. The fine grid is drawn only while snapping is on. Put the setting in the ⋮ menu.
- Snap while dragging, not on release. Controls snap their centre (`snapAxis`). Screens snap their nearest edge, and the editor's edges count as lines (`snapEdge`). Add the fold line when unfolded (`LayoutEditorActivity` already collects `FoldingFeature`s).
- Snap the corner-handle resize too.
- Screen-to-screen alignment: within 5%, snap a moved screen's centre or edges to the other screen's, and match sizes. DraStic does this on release in `Customizer.java` `q()` (L1072, `C:\Dev\drastic\java-src\sources\com\dsemu\drastic\ui\`).

Where it hooks in, all in `ui/layouteditor/LayoutEditorView.kt`: `dragView` (move), `resizeFromHandle` / `constrainSize` (handles), `resizeSelectedView` / `resizeSelectedViewBy` (pill and panel), and `applyRect` (the single place that sets `modifiedByUser`). Draw the grid in `dispatchDraw` behind the components.

### Code map

- `LayoutEditorActivity`: menu options enum, save/exit, display and fold tracking.
- `LayoutEditorManagerView`: hosts everything; the ⋮ button, menu and dialogs. It's also used on an external display through `ExternalLayoutEditorPresentation`, so dialogs go through `showInCurrentWindow()` and icon tints are set in code, not with `app:tint`.
- `LayoutEditorItemControls`: pill and panel logic, aspect-ratio guess, scale shortcuts, repeat-while-held. `LayoutEditorPanelScrollView` caps the panel size.
- `LayoutEditorView`: selection, drags, handles, all resize and move APIs.
- `LayoutEditorViewModel`: `saveCurrentLayout()` assigns the UUID of a new layout and stores it in the `SavedStateHandle`; `initialLayoutConfiguration` is the last-saved baseline for `hasUnsavedChanges()` and Revert.

### Decisions and known limits

- Revert changes goes back to the last Save, not to when the editor was opened.
- A control sitting fully inside the gesture strip (~24–40 dp at each side) can only be grabbed by its inner part.
- Per-control opacity isn't applied in game: `RuntimeLayoutView.updateInputs()` overwrites each control's alpha with the layout-wide opacity. That's why the opacity slider is shown for screens only.
- A screen's aspect ratio is guessed on selection (within 2 px of 4:3 or 16:9, otherwise Free). Controls stay at their builder's aspect ratio (all 1:1).
- A gamepad can't move focus into the pill or panel; the D-pad nudges the selected item instead.
- `label_alpha`, `label_aspect_ratio` and the `aspect_ratio_*` strings are now unused. They were left in because they have translations.
- Not checked: the editor on an external display, and light theme.

## Performance work (done, parked)

### Performance logger

Settings → Performance → Diagnostics → Performance logging. Each session writes `_frames.csv`, `_display.csv`, `_memory.csv` and `_summary.txt` to `/sdcard/Android/data/me.magnum.melonds.dev/files/perf_logs`. Pull that folder into the repo root.

Summary keys worth comparing: `display_off_cadence_pct` (judder), `display_judder_worst_10s_pct`, `fps_1pct_low`, `interval_p99_ms`, `missed_frames`, `work_p95_ms`, `pacing_locked_frames_pct`. Ignore `display_unexpected_repeats`, which misses judder.

`thermal_status` doesn't see Samsung's CPU caps. Check logcat instead: `limitCPUFreq` for caps, `thermal_core` for AP/skin temps (in tenths of a °C). Analysis is quick Python over the CSVs (run with `py -I`); the scripts live in the session scratchpad and don't survive.

### Results (Pokémon White 2, same route)

| Run | Settings | Judder | Work avg / p95 | Notes |
|---|---|---|---|---|
| 10-26 19:46 | 2x, before pacing | 13.1% | 12.8 / 15.2 ms | baseline |
| 10-26 20:57 | 2x, pacing + buffer | 0.82% | 12.9 / 15.3 ms | |
| 10-26 21:47 | 2x, + 60 Hz lock, charging | 0.65% | 13.7 / 17.6 ms | CPU capped to 1.75 GHz at 185 s, AP 43–46 °C |
| 10-26 21:58 | OpenGL 1x, charging | 0.11% | 14.0 / 16.3 ms | capped at 2.1–2.4 GHz throughout, still fine |
| 10-27 08:07 | OpenGL 1x, spin fix, **unplugged** | 0.49% | 13.5 / 16.4 ms | 234 s, no cap, AP peaked 36 °C, 0% judder after warm-up |
| 10-27 08:24 | same, **native -O2** (unplugged) | 0.04% | 10.2 / 11.9 ms | 712 s, unfolded at 435 s; no cap, AP 38 → 32 °C; 4 missed frames total, warm-up burst gone |

All runs before 08:24 used native code built at -O0, so their work times are inflated.

### What we know

- **The debug build used to compile native code at -O0.** AGP's Debug CMake type adds no -O flag. The debug build type in `app/build.gradle.kts` now passes `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (-O2 -g, asserts off). Work per frame dropped 13.5 → 10.2 ms, so there's ~6 ms of headroom now.
- **CPU profile on -O2** (10-27 08:46, 60 s, `perf_logs/o2_profile.data`): the emulator thread is ~60% of a core, about 10 ms per frame. Shares: 2D layers in software 30% (~3 ms/frame), memory/IO/DMA 13%, Adreno driver 10%, 3D geometry 8.5%, emutls + PLT + `pthread_getspecific` 7.8% (emutls is now gone with minSdk 37; ~3.4% `@plt` remains), timers/scheduler 6%, JIT game code 4%, JIT dispatch 4%, sound 3%. Audio thread 1.4%.
- **Recording a profile:** `simpleperf record --app me.magnum.melonds.dev --duration 60 -e cpu-clock -f 1000 -o /data/local/tmp/x.data`, then `simpleperf report -i ... --comms EmulatorThread --sort dso,symbol` on the device. The user remote-controls from the same phone, so start it from a script that waits for `EmulatorActivity` in `dumpsys activity activities`.
- **Thermal throttling** hit while charging on the -O0 build. The -O2 run (12 min, unplugged) had no cap.
- **Compute renderer** costs 1.5–2.5 ms more CPU than OpenGL. Sticking with OpenGL.
- Unfolding causes a one-off ~0.2 s stall (expected). Saving no longer causes slow frames. Memory is stable at ~1 GB RSS; graphics PSS grows over a session.

### Frame pacing code map

- `app/src/main/cpp/renderer/FramePacing.h` (`DisplayVsyncTracker`, `VsyncFramePacer`), `emulate()` in `MelonDSAndroidJNI.cpp`, `FrameQueue::getPresentFrame`, the Choreographer renderers.
- 60 Hz lock: `EmulatorActivity.updateDisplayModeLock`, `extensions/DisplayExtensions.kt`. Dropped during fast-forward.
- Settings: Video → Frame pacing (sync to refresh, extra frame buffer, lock 60 Hz). All on by default and apply live.
- Pacing only runs with no fast-forward, a recent vsync, and refresh/60 close to a whole number 1–4. Otherwise it falls back to the old timer.
- Untuned constants: 0.5 ms grid offset, 2% clamp, 1/8 gain, P/2 tolerance. Re-port `FramePacing.h` to a simulation if the logic changes.

### Open items

1. Phone tests with no code needed: a charging run on -O2 (thermal), and extra frame buffer on vs off (it adds ~17 ms of input lag; buffer-off judder is unmeasured).
2. Ideas if charging still throttles: `sustained_performance_mode`, ADPF hints, audio time-stretching.
3. Not measured on the device: the pacer with the 60 Hz lock off, front-buffer mode, an external display.
4. Only if a heavier game needs it: 2D rendering on its own thread or the GPU (~3 ms, big job in the core submodule).
5. Known issues left alone: the mic stream (enableMic/disableMic) isn't under `audioOutputMutex`; a FilterAudioStream (only if Oboe resamples) can be freed under its error thread; ADPF hints miss the inner stream when Oboe resamples; paced → timer fallback with the buffer on skips a frame; the summary's settings block is only read at session start; a save write that fails after open has already truncated the old file.
6. Upstream PRs maybe later: #1657 idle power (test fold), #1676 fast-forward rate matching, #1677 cheat search. Skip #1682 as a whole, #1666, #1524, #1606. Note: fast-forward pushes the DS clock ahead, and the forward-only sync leaves it there until the next boot/load.

## Working notes

- Coding goes through a subagent, and a separate fresh subagent reviews the diff from the code itself. Fixes go back to the coder, then build and install.
- The user plays on the phone, then reports back or asks for logs and screenshots.
