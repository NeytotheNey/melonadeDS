# Handoff: the polish pass (PLAN-polish.md)

Read this, then `PLAN-polish.md`. The plan has the per-item file references, the phase order and the verification list per phase. This file says where things stand, what is untested, and what the next session does first.

## Where things stand (2026-11-02)

- **Branch:** `claude/friendly-heisenberg-3eipz2` in this repo, on top of `material3`. Phases 1, 2 and 3 are applied and verified on the Z Fold 8 (the plan's status has what was checked). The core submodule is untouched by the pass.
- **Phase 4 (Kotlin consolidation):** applied in part, `f58360d9..812dc29d` (29 commits), written without the Android SDK. Every Kotlin file was type-checked with a standalone `kotlinc` and no Android classpath (recipe below), which caught one real error before it was committed and can prove nothing about code that touches Android types; three read-only reviews traced the rest by hand and found no behaviour change beyond the documented log-output changes. Built and verified on 2026-11-01: the debug build on the Z Fold 8 and a release build signed with the debug keystore on the OnePlus 7 Pro (`-Pandroid.injected.signing.*`, since this PC has no release keystore). The checklist below passes except an RA popup dismissing on stop and the unwritable-log-folder fallback, which weren't checked. For item 7 use `kill -9`: a recents swipe is a user-requested exit, which the reporter skips. The plan's status has the detail. A Lua ITCM check on the script's first frame fails in DS mode because the game hasn't enabled ITCM yet; check after boot.
- **Phase 4 rest:** applied on 2026-11-01, `043e6634..6c0217d6` (40 commits), with the SDK: compiles, 153 JVM tests pass, the debug APK builds, three reviews found no behaviour change. Smoke-tested on the Z Fold 8 (API 37) on 2026-11-01: items 1 and 2 of "Phone checklist for the rest of Phase 4" below pass, plus 2xBR/LCD/MMPX on OpenGL, the Mods tab rows, the 9-style page and a layout editor drag/resize/discard, with no crash or shader error. The rest of that checklist (3 to 7 in detail, compute, external display, pixel styles) isn't checked yet. `Failed to allocate memory using ftruncate! (Invalid argument)` logs once per game load and the game runs; not looked into. The plan's status has what was skipped and why.
- **Phases 5 to 7:** not started.

## What Phases 2 and 3 changed

Verified on the phone on 2026-11-01; the per-commit tables that used to be here are in the git history of this file (`git show f98472c2:HANDOFF-polish.md`). Two facts still worth knowing: every logcat tag is `melonade-*` now (`melonDS`, `melonDS-core`, `melonDS-lua` and `OpenGL` match nothing), and the Lua API lives in `LuaEngine.cpp` plus `LuaSandbox.cpp`, `LuaMemoryApi.cpp`, `LuaDrawApi.cpp`, `LuaFolderApi.cpp`, `LuaStateApi.cpp`, `LuaStorageApi.cpp` with the shared types in `LuaEngineInternal.h`.

## What Phase 4 changed, in the order it should be tested

Emulator and layout refactors (`f58360d9..22ca650c`):

| Commit | Change | What can break |
|---|---|---|
| `f58360d9`, `6044e486` | `FRAME_TEXTURE_HEIGHT` used directly in `ShaderProgramSource`/`VideoFilterPipeline` | Every video filter (the band constants are built from it) |
| `16985422` | `VSYNC_PERIOD_RANGE_NS` and `Long.asVsyncPeriodOrZero()` in `ChoreographerFrameRenderer.Companion` | Frame pacing on both Choreographer renderers, 60 Hz lock |
| `3423ec81` | `EmulatorSession.isHardcoreActive` | Savestate load, cheats and Lua scripts blocked in RA hardcore |
| `b419ea06` | `EmulatorViewModel.endSession()`; the RA reset event is now emitted after the native stop | Stop, stop-and-exit, reset; RA popups dismissed on stop |
| `3ae66bb6`, `84b24e7e` | `EmulatorManager.getLuaScript()`; one immutable `LuaScriptsState` in `AndroidEmulatorManager`, read without the lock | Mods tab status, disable/enable/restart a script, a script that errors out, two ROM loads in a row |
| `47a5e42c` | `getRewindWindow()` on `Dispatchers.IO` | Rewind |
| `0c295287` | `TouchscreenInputHandler.screenArea()` with a cached fallback `Rect` | Touch before the layout is applied |
| `aba2d91b`, `f6ad52c4` | `MIN/MAX_WIDE_VIEW_WIDTH` in `NDS.kt`; `consoleAspectRatio` in `ScreensDrawing` | Wide picks, swapped screens' letterbox |
| `22ca650c` | `LuaLayoutRequests.kt` split out of `LuaUiOverrides.kt` | Nothing visible |

Lua stores, settings and domain model (`c6989818..5b55d8ee`, plus `99c6572e`):

| Commit | Change | What can break |
|---|---|---|
| `c6989818` | Shared `NAME_ORDER`, `LUA_EXTENSION`/`isLuaScriptName`, `toShortHex()`, `LUA_LOG_TAG`; `REASON_*` built from the `MAX_*` constants | Script folder listing order, the "left out" reasons in the Mods tab |
| `37733a9e` | `LuaScriptHeader` in `domain/model/emulator`, `LuaScriptHeaderParser` in `impl` | Header parsing (options shown in the Mods tab) |
| `dea2f99e`, `9cf3914a`, `99c6572e` | `LuaScriptSetup.kt` split into five files; `LuaScriptOptionType(nativeId)` enum, `LuaScriptOptionValue.type` still the `Int` field native reads; ProGuard keeps for `LuaScriptOptionValue` and `LuaScriptDataSnapshot` | **Options of every type reaching `melon.options`, and `melon.storage` data surviving a restart, on a release build** |
| `0ef14f4f`, `e693e1e9`, `234035b1` | `SettingsBackupFormat.*_FILE` everywhere; one `putValue`; `SettingsBackupManager` bound once | Settings backup and restore, including a layout and a controller config; the 33→34 migration only on an old install |
| `5b55d8ee` | `PreferenceKeys` KDoc; `PreferenceKeysTest` parses `res/xml/pref_*.xml` and regexes the repository's defaults | The JVM test itself (it reads source as text; if it fails on a reformatted call, fix the regex or cut it to the key half) |

Diagnostics (`636e0a87..ba5dbd02`, plus `812dc29d`):

| Commit | Change | What can break |
|---|---|---|
| `636e0a87` | `LogDestination`, `getDefaultLogDirectory` and the MIME constants in `LogFolder`; `PerformanceLogger` takes a `LogFolder` | Perf log and debug log files landing in the picked folder, and the fallback when it isn't writable (the warning no longer prints the URI) |
| `16df7a0f`, `a10cb970` | `PerformanceSummary.kt`, `SessionStatistics.kt`; table-driven summary with a `# Keys` block; `PerfCsv` helpers | The CSVs must be byte-identical to a pre-change run; the summary keeps the same keys in the same order |
| `563a1596` | `PerformanceLogger.logStateChange` is the one façade, backed by `DebugLog`'s map | State lines (fold, display, pause) appearing once per change in each log, and replayed at session start |
| `86e8427c`, `812dc29d` | `DiagnosticsInfo.settings/app/device` shared by both logs; `supportsJit()` | The `config` lines and the header of the debug log (see the expected diff below) |
| `cd182c9f` | `impl/debug` + `impl/perf` → `impl/diagnostics`; `LogFolder` and `ExitReasonReporter` from `MelonModule`; `LogTags` | Hilt graph at startup (a missing binding fails the build, not the run); exit reasons reported at the next start after a crash |
| `ba5dbd02` | `DurationHistogramTest` | JVM tests only |

Expected output differences when diffing against a pre-Phase-4 run (anything else is a bug):

- Perf summary: a `# Keys` block of `# key: description` lines between `complete:` and `# App`; `# App` gains `app_upstream_version`, `app_installed`, `build_flavor`, `build_debuggable`, and `build_type` moves down two places; `# Device` gains `device_board`, `device_abis`, `device_fingerprint`, `android_security_patch`. Everything from `display_refresh_rate_hz` down is unchanged. CSVs identical.
- Debug log header: the `app:`/`build:`/`device:`/`abis:`/`android:`/`fingerprint:` prose lines become the same `key: value` lines as the perf summary; `device_soc` is absent below API 31 instead of `soc n/a`; `app_installed` has no `.SSS Z` suffix. `started:`, `pid:`, `command:`, `tags:` identical.
- Debug log `config` lines: `jit`→`jit_enabled`, `renderer`→`video_renderer`, `resolution_scaling`→`internal_resolution_scaling`, a new `default_console_type`, and the 17 shared settings emitted as one block after `device_64bit` in the perf summary's order. Values unchanged.

## Phone checklist for Phase 4

1. Debug and release builds (the release build matters this time: the ProGuard keeps), the JVM tests (expect 147 + 4 histogram + 2 `PreferenceKeysTest`, and `LuaScriptHeaderParserTest` replaces `LuaScriptHeaderTest`), `LuaStorageTest`.
2. A game under OpenGL and compute with a non-default filter; a Wide pick; swapped screens; touch before and after the layout settles.
3. Frame pacing on both renderers, 60 Hz lock on and off; rewind; reset; stop and stop-and-exit with an RA popup open (it must dismiss).
4. Mods tab: a folder with subfolders, a script that fails to load, disable/enable/restart, options of every type read from `melon.options`, `melon.storage` data surviving a restart, and the same on the release build. Load a second ROM after the first without leaving the app.
5. Settings backup, then restore on top (layouts, controller config, ROM data); RA hardcore blocks savestate load, cheats and scripts.
6. One perf log session and one debug log session with the log folder picked, and one each with no folder picked; diff against a pre-Phase-4 run using the list above. Fold and unfold during the session for the state lines.
7. Kill the app from the recents screen with the debug log on, start it again: the exit reason line appears.

## Phone checklist for the rest of Phase 4

1. Back on the Compose dialogs: pause menu → Mods, the script options dialog and achievements each close on back and resume the game (passes on API 37 with `KEYCODE_BACK`; gesture and controller B not tried). The pause menu's Mods entry only shows when a script is enabled for the ROM.
2. Lua scripts dialog: untick/retick a script, tick one that fails (error shows next time), Options… keeps the game paused through the handover and Save/Cancel resumes it, Close resumes, a long list scrolls, D-pad navigation.
3. Screen areas and settings: a Wide pick and a normal layout, swapped and not, screen alpha and on-top layouts, touch moves with the swap, back swipe blocked over the touch screen; the same on an external display. Change status bar, sustained performance, frame pacing, 60 Hz lock and FPS counter mid-game and return.
4. Every filter on OpenGL and compute (Sharp included, one upscaled), screen shadows on, a script drawing an overlay; no shader errors in logcat.
5. Layout editor: centre buttons on odd sizes, resize from all four handles at 4:3/16:9/Free/Wide locks down to the minimum, snapping to the other screen, tap vs small drag, the menu button moving out of the pill's way, 1x/2x/3x/Fit, grid on/off and size, overlay drawing, rotate/fold/plug a display while editing, a value dialog with the keyboard open, D-pad/ABXY spacing above 0 in game and in the editor.
6. All 9 controller styles in game, in the editor and in the style page, 8-bit and 16-bit shading, pressed looks, toggle buttons with off/dimmed icons; the chosen style survives the update and a fresh install shows "Classic".
7. ROM Mods tab looks as before: widescreen switch and caption, script rows with and without a description (same height), hardcore disables switches without greying names, the fixed aspect-ratio row, the folder row (2-line path, X removes, tap opens picker), the software-renderer note, "no wide layouts", the wide layout row disabled while loading. Lua choice and layout options: pick each, "Don't change", a deleted layout shows "Layout not found", unnamed layouts numbered.

## Phase 4 notes

- `getPlayerLayoutPicks` stays public and top-level in `LuaLayoutRequests.kt` because `LuaUiOverridesTest` calls it; move it into the VM only together with the test.
- `docs/polish-findings/*.md` still cite `impl/perf`, `impl/debug`, `impl/LuaScriptHeader.kt` and `LuaScriptOptionValue.TYPE_*`; they are the record of the review and go with the plan in Phase 7.
- Don't touch `app/src/main/cpp/lua/` (vendored) or the submodules.

## Type-checking Kotlin without the SDK

`dl.google.com` is blocked from the cloud container, so no SDK; Maven Central is reachable (rate-limited, retry) and has the compiler. Fetch `kotlin-compiler-embeddable`, `kotlin-stdlib`, `kotlin-script-runtime`, `kotlin-reflect`, `kotlin-daemon-embeddable` (all at the version in `gradle/libs.versions.toml`), `org.jetbrains.intellij.deps:trove4j:1.0.20200330`, `org.jetbrains:annotations:13.0` and `kotlinx-coroutines-core-jvm` from `https://repo.maven.apache.org/maven2/`, then:

```bash
find app/src/main/java app/src/test/java -name '*.kt' > files.txt
java -Xmx6g -cp '<all the jars>' org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -no-stdlib -no-reflect \
  -cp kotlin-stdlib.jar:kotlinx-coroutines-core-jvm.jar -jvm-target 17 -d /tmp/classes @files.txt > out.txt 2>&1
```

It reports about 18,000 errors, nearly all unresolved Android, Compose, Hilt and AndroidX references. Run it once on the unchanged tree, once after the change, normalise (`sed -E 's#^.*/([A-Za-z0-9_]+\.kt):[0-9]+:[0-9]+:#\1:#'`, sort, uniq) and `comm -13` the two: new lines that aren't `unresolved reference` to an Android name are real (a `'val' cannot be reassigned` from a shadowed parameter was caught this way). It takes about 40 s. Inference failures that cascade from an unresolved Android type also show up as new lines in new files, so read the list rather than counting it.

## Syntax-checking native code without the SDK

Worked in the cloud container for Phases 2 and 3 and is worth repeating for any later native change. Needs the `melonDS-android-lib` and `app/src/main/cpp/oboe` submodules checked out (`git submodule update --init --depth 1 <path>`), `apt-get install libgles-dev libegl-dev`, a JDK for `jni.h`, and a stub directory with `android/log.h`, `sys/system_properties.h` (`PROP_VALUE_MAX 92`, `PROP_NAME_MAX 32`, `__system_property_get`), `android/asset_manager*.h`, `android/api-level.h`, `libslirp.h` (typedef `Slirp`, `SlirpCb`), `pcap/pcap.h`, and a `version.h` made from the core's `version.h.in`. Then:

```bash
clang++ -std=c++20 -fsyntax-only -w -include sys/types.h -include cstring -include condition_variable -include atomic -include memory -include cmath \
  -I <stubs> -I <jdk>/include -I <jdk>/include/linux -I app/src/main/cpp -I melonDS-android-lib/src -I melonDS-android-lib/src/frontend \
  -I melonDS-android-lib/src/net -I app/src/main/cpp/oboe/include -I app/src/main/cpp/lua -I app/src/main/cpp/rcheevos/include \
  -I app/src/main/cpp/faad2/include -I app/src/main/cpp/enet/include -DOGLRENDERER_ENABLED '-DMELONDS_GL_HEADER=<GLES3/gl32.h>' \
  -DEGL_EGLEXT_PROTOTYPES -DANDROID -D__ANDROID__ [-DMELONADE_DEBUG_TOOLS] app/src/main/cpp/<file>.cpp
```

The forced `-include`s stand in for headers the NDK pulls in transitively; they are not missing includes to fix. `LuaStorageTest` builds and runs on Linux with the recipe in MODS.md ("Testing") as it stands, `gcc`/`g++` for the MinGW ones.

## Housekeeping

- This file and `PLAN-polish.md` move under `docs/` in Phase 7, with the other plan and handoff files.
- `docs/polish-findings/` is the working list for Phases 3 to 6 and goes with the plan.
