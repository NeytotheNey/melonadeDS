# Plan: cleanup and polish pass over `material3`

## Status (2026-11-01)

Phase 1 applied on `claude/friendly-heisenberg-3eipz2` (20 commits, one per item group, from "Remove the gitHub flavour's dead update-install path" to "List Lua and credit WideMelon in the open source licences"). Verified 2026-11-01 on the Z Fold 8: debug build, 147 JVM tests and `LuaStorageTest` pass; reset, rewind load, fast-forward, a 16:10 Wide pick, all four button styles, a Lua options save from the pause menu, the Lua/WideMelon licence entries and the dropped install permission all check out, with no crashes or hitches. The release build compiles and minifies but wasn't signed (no keystore in `local.properties` on this PC). Two departures from the list: `MelonDSAndroidJNI.cpp` keeps a `<ctime>` include, since `clock_gettime` needs it, and the Lua licence text comes from `lua.h`, not `cpp/lua/LICENSE`, which is a provenance note. 

Phase 2 applied on the same branch (nine commits, from "Gather the debug.melonade.* switches in one DebugProperties table" to "Check the hand-mirrored native layouts at startup"), again without the Android SDK: every touched native file passes a host `clang++ -fsyntax-only` in the debug and release configurations, but the build, the JVM tests and the phone checks are still owed. What to check on the phone: each `setprop debug.melonade.*` switch still flips its target (the summary line is now under the `melonade-debug` tag), fast-forward on/off and a configuration change while fast-forwarding, a pause and resume with frame pacing on, the audio latency setting (now an enum on the native side), a debug log session (the tags are all `melonade-*` now, so any logcat filter on `melonDS` needs updating; `emu.sh log` was updated) and a perf log session. One departure from the list: the achieved-speed meter moved into `AudioDiagnostics` rather than only losing its narration. Phase 3 applied on the same branch (nine commits, from "Add a LOG_DEBUGLOG macro for debug-log-only lines" to "Move the LuaStorageTest build recipe to MODS.md"), again syntax-checked with the host `clang++` only, in both configurations, for every file that includes the changed headers. `LuaStorageTest` was built with the recipe and passes on Linux. What to check on the phone: a script that uses every part of the module (memory reads and writes on both CPUs, every `melon.draw` call, `require` and both kinds of image, `melon.state` save and load, `melon.storage`, a screen and a fast-forward request, `set_layout`), one that hits a limit (a long loop, a big `string.rep`, over 4096 draws) and is switched off with the right message, the options table, and a disable/enable/restart from the Mods tab. Three departures from the list: the API files are structs of static functions that `LuaEngine` befriends (a namespace of free functions can't be a friend, and the functions read the engine's private state through `LuaScript::engine`); the tables they expose are registered with `lua_newtable` + `luaL_setfuncs` rather than `luaL_newlib`, since the array size isn't visible to `setupScript`; and `kMaxImageOffset` went to `LuaDrawApi.cpp` (only `melon.draw.image` uses it). Phases 4 to 7 are not started.

Five read-only reviews covered the whole branch against its fork point `c42995ca` (88 commits, 644 files, +63k/-10k): the emulator UI and GL code, the layout editor and shared views, settings/impl/perf logging and tests, the native code (excluding vendored Lua and the submodules), and resources/build/scripts/docs. Every "unused" claim below was grep-verified by the reviewer; line numbers are against the `material3` tip `fff6a01e`.

Headline numbers:

| What | Count |
|---|---|
| Multi-sentence comment blocks added by the branch (Kotlin + C++) | ~450 in 125 files |
| Of those, in native code | ~390 |
| Files over 1,400 lines that should split | 3 (`LuaEngine.cpp` 2170, `PerformanceLogger.kt` 1657, `EmulatorActivity.kt` 1443) |
| Debug `debug.melonade.*` switches wired by copy-paste | 14 |
| Parallel diagnostics paths (perf log, debug log, audio diagnostics) that overlap | 3 |
| Root-level plan/handoff markdown files | 7 of 9 docs |

Lint could not run in the review container (no Android SDK), so the resource sweep was done by hand.

## Ground rules for the pass

1. One concern per commit, in the phase order below, so each can be built and phone-tested on its own.
2. Comments: keep the first sentence, delete anything that narrates history ("used to", "as before X", "for the time-stretching to come"), restates the code, or cites a plan doc. Rationale that still matters goes in the commit message. Doc comments included.
3. No behaviour changes except the three correctness fixes called out in Phase 1, which are tiny and isolated.
4. Spelling in identifiers: American (`center`, `color`), matching upstream melonDS-android. UI strings: British, matching the About screen and upstream's `favourite colour`.
5. Native constants: `UPPER_SNAKE` in frontend files; `kCamel` may stay inside `scripting/` only.
6. Don't touch `app/src/main/cpp/lua/` (vendored) or the submodules.

## Phases

### Phase 1: delete and fix (S, one day)

Dead code and the few correctness smells. No design decisions needed.

**Delete**
- `impl/SettingsBackupFormat.kt:46-50`: the four `github_updates_*` entries in `KEPT_PREF_KEYS` can never match (those keys lived in a separate `preferences-github` file and nothing writes them any more).
- The whole update-install path of the gitHub flavour, now that `DisabledUpdatesRepository` is bound: `GitHubModule.provideGitHubApi`, `GitHubApi.kt`, the two "Not used" `GitHub*UpdatesRepository` classes, `GitHubUpdateInstallManager`, `UpdateContentProvider`, `app/src/gitHub/AndroidManifest.xml` (`REQUEST_INSTALL_PACKAGES` + provider), `provider_paths.xml`, the `check_for_updates*` strings and the Retrofit dependency. Release APKs currently ask for an install permission they never use.
- `SaveManager::NeedsFlush()` (`SaveManager.cpp:269`, `.h:44`): rewritten on the branch, no caller left.
- `ExitReasonReporter.kt:45-47`: the `isDebugLogEnabled()` re-check and the `settingsRepository` parameter that exists only for it.
- `LogFolder.LogFile` (`LogFolder.kt:22`): single-field wrapper, return the `OutputStream`.
- `SettingsBackupManager` double binding (`@Inject constructor` + `MelonModule.provideSettingsBackupManager`): keep `@Provides`, matching `PerformanceLogger`.
- `ControllerAppearance.ColorGroup` (single entry `DPAD`): replace with `isDpad: Boolean` on `RaisedButtonView.Part`.
- Unused imports: `DSRenderer.kt:15` (`SCREEN_HEIGHT`), `EmulatorViewModel.kt:4` (`Log`), `MelonDSAndroidJNI.cpp:5-10` (`<stdlib.h>`, `<sstream>`, `<time.h>`).
- Leftover markers: `ShaderProgramSource.kt:268-269` (`// What is this?` x2), `DSRenderer.kt:395` TODO (rename or drop), `MelonInstance.cpp:257` (`// TODO: Re-enable` DLDI line), `OboeCallback.cpp:318-319` (commented-out speed factor), `PageListPreference.kt:14-15` (two stacked no-op comments).
- Strings: `check_for_updates*`; the 17 `multiplayer_*`/wifi strings (dead since upstream) unless local multiplayer is planned; `pref_system.xml:9` stray `android:key="internal_firmware_settings"`.
- Upstream leftovers under `.github/`: `release-playstore.yaml` and the `playstore-release` job, the 2.0.1 changelogs that `release.yaml:54` would publish as 1.0.0 notes, `FUNDING.yml` (points at rafaelvcaetano), the issue template's upstream Discussions link.
- `LayoutEditorView.kt:71` `minComponentSize` back to `private`; `LayoutEditorItemControls.kt:67-68` keep only `marginPx`; `formatSize()` (`:393`) is `toString()`; `LayoutEditorSnapping.gridOrigin` inlined; `PixelIcon.withCompact()` folded into `icon(rows, compact)`; `LayoutEditorActivity.kt:101` `controllerStyle` field.
- `PerformanceLogger.getDisplayRefreshRate()` one-line wrapper (`:881`).

**Fix (confident correctness smells)**
- `MelonDSAndroidJNI.cpp:536,616`: `while (!isThreadReallyPaused);` busy-spins in `resetEmulation` and `loadRewindState`; route both through the new `pauseAndWaitForEmulatorThread()`, which yields.
- `PerformanceLogger.startSession` (`:106-135`): when it gives up on a stuck previous session it leaves `currentSession` set, so `isSessionActive()` stays true and the view model never retries. Clear it.
- `EmulatorViewModel.kt:1083`: `setLuaScriptOptions` uses `romsRepository.getRomAtUri` where every other Lua config save uses `getLibraryRom` (whose KDoc says `getRomAtUri` re-reads and unpacks the file and returns a ROM whose config update is a no-op).
- `LayoutEditorItemControls.kt:464,470`: `WidescreenAspect.ordinal` as `PopupMenu` item id gives `RATIO_16_10` id `0 == Menu.NONE`; use `ordinal + 1`.
- `RomDetailsViewModel.kt:293-300`: re-picking the same Lua folder swallows a failed `takeLuaScriptFolderPermission` and syncs anyway; report it like the new-folder branch.
- `RaisedButtonView.kt:89-92`: icon tint is set once in `setParts`; for `SOFT_TINT`/`OUTLINE` the pressed face differs and the tint never follows; for pixel styles it is dead work. Tint in `drawFace`, skip for pixel.
- Stale comments that mislead: `MelonInstance.h:52` and `Configuration.h:94` say the wide view is "OpenGL only"; `runFrame()`/`updateRenderer()` apply it to the compute renderer too. `PerformanceLogger.kt:197` ("until the display samples carry their own vsync period") is already true.
- `app/config/libraries/lua.json` is missing: the About screen's licence list claims to cover native code but omits Lua 5.4.9 (MIT, text at `cpp/lua/LICENSE`). Add it, plus a WideMelon credit in `melonds-android.json`.

### Phase 2: native consolidation (M, two to three days)

- **`debug/DebugProperties.{h,cpp}`**: one table `{ name, default, target }` for the fourteen `debug.melonade.*` switches, replacing twelve copy-pasted `__system_property_get` blocks in the `MelonInstance` constructor (`MelonInstance.cpp:72-170`), the two in `MelonDSAndroidJNI::debugPinThreadToCpus`, and the `threaded2dOverride`/`hiresCaptureOverride` members (`MelonInstance.h:170-173`, guard them with `MELONADE_DEBUG_TOOLS` like the rest). One doc block lists every switch; the nine "as before X, for A/B tests" comments go.
- **`MelonDSAndroidJNI.cpp` dedupe**: `applyFastForwardPacing(enabled, speedMultiplier)` for the twin 15-line blocks at `:770-795` and `:813-828`; `FramePacingState::reset()` for the pause path at `:1098-1103` that hand-copies `unlockPacingFromRunning`; `getCurrentMillis()` and `getCurrentNanos()` both `static`.
- **Clocks and logging**: one `inline int64_t monotonicNanos()` replacing five copies (`DebugLog.cpp:31`, `AudioDiagnostics.cpp:91`, `AudioDiagnostics.h:134`, `MelonDSAndroidJNI.cpp:847,853`); one tag header under one scheme (`melonade-*`; today `melonDS`, `melonDS-core`, `melonDS-lua`, `melonade-cpu`, `melonade-audio`, `OpenGL` coexist, also on the Kotlin side `LuaScriptDataStore.kt:116`, `LuaScriptStorage.kt:262`); use the existing `MelonLog.h` `LOG_*` macros in `DebugLog.cpp`, `AudioDiagnostics.cpp`, `MelonInstance.cpp` and the JNI file instead of raw `__android_log_print`; debug-only `CAPWAIT`/`REWIND`/pin lines get a `melonade-debug` tag instead of the core's.
- **`FramePacing.h`** (384 lines, header-only): declarations stay, method bodies move to `renderer/FramePacing.cpp`; `EMULATOR_TARGET_FPS` moves to `Configuration.h` (today `MelonDSAudio.cpp` includes the pacing header only for it).
- **`MelonDSAudio.cpp`**: all file-scope state `static` (seven new atomics and two globals currently have external linkage); `enum class AudioLatency { Low, Medium, High }` in `Configuration.h` instead of the magic-int `switch` duplicated at `MelonDSAudio.cpp:120` and `AudioDiagnostics.cpp:103`; the "achieved speed" meter (`AchievedSpeedMeter`, `publishAchievedSpeed`, `onEmulatorFrameEnd`) is consumed only by diagnostics, move it next to them or at least strip the "for the time-stretching to come" narration.
- **Header hygiene**: `MelonInstance.h:19` includes `debug/DebugLog.h` (and through it `NDS.h`) for one inline function; move `logDebugFrameState()` to the .cpp. `DebugLog.h:7` forward-declares `melonDS::NDS` instead of including `NDS.h`.
- **Small helpers**: `sanitizeAscii()` for the two identical loops in `DebugLog.cpp:411-420`; one "n/a or number" formatter for the four `snprintf` pairs in `AudioDiagnostics.cpp:402-449`; `PackedTouch { pack(); unpack(); }` for the `kUserTouchDown | x << 16 | y` word spelled out in four places in `MelonInstance.cpp`.
- **Perf sample layout** (`MelonDSAndroidJNI.cpp:78-96`, `AudioDiagnostics.h:26-63`, `MelonEmulator.kt:22-61`): hand-mirrored with "must match" comments on both sides (consistent today). Expose the field count through a JNI getter and `check()` it at startup, same for `EmulatorMessageQueue.DATA_SIZE_BYTES` vs `kMaxDataLength`.
- **Constant naming**: rename the outliers to `UPPER_SNAKE` in frontend files (`kUserTouchDown`, `kMinDriftSeconds`, `kMaxDataLength`, SaveManager's `MaxRetryDelayTicks`/`FlushFailureLogInterval`).

### Phase 3: split `LuaEngine.cpp` (L, two to three days)

`scripting/LuaEngine.cpp` is 2170 lines holding the sandbox, raw memory API, lifecycle, 150 lines of registration tables, the draw API, `require`/images, `melon.state` and `melon.storage`.

- Split by namespace, each file exposing a `luaL_Reg` table: `LuaSandbox.cpp` (allocator, hooks, work budget, `wrapFunction`/`safe*`), `LuaMemoryApi.cpp`, `LuaDrawApi.cpp`, `LuaFolderApi.cpp` (require + image), `LuaStateApi.cpp`, `LuaStorageApi.cpp`; `LuaEngine.cpp` keeps lifecycle and `setupScript`. `Script` and `DrawCommand` (public today only so free functions can name them, `LuaEngine.h:168`) move to a private `LuaEngineInternal.h`.
- Fold the marshalling boilerplate while moving it: `setBoolRequest(lua_State*, std::optional<bool> Script::*)` for `luaScreenSwap`/`luaScreenShowControls`/`luaEmuFastForward` (`:1371-1391`); `queueScriptSwitch(id, action, options)` for `disableScript`/`enableScript`/`restartScript` (`:750-770`); `Script::clearRequests()` (duplicated in `sendRequests` and `closeScript`); `[[noreturn]] raiseLimitError()` for the four `failRestOfCall` + `luaL_error` pairs; a `findInFolder(map, name, len)` template for `findModule`/`findImage`; `addDrawCommand` taking geometry and colour so the three initialisers at `:1629-1727` collapse; a `LOG_DEBUGLOG(TAG, ...)` macro for the five `if (DebugLog::isEnabled()) LOG_INFO` pairs; one helper (or `noexcept` host methods) for the two identical `bad_alloc` wrappers at `:616` and `:1454`.
- `LuaScriptingJNI.cpp:86-108`: option types are bare `0,1,2,4` with `default` standing in for String; named constants matching Kotlin's `TYPE_*` (which in turn should become `enum class LuaScriptOptionType(nativeId)` mapped at the JNI boundary, see Phase 5).
- `LuaEngine.h:137-164`: the 28-line class comment restates the API that `MODS.md` documents; two sentences (what it owns, which thread each method runs on) plus a pointer to MODS.md.
- `app/src/test/cpp/LuaStorageTest.cpp:1-10`: the MSYS2 build recipe moves to MODS.md "Testing".

### Phase 4: Kotlin consolidation (M to L, three to four days)

**Diagnostics (perf log + debug log)**
- Split `impl/perf/PerformanceLogger.kt` (1657 lines) into `PerformanceLogger.kt` (session + CSV writers), `PerformanceSummary.kt` (`buildSummary`, `:619-856`, 240 `line(...)` calls; make it table-driven so each metric's description is emitted into the summary header instead of living in comments), `SessionStatistics.kt` (+ `RefreshRateStatistics`, `DurationHistogram`, already `internal`) and a shared log-destination file.
- One log-folder resolver: `PerformanceLogger.resolveDestination`/`DocumentTreeDestination` (`:558-579, 915-947`) duplicates `impl/debug/LogFolder.kt:29-52`, which depends back on `PerformanceLogger.getDefaultLogDirectory`. Move `LogDestination` and the default directory into `LogFolder`.
- One state-change map: `PerformanceLogger.logStateChange`/`lastStates` and `DebugLog.stateChange`/`currentStates` are the same "emit on change, replay at session start" logic, and `EmulatorActivity.kt:812-813, 846-847` call both back to back with identical strings. One `logStateChange(key, value)` façade that fans out, or `PerformanceLogger` reads `DebugLog`'s map at `startSession`.
- One settings/device snapshot: `PerformanceLogger.buildSettingsSnapshot` (`:591-617`) and `DebugLog.logConfiguration` (`:120-154`) are two hand-kept lists of the same settings with diverging key names (`video_renderer`/`renderer`, `jit_enabled`/`jit`); the App/Device header is gathered twice (`PerformanceLogger:638-655`, `DebugLogRecorder.buildHeader:159-186`).
- Then merge `impl/debug` and `impl/perf` into one `impl/diagnostics` package; `DebugLogRecorder.kt:46-47` builds `LogFolder` and `ExitReasonReporter` by hand, provide them from `MelonModule`.
- Small: `appendSeconds(ns)` for the three copies of the "µs as ms gives seconds" trick (`:429,443,469`); `appendFlag(Boolean)` for the eleven `if (flag) '1' else '0'`; `supportsJit()` for the three `SUPPORTED_64_BIT_ABIS` checks; `AndroidEmulatorManager.getRewindWindow()` uses `Dispatchers.Default` where every other blocking JNI call uses `IO`.

**Emulator UI**
- `EmulatorActivity.updateRendererScreenAreas()` (`:1112-1140`) and `ExternalPresentation` (`:95-120`) are a near-verbatim pair the branch grew by two more parameters, each with its own cached `isWideLayout` (Activity:191, Presentation:42). Move it into `RuntimeLayoutView.applyScreenAreasTo(renderer)`.
- `EmulatorViewModel`: `endSession()` for the teardown repeated in `stopEmulator():482`, `stopEmulatorAndExit():494`, `resetEmulatorState():763`; `EmulatorManager.getLuaScript(id)` instead of building the full status list at `:1046,1068,1080,1122`.
- `AndroidEmulatorManager.kt:66-88`: six `@Volatile` Lua fields under `luaScriptsLock` reset together in `clearLuaScripts()`; one immutable `LuaScriptsState` swapped atomically, `getLuaScripts()` lock-free.
- `EmulatorActivity.kt:1257-1340`: the hand-built Lua scripts dialog (fully-qualified `android.R.layout.*`, manual ripple) becomes a Compose dialog next to `LuaScriptOptionsDialog`.
- Five one-line setting passthroughs in the VM (`:1137-1155`) each re-read by a `setupX()` in the Activity from three call sites: one `EmulatorScreenSettings` data class and one `applyScreenSettings()`.
- `EmulatorSession.kt:64-77`: `areSaveStateLoadsAllowed`/`areCheatsEnabled`/`areLuaScriptsAllowed` share one body; one `isHardcoreActive` getter.
- GL: `MIN/MAX_VSYNC_PERIOD_NS` declared in both Choreographer renderers; `LOGICAL_TEXTURE_HEIGHT`/`NATIVE_TEXTURE_HEIGHT` are private aliases of `FRAME_TEXTURE_HEIGHT`; `renderScreenShadows()` hand-writes the attrib pointers `setScreenVertexAttributes()` wraps; `useScreenShader`/`useOverlayShader` share a preamble; every filter in `ShaderProgramSource.kt` repeats the same three-uniform preamble (one `SCREEN_UNIFORMS` const), and `SCREEN_BAND_FUNCTIONS`/`_300` differ only by `texture2D` vs `texture`.
- `LuaUiOverrides.kt` holds six things; move `LuaUiSetting` into `ui/emulator/model` (its `model` package imports it from the parent today), split `LuaLayoutRequests` out, `getPlayerLayoutPicks` into the VM. Consider a `LuaSessionController` for the VM's Lua state (channel, session counter, layout requests) to get the VM under 1,000 lines.

**Layout editor and shared views**
- Snapping/centre math written twice: `LayoutEditorView.clampInside` vs `LayoutEditorSnapping.clamp`; centre buttons vs `centeredStart`; `max(minComponentSize, minComponentSize * ratio)` in `snapResize` and `constrainSize` (comment admits "Same minimum as constrainSize"); the other-screen `when` block in `getSnapAxes` and `setSelectedScreenOnTop`; five `Anchor.isLeft/isTop` ternaries; touch-slop detection in both the resize gesture and the drag handler; the menu-button overlap test twice in `LayoutEditorItemControls` (`:550, :604`).
- `Pair<Int, Int>` sizes through six functions: a `Size` data class.
- Aspect-ratio lock state is split between `LayoutEditorItemControls` (`ScreenAspectRatio`/`lockedRatio`) and `LayoutEditorView.selectedScreenAspectRatio`, reconciled in `setPropertyValue`; the editor view owns `selectedView`, so it should own the enum too.
- `LayoutEditorActivity.kt:164-176, 317-350`: nine `latest*`/`reported*` fields as a change detector; one `LayoutVariantInputs` data class.
- `LayoutEditorView` (900 lines): extract a `SnapGrid` holder (`:109-153`) and a `LayoutEditorOverlayDrawer` (`:273-349`); `openGridDialog` is the last View-binding dialog among Compose ones.
- `PixelButtonPainter`: `ICON_SIZE = 0.55f` duplicated from `RaisedButtonView` (the comment says so); nominal detail size computed twice; `labelCentre`/`iconCentre`/dimple centre share one px-to-detail conversion; `fitScale` and `stamp` recompute the same placement; `buildPart` is 165 lines with 7 local functions (extract a `PartShader`); four hand-rolled `Mask` loops; the toggle icon rule `if (isToggledOn) icon else offIcon ?: icon` in two files. `ControllerAppearance.pixel()` builds placeholder `ControllerButtonColors` its own comment calls unused: make `ControllerAppearance` a sealed `Flat(colors)`/`Pixel(art)`. The 8-bit palette is the 16-bit one minus glint/lip; derive it.
- `RaisedButtons.dpad()/abxy()` build parts at spacing 0, then `applySpacing` rebuilds them; pass spacing in.

**Compose rows**
- `LuaScriptOptionControl.ChoiceControl`/`LayoutControl` (`:141-254`) are one `DropdownControl`.
- `RomModsUi.WidescreenSwitchItem`/`LuaScriptItem`/`InfoItem`/`ScriptFolderItem` duplicate `ui/common/preference` `SwitchItem`/`ActionLauncherItem`, which only need `caption`/`enabled` params; plus a `SectionTitle` and `SectionNote` for the two-times and four-times repeated header/caption blocks. About 150 lines go.
- `layoutDisplayName(name, unnamedNumber)` once (today `RomModsUi` reuses `lua_script_option_layout_unnamed` for a wide layout; rename to `layout_unnamed`).

**Settings**
- `ControllerStyle` default and lower-case encoding spelled out in four places (`pref_input.xml:38`, `InputPreferencesFragment:64`, `ControllerStylePreferencesFragment:100,120`, `SharedPreferencesSettingsRepository:551`) plus two parallel arrays in `arrays.xml`/`strings.xml` with a "never reorder" warning: `ControllerStyle.DEFAULT`, `toPreferenceValue()`, and entries built from the enum in the (already custom) `PageListPreference` page.
- `PreferenceKeys` vs XML literals: state the rule once ("fork-added keys only") and replace the keys that appear both as constant and literal (`frame_pacing_*`, `mute_fast_forward_audio`, `sync_rtc_to_device`).
- `"controller_config.json"`, `"layouts.json"`, `"rom_data.json"` defined in several places; reference `SettingsBackupFormat.*_FILE`. `PendingSettingsRestore.putValue` vs `SettingsBackupFormat.putTypedPreference` are two typed-put switches.
- `LUA_EXTENSION`, `NAME_ORDER`, the short-hex helper and the `melonDS-lua` tag each exist twice across `LuaScriptStorage`/`LuaScriptFolderContent`/`LuaScriptFolderSync`/`LuaScriptDataStore`; `REASON_*` strings hardcode the numbers the `MAX_*` constants hold.

**Domain/package placement**
- `impl/LuaScriptHeader.kt` is imported by `RomModsUi`, `RomLuaScriptsUiModel` and `RomDetailsViewModel`: move the data class to `domain/model/emulator`, keep the parser in `impl`.
- `domain/model/emulator/LuaScriptSetup.kt` holds five unrelated JNI structs; split or rename with one header. `LuaScriptOptionValue.TYPE_*` become an enum mapped at the JNI boundary.
- `MIN/MAX_WIDE_VIEW_WIDTH` (in `rom.config`) and the JNI-coupled `NDS.kt` constants (`SCREEN_GAP`, `FRAME_TEXTURE_HEIGHT`, `EMULATOR_TARGET_FPS`, each with a "must match X.cpp" comment) belong together next to the other native-facing constants. `ScreensDrawing`/`WideTopScreen` recompute `SCREEN_WIDTH / SCREEN_HEIGHT.toFloat()` although `consoleAspectRatio` exists.
- `RomConfigUiModel.isWidescreenSupported`/`wideLayouts` are app-wide facts in a per-ROM model; a sibling `RomModsUiModel`.

### Phase 5: resources, strings, build (S to M, one day)

- Preference row layouts: the title/summary `TextView` pair is written three times (`preference_card.xml:38-58`, `preference_top_level.xml:41-61`, `preference_slider.xml:24-43`); one `<include>`. The chevron widget is repeated on 9 rows and the directory-picker trio (`type`/`permissions`/`persistPermissions`) on 4; set both as defaults in `StoragePickerPreference` and `BasePreferenceFragment` (for `fragment != null`). `view_layout_editor_manager.xml` has five identical slider rows; one included row.
- Strings: one `percent_value` (two identical `%1$d%%`); one "Mods"; drop the duplicated sentence between `threaded_rendering_software_only` and `threaded_rendering_summary`; `settings_performance_summary` still says "threaded rendering" (now in Display) and `settings_audio_summary` names the hidden latency setting; the copied "Keep words together because this is the official name" comment makes no sense on "Account, hardcore mode, indicators"; US spelling in `dynamic_colors`/`settings_appearance_and_backup_summary` vs British elsewhere.
- `settings_audio_latency_enabled=false` hides a setting the native side still applies (`MelonDSAndroidConfiguration.cpp:49`); with PLAN-audio's automatic sizing, decide whether the pref, two arrays, two strings and the switch go.
- `pref_retroachievements.xml` uses `app:dependency` where every other screen uses `android:dependency`; `pref_video.xml`/`pref_cheats.xml` lack the `xmlns:app` header their rewritten siblings carry.
- `colors.xml:39` `textLayoutEditor` and `layoutEditorPanelBackground` are fixed colours on an otherwise dynamic-colour UI; consider `?attr/colorOnSurface`/`colorSurface` with alpha.
- `.claude/emulator/emu.sh` hard-codes `C:/Tools/android-sdk`, `C:/Dev/melonDS-android`, a JDK path and is mode 644; derive from the script location, `ANDROID_HOME`, `JAVA_HOME`, `chmod +x`. `.claude/agents/emulator-tester.md` re-documents every `emu.sh` command (already drifting from `emu.sh help`) and names the user's OneDrive Games folder; keep the usage rules, have the agent run `emu.sh help`. `.claude/shadercheck/gl_dump.cpp`/`compute_dump.cpp` share a writer and hand-copy the compute define list; merge.
- `build.gradle.kts`/`CMakeLists.txt`/`styles.xml`/`colors.xml`/layout XML comments: a dozen two-to-three sentence comments, all with one-sentence replacements in the resources findings.
- Tests (cheap, JVM): a `PreferenceKeys` ↔ `res/xml/pref_*.xml` consistency test (every constant is an `android:key`, every XML default equals the repository's hardcoded default; the 14 agree today by discipline only); `DurationHistogram` percentile/overflow; `SessionStatistics.trackFrame`/`trackAudioFrame` exclusion rules and sentinels; `LayoutConfigurationDto` legacy `wideAspect` copy and spacing clamp round-trips; `SettingsBackupFormat.getBackupLong`/`Float`/`Set`. Move the two `RomModsConfigDto` tests out of `LuaScriptDataStoreTest`.

### Phase 6: comment pass (M, mechanical, one to two days, last so it doesn't conflict)

About 450 multi-sentence blocks. Worst files: `LayoutEditorView.kt` 19, `PerformanceLogger.kt` 18 (31 by the settings reviewer's count including KDoc), `PixelButtonPainter.kt` 17, `AudioDiagnostics.h` 14, `MelonEmulator.kt` 14, `MelonDSAndroidJNI.cpp` 13, `ShaderProgramSource.kt` 13, `EmulatorActivity.kt` 13, `LuaEngine.cpp` 50 and `LuaEngine.h` 26 (native count), `MelonDSAudio.cpp` 28, `MelonInstance.cpp` 27, `FramePacing.h` 21.

Rule: keep the first sentence. Each area's findings file lists a one-sentence replacement for every block, so this is apply-and-review, not authoring. Specific deletions:
- History/plan narration: `MelonDSAudio.h:49-59`, `OboeCallback.cpp:150, 210`, `OboeCallback.h:97`, `MelonDSAudio.cpp:105, 200` ("as before priming existed", "for the time-stretching to come"); `MelonInstance.cpp:78-130` (nine "as before X, for A/B tests"); `EmulatorActivity.kt:977-982` ("hasn't been verified on every device"), `:1265-1267` ("the checkbox row the old list used"); `LuaUiOverrides.kt:146` ("Kept apart because..."); `LuaScriptStorage.kt:237` ("Older versions copied..."; better, move the call into a migration); `LayoutConfigurationDto.kt:24`; `ic_launcher_foreground.xml:12` ("instead of at 36,34"); `PerformanceLogger.kt:123, 258` (restate the call).
- Collapse repeated notes: six "Missing in configs saved before X" DTO field comments into one class-level sentence; `SettingsBackupFormat.kt:53` and `:107` say the same thing; `colors.xml` and `values-night/colors.xml` carry the same two-sentence header; `styles.xml:125` and `preference_widget_material_switch.xml:2` repeat the switch-id note; `SettingsBackupManager` and `PendingSettingsRestore` KDocs both explain the two-step restore (keep it in `SettingsBackupFormat`).
- Essays that should be one class-level paragraph: `FramePacing.h` per-method algorithm (`:209-223` is 15 lines), `LuaEngine.h:137-164`, `LuaDraw.h:12-19`, `AudioDiagnostics.h:10-17`, `PreferenceCardDecoration.kt:15-24`, `LuaScriptHeader.kt:5-21` (keep the example block, it is the spec), `LuaScriptFolderContent.kt:3-13`, `PendingSettingsRestore.kt:20-30`, `DebugLogRecorder.kt:26-31`, `ExitReasonReporter.kt:19-25`, `MultiButtonInputHandler.kt:12-19`, `FrameRenderCallback.kt:7-19`, `ScreensDrawing.kt:29-42`, `WidescreenLaunch.kt:7-17`, `ControllerAppearance.kt:32-37`.
- `@param`/`@property` blocks: one clause each (`LayoutEditorSnapping.kt:23-33`, `EmulatorManager.kt:22-29`, `EmulatorAudioFocusManager.kt:10-19`, `RomLuaScriptsUiModel.kt:7-16`).
- Reflow: `MelonDSAudio.cpp:28-45` wraps at 100 columns, the rest of the branch at 150.

### Phase 7: docs and repo root (S, half a day)

- Root keeps `README.md`, `MODS.md`, `LICENSE`. Everything else moves under `docs/`:
  - `PLAN-blackmagic3.md` + `HANDOFF-blackmagic3.md` (work merged and phone-verified; the handoff says the branch is finished) become one ~40-line `docs/blackmagic3.md`: what landed per batch, the savestate-14 field order, the "no port" decision and its reopen triggers, the open follow-ups.
  - `FRAME-INTERPOLATION.md`, `WIDEMELON-SOFTWARE.md` unchanged to `docs/shelved/`, linked from MODS.md "Remaining".
  - `PLAN-audio.md`: "Done" half cut to one line per item; `docs/plan-audio.md`.
  - `PLAN-hires-capture.md`: Phase 1 shipped; fold Design into a short `docs/hires-capture.md` (how the shadow copy works, what Phase 2 adds, the untested cases), drop the dated review section.
  - `PLAN-vulkan.md`: status and the Phase 0 result at the top, inventory table trimmed to what drove the decision.
  - This file follows the same route once executed.
- Dangling references: `PERF-NOTES.md` is cited 11 times and is local-only (say so once in `docs/`); `PLAN-threaded-2d-overhead.md` and `HANDOFF.md` are cited and deleted. Local paths `C:\Dev\melonDS`, `C:\Dev\WatermelonDS`, `C:\Dev\drastic` in `PLAN-hires-capture.md:7`, `PLAN-vulkan.md:5`, `MODS.md:7` become upstream repo + commit.
- `MODS.md:113-115`: the Adreno `uvec4` history and the "Lesson" paragraph to one sentence each; "Testing" trimmed to what is still untested.
- `README.md`: accurate on minSdk, packages, build task and APK path; add a short "What this fork adds" list (Material 3 settings, button styles, layout editor, WideMelon, Lua scripts → MODS.md, upscaled capture, frame pacing, settings backup, diagnostics logging, English-only UI) and one line on debug builds (`-PnativeBuildType=Debug`; debug APKs are RelWithDebInfo). `:36-37` still calls the app "melonDS".

## Verification per phase

- Phases 1, 2, 3: build debug + release, run the JVM tests and `LuaStorageTest`, then the phone checklist from `PLAN-blackmagic3.md` (OpenGL and compute, a Lua script with options and drawing, savestate load, rewind, fast-forward on/off for the pacing dedupe). Phase 2's debug-switch table: confirm each `setprop debug.melonade.*` still flips its target by reading the startup summary log.
- Phase 4: emulator-tester checklist on the foldable (layout editor snapping/centre/resize at a locked ratio, Mods tab rows, Lua scripts dialog, screen swap, external display) plus a perf-log and debug-log session each, diffing the summary and header against a pre-change run.
- Phase 5: `./gradlew :app:lintDebug` (couldn't run in the review container) for unused resources once the SDK is available; every settings screen opened once in light and dark.
- Phase 6, 7: build only; a reviewer reads the diff for lost meaning.

## Out of scope

- Core submodule, vendored Lua, enet/faad2/oboe.
- Pre-existing upstream TODOs not touched by the branch (`EmulatorViewModel.kt:1345`, `MelonDS.cpp` forwarders).
- New features; the Vulkan, hi-res Phase 2 and audio plans continue after this pass.

## Review material

The five per-area findings are in `docs/polish-findings/` (about 860 lines): every item with `path:line`, the proposed fix and an effort estimate, plus a one-sentence replacement for each flagged comment. They are the working list for Phases 4 to 6 and go when this plan does.
