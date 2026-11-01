# Findings: Kotlin settings UI, impl (non-emulator), di, tests

Area: `ui/settings/**`, `impl/**` (excluding `impl/emulator`), `di/**`, `app/src/test/**`, `app/src/androidTest/**`.
Paths below are relative to `app/src/main/java/me/magnum/melonds/` unless prefixed with `test:` (= `app/src/test/java/me/magnum/melonds/`) or given in full.
All claims were checked against the current tree (callers grepped, fork version compared with `git show c42995ca:<path>`).

## 1. Dead code

- `impl/SettingsBackupFormat.kt:46-50` — the four `github_updates_*` keys in `KEPT_PREF_KEYS` can never appear in the default `SharedPreferences`: the update repositories stored them in a separate `"preferences-github"` file (see the removed DI code in `GitHubProdModule`/`GitHubNightlyModule`), and those repositories are no longer bound at all — delete the four entries and their comment — S
- `app/src/gitHub/java/me/magnum/melonds/di/GitHubModule.kt:22-30` — `provideGitHubApi` (Retrofit client) has no consumer left: `GitHubApi` is only used by `GitHubProdUpdatesRepository`/`GitHubNightlyUpdatesRepository`, which the flavour modules replaced with `DisabledUpdatesRepository` — remove the provider (and, outside this area, the two repositories, `GitHubApi.kt` and the retrofit dependency, or re-enable the feature) — M
- `impl/SettingsBackupManager.kt:44-45` + `di/MelonModule.kt:60-64` — `SettingsBackupManager` is both `@Singleton @Inject constructor` and provided by `provideSettingsBackupManager` (pre-existing, but the branch rewrote both sites) — keep one binding style, matching `PerformanceLogger`/`DebugLogRecorder` which use `@Provides` only — S
- `impl/debug/ExitReasonReporter.kt:45-47` — `settingsRepository.isDebugLogEnabled()` is re-checked although `reportNewExits()` is only called from `DebugLogRecorder.setRecording(true)`; the `settingsRepository` constructor parameter exists only for this — drop the check and the parameter — S
- `impl/perf/PerformanceLogger.kt:881-883` — `getDisplayRefreshRate()` is a one-line wrapper used once — inline into `run()` — S
- `impl/debug/LogFolder.kt:22` — `class LogFile(val outputStream: OutputStream)` wraps a single value and callers only ever read `.outputStream` — return `OutputStream` directly — S
- `ui/settings/preferences/PageListPreference.kt:14-15` — two stacked comments (`// Disable click behaviour…` and `/* no-op */`) for an empty override, carried over from the deleted `SoftwareInputBehaviourPreference` — keep one — S
- `ui/settings/fragments/InputPreferencesFragment.kt:62-64` + `ControllerStylePreferencesFragment.kt:100,120` + `impl/SharedPreferencesSettingsRepository.kt:551` + `res/xml/pref_input.xml:38` — the controller-style default (`"classic"` / `ControllerStyle.CLASSIC`) and its lower-case-name encoding are spelled out in four places — add `ControllerStyle.DEFAULT` + `toPreferenceValue()/fromPreferenceValue()` and use them everywhere — S

## 2. Duplication and reuse

- `impl/perf/PerformanceLogger.kt:558-579,915-947` vs `impl/debug/LogFolder.kt:29-52` — picking the log folder (`DocumentFile.fromTreeUri(...).takeIf { isDirectory && canWrite() }`, fallback to `getDefaultLogDirectory`, the "Cannot write to … Using the default" warning, `createFile`/`openOutputStream`) is implemented twice, and `LogFolder` depends back on `PerformanceLogger.getDefaultLogDirectory` — move `LogDestination`/`DocumentTreeDestination`/`FileDestination` and `getDefaultLogDirectory` into `LogFolder` and make `PerformanceLogger` use it — M
- `impl/perf/PerformanceLogger.kt:98-99,164-171` vs `impl/debug/DebugLog.kt:46-59` — `logStateChange`/`lastStates` and `stateChange`/`currentStates` are the same "emit only on change, replay at session start" logic, and every call site calls both back to back (`ui/emulator/EmulatorActivity.kt:812-813, 846-847`) — keep one state map (e.g. in `DebugLog`) that `PerformanceLogger` reads at `startSession`, or one `DiagnosticsLog.stateChange()` façade that fans out — M
- `impl/perf/PerformanceLogger.kt:591-617` (`buildSettingsSnapshot`) vs `impl/debug/DebugLog.kt:120-154` (`logConfiguration`) — two hand-maintained lists of the same settings with diverging key names (`video_renderer`/`renderer`, `internal_resolution_scaling`/`resolution_scaling`, `jit_enabled`/`jit`) — one `settingsSnapshot(configuration, settingsRepository): List<Pair<String,String>>` used by both, with one key spelling — M
- `impl/perf/PerformanceLogger.kt:638-655` (`# App`/`# Device`) vs `impl/debug/DebugLogRecorder.kt:159-186` (`buildHeader`) — package info, debuggable flag, SoC and `Build.*` fields gathered twice — one `AppAndDeviceInfo` helper returning key/value pairs — S
- `impl/perf/PerformanceLogger.kt:429-430,443-444,469-470` — the "elapsed µs formatted as ms gives seconds" trick with the identical two-line comment appears three times — `StringBuilder.appendSeconds(ns)` — S
- `impl/perf/PerformanceLogger.kt:347-349,356,402-407` — `if (flag) '1' else '0'` for CSV booleans appears 11 times — `StringBuilder.appendFlag(Boolean)` — S
- `impl/LuaScriptFolderContent.kt:43` and `impl/LuaScriptFolderSync.kt:11` — identical `NAME_ORDER` comparator — one shared `internal val` — S
- `impl/LuaScriptStorage.kt:264` + `impl/LuaScriptFolderContent.kt:39` — `LUA_EXTENSION = ".lua"` twice; `isLuaScriptName` vs `removeExtension(..., LUA_EXTENSION)` do the same check — keep constant and predicate in `LuaScriptFolderContent` — S
- `impl/LuaScriptDataStore.kt:150,165` — `hash.take(8).joinToString("") { "%02x".format(it) }` twice — `private fun ByteArray.toShortHex()` — S
- `impl/LuaScriptDataStore.kt:116` + `impl/LuaScriptStorage.kt:262` — `TAG = "melonDS-lua"` twice — one shared constant — S
- `impl/LuaScriptFolderContent.kt:30-35` — the `REASON_*` messages hardcode "1 MB", "2 MB", "256 files", "1024x1024", "4M pixels", "128 files", restating `MAX_*` constants that can drift — build the strings from the constants — S
- `ui/settings/fragments/PerformancePreferencesFragment.kt:34` + `impl/SharedPreferencesSettingsRepository.kt:339` + `impl/debug/DebugLog.kt:118` — `Build.SUPPORTED_64_BIT_ABIS.isEmpty()/isNotEmpty()` as the "JIT supported" test in three places — one `supportsJit()` helper — S
- `ui/settings/fragments/ControllerStylePreferencesFragment.kt:94-102,119-121` vs `SoftInputBehaviourPreferencesFragment.kt:70-80` — both Compose pages open `PreferenceManager.getDefaultSharedPreferences`, read with fallback and write back with `edit {}` — a `rememberPreferenceValue(key, decode, encode)` helper, or go through `SettingsRepository` — S
- `ui/settings/fragments/InputPreferencesFragment.kt:62-64` — `onResume` refreshes two `PageListPreference`s with the same line — loop, or move the refresh into `PageListPreference.onAttached()` — S
- `ui/settings/fragments/AboutFragment.kt:228-264` (`CardPosition`/`AboutCard`) vs `ui/settings/PreferenceCardDecoration.kt:56-84` — the "outer corners large, inner small" rule exists once for Views and once for Compose from the same two dimens — acceptable split; state the rule in one KDoc and reference it from the other — S
- `impl/SharedPreferencesSettingsRepository.kt:76` + `impl/SettingsBackupFormat.kt:16` (+ `migrations/Migration33to34.kt:20`) — `"controller_config.json"` defined in several places; likewise `"layouts.json"`/`"rom_data.json"` in `InternalLayoutsRepository`/`FileSystemRomsRepository` — reference `SettingsBackupFormat.*_FILE` (or a `DataFiles` object) — S
- `impl/PendingSettingsRestore.kt:313-322` (`putValue`) vs `SettingsBackupFormat.putTypedPreference` — two "put a typed value" switches — put `putValue(key, Any?)` into `SettingsBackupFormat` — S

## 3. Consolidation

- `impl/perf/PerformanceLogger.kt` (1657 lines) is four things in one file: session lifecycle + CSV writers (`PerformanceLogger`/`Session`, ~550 lines), the text summary (`buildSummary`, 619-856, 240 lines), the statistics (`SessionStatistics`, `RefreshRateStatistics`, `DurationHistogram`, 961-1629) and the log destinations (901-959) — split into `PerformanceLogger.kt`, `PerformanceSummary.kt`, `SessionStatistics.kt` (+ histogram, already `internal`/`@VisibleForTesting`) and the shared `LogFolder`/`LogDestination` file from 2.1 — L
- `impl/perf/PerformanceLogger.kt:619-856` — `buildSummary` is 240 `line(...)` calls with each metric explained in a comment — make it table-driven (`SummaryEntry(key, description, value)`) so the key documentation is emitted into the summary header (and testable) instead of living in comments — M
- `impl/debug/*` and `impl/perf/*` form one diagnostics feature split over two packages with two log-folder resolvers — one `impl/diagnostics` package with `LogFolder` at its root — S (after 2.1)
- `impl/SettingsBackupManager.kt:36-42` and `impl/PendingSettingsRestore.kt:20-30` — both KDocs explain the two-step restore and the staging folder — keep the protocol description in `SettingsBackupFormat` (which owns the file names) and link to it — S
- `common/PreferenceKeys.kt` vs `res/xml/pref_*.xml` — the object's KDoc admits the keys are duplicated as XML literals and "must match"; nothing enforces it — add a JVM test that parses `res/xml/pref_*.xml` and asserts every `PreferenceKeys` constant (except the documented "no screen" ones) is an `android:key`, and that `defaultValue`s equal the defaults hardcoded in `SharedPreferencesSettingsRepository` (all 14 currently agree, by discipline only) — S
- `impl/SharedPreferencesSettingsRepository.kt` — new accessors use `PreferenceKeys.*` while upstream ones use literals (`"theme"`, `"enable_sustained_performance"`, `"enable_jit"`…), and `PerformancePreferencesFragment:30-32` mixes both on adjacent lines — state the rule ("fork-added keys only") in one line, or move the upstream keys the fork touches into `PreferenceKeys` — S
- `impl/debug/DebugLogRecorder.kt:46-47` — `LogFolder` and `ExitReasonReporter` are built by hand inside the recorder while the rest of the feature is provided by `MelonModule` — provide them from the module — S

## 4. Comments

Every comment longer than one sentence that is new on the branch (pre-existing melonDS-android comments in `PreferenceFragmentHelper`, `FileSystemRomsRepository`, `UILayoutProvider`, `DefaultLayoutProvider:45,250,300` are left alone). Format: location — proposed one-sentence replacement ("keep" = already one sentence, listed because it spans two lines or two clauses).

### impl/perf/PerformanceLogger.kt
- `:47-52` (class KDoc) — "Opt-in logger that drains the emulator and display frame samples about once a second and writes CSVs plus a running summary, all on a background thread." (move the 30 s PSS detail next to `PSS_SAMPLE_INTERVAL_NS`)
- `:101-105` (`startSession`) — "Starts a session named after the ROM or firmware if performance logging is enabled, stopping any active one first."
- `:118` — "Sessions share the native capture buffer, so wait for the previous one to finish."
- `:123` — delete (restates `join()`)
- `:137-138` — "Stops the active session; remaining samples and the final summary are written in the background."
- `:151-153` — "Queues an event for the active session's events file; safe to call from any thread."
- `:160-162` — "Like [logEvent], but only when [value] differs from the last value for [key]; every key is also written at session start."
- `:197` — delete the second clause ("until the display samples carry their own vsync period"): stale, samples already carry `refresh_period_ms`
- `:199-200` — "Re-read at session start and before each periodic summary, not for the final one (the display may already be back at its normal rate)."
- `:224-226` — "A full PSS read walks /proc/self/smaps under the mmap lock and can stall the JIT, so it only runs before/after capture and every 30 s, with its duration logged."
- `:229` — "Capture starts before the writers open so start-up frames are kept."
- `:258` — delete (restates the call)
- `:429`, `:443`, `:469` — deleted by the helper from 2.5
- `:447` — "With no stream open at the start, the first stream's open size is the session's starting buffer size."
- `:461` — "/proc/self/statm is cheap: it doesn't walk the mappings."
- `:635` — "false until the session stopped cleanly."
- `:656-658` — "Vsync period the render loop ran at, from the counted non-fast-forward samples; the display's reported rate stands in until the first sample."
- `:672` — "Mode the 60Hz lock asks for; off = setting disabled, none = no 60Hz mode at this resolution."
- `:733-734` — "Longest run explained by the fastest counted refresh rate; longer runs are counted separately."
- `:739-740` — "Hold = refreshes a frame stayed on screen; judder is holds longer or shorter than expected even when the average matches."
- `:742-743` — "Holds spanning a rate change are skipped; with several rates the expected hold is averaged over the measured holds."
- `:757` — "Requested (always LOW; the latency preference isn't used) and actual stream latency."
- `:797-799` — "Priming over the whole emulation session (including before capture): silence while the buffer filled, how often it ran dry, and the trims when priming ended."
- `:818-819` — "Speed paced fast-forward actually reached (smoothed ~0.35 s) over the fast-forward frames where it was known."
- `:823-824` — "AAudio buffer size at session start (or first open during the session), at the end, and how often it grew after an xrun."
- `:1002-1003` — "A hold is the time between two new frames in refresh periods, measured by time because render callbacks can be merged or dropped."
- `:1014-1015` — "Timestamp of the current frame's first sample; invalid after a pause, fast-forward, an invalid frame or a rate change."
- `:1019-1020` — "Refresh rates seen, in first-seen order; every sample is measured against the rate in effect for it."
- `:1022-1023` — "Rate of the latest sample (or the initial one); null while unknown, when no holds or repeats are expected."
- `:1030-1031` — "Rate-weighted stats only use counted non-fast-forward samples, since fast-forward drops the 60Hz lock and shows a new frame almost every refresh."
- `:1287` — "Checked against the current rate and counted once per run."
- `:1327-1328` — "Each completed second's window is checked before its slot is reused; after a full window of steps every slot is empty."
- `:1356-1358` — "Worst off-cadence percentage over any [JUDDER_WINDOW_SECONDS] window with at least [JUDDER_WINDOW_MIN_FRAMES] holds, including the current one."
- `:1370-1372` — "Compact histogram such as \"1:2918 2:17640 3:2873 4:37 5+:6\": every hold up to 2 above the longest on-cadence one (at least 4), then the rest."
- `:1389-1391` — "Time-weighted average rate of the counted non-fast-forward samples, or null if none had a known rate."
- `:1398-1400` — "Expected repeat share from each counted sample's rate, or the current rate's before any sample."
- `:1426-1428` — "On-cadence holds, e.g. \"2\", or per rate when measured at several, e.g. \"1 at 60.00 Hz, 2 at 120.00 Hz\"."
- `:1542-1544` — "Fixed-size duration histogram: 0.05 ms buckets up to 250 ms plus an overflow bucket; count, sum, sum of squares, min and max are exact."
- `:1590-1591` — "Nearest-rank percentile, approximating values by the bucket midpoint (overflow bucket: its average)."
- `:1636-1637` — "Keeps a value in one CSV field by replacing separators."
- `:1643-1644` — "Appends nanoseconds as milliseconds with 3 decimals without String.format (too slow at thousands of lines a second)."
- Single-sentence ones to keep as is: `:63, :98, :189, :192, :251, :267, :279, :350, :361, :408, :413, :493, :661, :664, :667, :670, :714, :719, :774, :784, :807, :812, :814, :846, :850, :971, :974, :977, :1010, :1026, :1036, :1043, :1046, :1049, :1101, :1147, :1165, :1193, :1244, :1295, :1407, :1416, :1443, :1448, :1457, :1510, :1515, :1517, :1520`

### impl/perf/DisplayPerformanceCapture.kt
- `:5-11` (KDoc) — "One sample per presented display frame, recorded by the render thread and drained by [PerformanceLogger] through a lock-free single-producer/single-consumer ring." (list the fields as per-index comments next to `FIELDS`)
- `:38-39` — "Consumer only; enabling discards pending samples."
- `:90-91` — "Consumer only; copies pending samples into [buffer] and returns how many."

### impl/debug/*
- `DebugLog.kt:12-16` — "Markers and configuration dumps for the debug log; nothing here runs while it's off, and nothing private (paths, firmware identity, RA account) is logged."
- `DebugLog.kt:37-38` — "Logs a marker; the message is only built while the log is on."
- `DebugLog.kt:61-67` — keep the summary line, drop the `@param` block
- `DebugLogRecorder.kt:26-31` — "Records this process' logcat to melonade_debug_<timestamp>.txt in the log folder while the debug log setting is on, flushing often enough that a hang or kill keeps the lines so far." (header and `ExitReasonReporter` details go on the methods)
- `DebugLogRecorder.kt:51-52` — "Starts recording if the setting is on and follows it from then on; call once from the main thread at app start."
- `DebugLogRecorder.kt:56, :72, :134, :149` — keep
- `ExitReasonReporter.kt:19-25` — "Writes a melonade_exit_<timestamp>.txt report (plus the ANR trace or native tombstone when the system has one) for each crash, ANR or foreground kill since the last check, looking back [FIRST_RUN_LOOKBACK_MS] on the first run."
- `ExitReasonReporter.kt:80` — "Saved per exit so a retry never writes the same files twice."
- `ExitReasonReporter.kt:102, :123` — keep
- `LogFolder.kt:12-14` — "Creates files in the log folder picked in the performance settings, or in the default log directory when none is picked or writable."
- `LogFolder.kt:24-28` — drop the `@param`; one line on the default: "\"text/plain\" keeps a .txt name; other extensions need a generic type."

### impl/Lua*
- `LuaScriptHeader.kt:5-21` (13-line KDoc) — keep the example block (it is the spec), cut the prose to: "What a script says about itself in its leading comment block: a description line and `option:` lines (examples below), parsed from the first [READ_LIMIT] bytes without running Lua."
- `LuaScriptHeader.kt:48-51` — "Attributes each option type takes besides [COMMON_ATTRIBUTES], and its parser (null on a bad line)."
- `LuaScriptHeader.kt:146-148` — "Parses what follows `option:`, or returns null on any malformed part."
- `LuaScriptHeader.kt:168-170, :248, :273, :286` — keep
- `LuaScriptFolderContent.kt:3-13` (8-line KDoc) — "Rules for the modules (`require`) and images (`melon.image.load`) a linked folder offers its scripts, kept free of Android so they can be unit tested." (walk rules move onto `walk`/`getModuleName`)
- `LuaScriptFolderContent.kt:66-68` — "Module name `require` uses for [path], or null if it's a top-level script or has a disallowed path part."
- `LuaScriptFolderContent.kt:99-101` — "Walks from [root]; a folder [listChildren] can't read is skipped, an unreadable root returns null."
- `LuaScriptFolderContent.kt:170-173` — "Takes modules in path order until a cap is hit; after the first one that doesn't fit, every later one is left out."
- `LuaScriptFolderContent.kt:195-196, :200` — keep
- `LuaScriptFolderSync.kt:13-20` — "Returns the folder's scripts sorted by name, keeping the switch and options of known ones; with no folder the list is empty, and with an unreadable folder ([folderScriptNames] null) it's kept as is."
- `LuaScriptStorage.kt:16-19` — "Reads each game's Lua scripts from its linked folder; the app never writes there and native never opens script files."
- `LuaScriptStorage.kt:40-42` — "Top-level `.lua` files, unordered, or null when the folder can't be read (lost permission, moved, missing card, cloud still fetching)."
- `LuaScriptStorage.kt:46-47` — "Checked first because some providers list a missing folder as empty (and isDirectory is false once the permission is gone)."
- `LuaScriptStorage.kt:58-60` — "Lists one folder with a single query; handles are document IDs; null when the provider says the list is incomplete."
- `LuaScriptStorage.kt:63-64` — "One query instead of DocumentFile's two calls per file."
- `LuaScriptStorage.kt:73-74` — "A partial or failed cloud listing isn't the folder's content, so it counts as unreadable."
- `LuaScriptStorage.kt:93-95` — "Reads the scripts' modules and images (rules in [LuaScriptFolderContent]); lists up to 65 folders and reads a few MB, so never on the main thread."
- `LuaScriptStorage.kt:194-197` — "Reads the given scripts in order from [folderUri]; with no folder every result is [ReadResult.FolderNotFound]."
- `LuaScriptStorage.kt:237-239` — history-narrating ("Older versions copied…"): "Deletes the `files/scripts/` copies made before linked folders." (better: move the call into a migration and delete the method)
- `LuaScriptDataStore.kt:9-14` — "Stores the blobs scripts save with melon.storage under `files/lua-data/<game key>/<script>.bin`; writes are atomic and the data outlives the script leaving the folder."
- `LuaScriptDataStore.kt:19-20` — "Game key of the session running scripts; its data can't be deleted while native still holds it."
- `LuaScriptDataStore.kt:93-95` — "Deletes [scriptName]'s data unless its game is running scripts." (drop the "can't happen today" sentence)
- `LuaScriptDataStore.kt:135-139` — "Folder key for a game: the real game code, or a hash of the ROM URI for placeholder codes shared by homebrew; null when the header couldn't be read, so data stays session-only."
- `LuaScriptDataStore.kt:153-155` — "The script's file name plus `.bin`, or a hash when the name can't be a file name here."
- `LuaScriptDataStore.kt:41, :108, :122, :130` — keep

### impl/Settings*, PendingSettingsRestore
- `SettingsBackupManager.kt:36-42` — "Writes settings backups and stages restores; [PendingSettingsRestore] applies a staged restore on the next start so in-memory repositories can't overwrite it." (second paragraph goes, see 3.4)
- `SettingsBackupManager.kt:63-64` — "settings.json is removed first and written last so an interrupted backup can't be restored."
- `SettingsBackupManager.kt:74-76` — "Checks the backup and stages it; nothing changes until [PendingSettingsRestore] runs on the next start."
- `SettingsBackupManager.kt:100-101` — "Marker first: deleteRecursively() has no order and a marker without its files would apply a partial restore."
- `SettingsBackupManager.kt:117-120` — "Checks a data file the way its loader reads it (loaders fall back to empty data and the next save would wipe it); older-format files are only checked to be JSON."
- `SettingsBackupManager.kt:138-139` — "Same parser and DTOs as the repositories, on the text because Gson's tree path truncates numbers the text path rejects."
- `SettingsBackupManager.kt:154-156` — "Parses with the same parser [PendingSettingsRestore] uses and rejects trailing content."
- `SettingsBackupManager.kt:195-196` — "\"wt\" truncates; some providers' \"w\" doesn't, which would leave the tail of a longer old backup."
- `SettingsBackupManager.kt:52` — keep
- `PendingSettingsRestore.kt:20-30` (8-line KDoc) — "Applies a restore staged by [SettingsBackupManager.restore] at app start, before any preference or data file is read, with a rollback snapshot so a failure keeps the previous settings." (per-folder descriptions move onto the constants)
- `PendingSettingsRestore.kt:120-121` — "A file missing from the backup is removed, so every data file matches the restored last_version the migrations expect."
- `PendingSettingsRestore.kt:152-155` — "Replaces every preference with the backup's; keys not in the backup reset, except [KEPT_PREF_KEYS]."
- `PendingSettingsRestore.kt:206-209` — "Snapshots the data files (an empty `.absent` marker for a missing one) and the preferences into [rollbackDir]."
- `PendingSettingsRestore.kt:273-276` — "Markers go before folders because deleteRecursively() has no order and a stray marker would re-run the restore on partial data."
- `PendingSettingsRestore.kt:48-51, :75, :80, :87, :98, :107, :112, :145, :150, :159, :163, :171, :232, :262` — keep
- `SettingsBackupFormat.kt:9-12` — "File names and value encoding shared by [SettingsBackupManager] and [PendingSettingsRestore]."
- `SettingsBackupFormat.kt:53-55` and `:107-109` — both say "backups made before the values had types store plain numbers"; keep one note on `LONG_PREF_KEYS` and reference it
- `SettingsBackupFormat.kt:27-29, :41, :68-70` — keep

### impl/dtos, impl/layout, impl/SharedPreferencesSettingsRepository
- `dtos/layout/LayoutConfigurationDto.kt:24-25` — history-narrating: "Read-only legacy field: pre-variant layouts stored one ratio per layout, copied to the variants on load."
- `dtos/layout/UILayoutDto.kt:13,31`, `dtos/rom/RomConfigDto.kt:21,24`, `dtos/rom/RomModsConfigDto.kt:16,26` — "Missing in configs saved before X" six times — one class-level sentence ("Nullable fields are absent in configs saved before their feature; Gson leaves them null") and delete the per-field copies
- `dtos/rom/RomModsConfigDto.kt:35-36` — "Gson can leave the list, an entry or its file name null; entries without a file name are dropped."
- `dtos/layout/LayoutConfigurationDto.kt:58` — keep
- `layout/DefaultLayoutProvider.kt:125-127` — "Geometry of the default layouts, kept free of Android so it can be unit tested; sizes in dp via [dpToPixels]."
- `layout/DefaultLayoutProvider.kt:389-393` — "Row of small utility buttons centred between L and R at [rowTop]; buttons that don't fit (menu, then swap) move together to the nearest free spot above or below."
- `layout/DefaultLayoutProvider.kt:429-431` — "No free spot: put them over the top screen's top-right corner (it takes no input), or the safe area's."
- `layout/DefaultLayoutProvider.kt:438-443` — "Landscape utility row anchored so soft-input and mic flank [centerX] and the rest runs left over the top screen; buttons that would hit L each move to the nearest free spot below."
- `layout/DefaultLayoutProvider.kt:487-489` — "Nearest spot at or below [preferredRect] that overlaps none of [placedRects], scanning line by line from the preferred x outwards."
- `layout/DefaultLayoutProvider.kt:517-519` — "Spot [width] wide at [x] just above or below the row at [rowTop] that overlaps nothing, [spacing] away or centred if there isn't room."
- `layout/DefaultLayoutProvider.kt:303`, `layout/UILayoutProvider.kt:47-49` — keep
- `SharedPreferencesSettingsRepository.kt:103-104` — "combine() takes at most 5 typed flows, so the look and threading settings are grouped first."
- `SharedPreferencesSettingsRepository.kt:77`, `AndroidRetroAchievementsRepository.kt:62`, `FileSystemRomsRepository.kt:236` — keep

### ui/settings
- `PreferenceCardDecoration.kt:15-24` (7-line KDoc) — "Draws the rows between two [PreferenceCategory] headers as one card group (gaps between rows, large outer and small inner corners), recomputed on every layout/draw so hidden rows don't break the shape."
- `BasePreferenceFragment.kt:46-49` — "Hides [group] while none of its rows is visible; call again when a row's visibility changes."
- `BasePreferenceFragment.kt:54-57` — "Shows [format] of the saved value as the summary, kept current by the summary provider."
- `SettingsActivity.kt:114-116` — "Pushes [fragment] as a new settings page; Back returns here."
- `SettingsBackupLauncher.kt:20-24` — "Folder pickers plus the off-main-thread backup/restore and their result dialogs; create it while [fragment] is being created (it registers activity results), [settingsBackupManager] is only called after a folder is picked."
- `SettingsBackupLauncher.kt:103-106` — "[runCatching] that rethrows [CancellationException] and still catches Errors such as StackOverflowError from a deeply nested file."
- `SettingsContentPadding.kt:14-16` — "Side and bottom insets plus the given margins; the top inset is already taken by SettingsActivity's toolbar."
- `CheatsImportProgressDialog.kt:28-29` — "Launched once in onCreate (onStart would add a collector per restart); it only collects while started, after onCreateDialog created the binding."
- `PerformancePreferencesFragment.kt:52-53` — "Both logs share the folder, so it's enabled while either is on; the listener runs before the value is saved, hence the explicit argument."
- `OpenSourceLicensesFragment.kt:29-32` — "Third-party code and licences, generated at build time by AboutLibraries from the Gradle dependencies plus app/config/libraries."
- `OpenSourceLicensesFragment.kt:72-73` — "Sheet, not dialog: the dialog renders HTML and drops the GPL's angle-bracket addresses."
- `AboutFragment.kt:61-64` — "Settings → About: version, credits, licence notices and source links required by the GPL-3 and the bundled licences."
- `AboutFragment.kt:202-205` — "The launcher icon drawn from its adaptive layers (Compose can't load the mipmap): the 108 dp foreground scaled 1.5× inside the clipping circle, like a launcher."
- `AboutFragment.kt:224-227` — "Where a card sits in its group, which decides which corners are large (see PreferenceCardDecoration)."
- `AboutFragment.kt:266-269` — "Icon, title and summary laid out like preference_card.xml, with an \"open in new\" icon for web links."
- `ControllerStylePreferencesFragment.kt:167-168` — "Small controller built from the real button views; pressable only when [touchVibrator] is set."
- `SliderPreference.kt:13-20` (5-sentence KDoc) — "[SeekBarPreference] rendered with an M3 [Slider], keeping its attributes and persistence; every change goes through `value` so the slider and value text always show the stored value."
- `SliderPreference.kt:35-36` — "SeekBarPreference saves D-pad changes on its hidden SeekBar without notifying, so the keys are handled here."
- `PageListPreference.kt:7-10` — "[ListPreference] whose value is picked on its app:fragment page instead of a dialog, so the row still shows the value name as summary."
- keep: `PreferenceCardDecoration.kt:46, :80`, `SettingsBackupLauncher.kt:46-48, :76, :82, :97`, `PerformancePreferencesFragment.kt:67`, `AppearancePreferencesFragment.kt:28`, `OpenSourceLicensesFragment.kt:54`, `LicenseTextFragment.kt:30, :67, :70`, `AboutFragment.kt:68, :72, :130`, `ControllerStylePreferencesFragment.kt:98`, `SliderPreference.kt:38, :41, :75, :91`, `SoftInputBehaviourPreferencesFragment.kt:78`

### tests
- `test:impl/FakeSharedPreferences.kt:5-7` — "In-memory [SharedPreferences] for JVM tests; like the real one, clear() applies before the edit's other changes."
- `test:impl/PendingSettingsRestoreTest.kt:100-103, :130-134` — test-environment quirks (org.json BigDecimal vs Double); one sentence each, with the quirk named in the test method name
- `test:impl/layout/UtilityButtonsTest.kt:123-124` — "Buttons may overlap the screens but nothing else."
- `test:domain/model/layout/ScreensDrawingTest.kt:8-9`, `test:ui/emulator/LuaUiOverridesTest.kt:296-297`, `test:ui/layouteditor/LayoutEditorSnappingTest.kt:57-58` — one sentence each, or move the arithmetic into named constants

## 5. Polish

- `di/MelonModule.kt:38-39` — `impl.debug.DebugLogRecorder` and `impl.perf.PerformanceLogger` imports inserted after `impl.layout.*`, breaking the file's alphabetical import order — reorder — S
- `impl/layout/DefaultLayoutProvider.kt:388-389` — class closing brace directly followed by `/**` with no blank line — add it — S
- `ui/settings/preferences/PageListPreference.kt:17`, `ui/settings/fragments/AboutFragment.kt:318` — no trailing newline in new files — add — S
- `ui/settings/SettingsBackupLauncher.kt:58` — `backUp()` next to `launchBackup()`/`SettingsBackupManager.backup()` — rename to `backup()` — S
- `ui/settings/fragments/ControllerStylePreferencesFragment.kt:180` — hardcoded `Color(0xFF1B1D22)` preview background on an otherwise themed screen — theme colour or `colorResource` — S
- `ui/settings/fragments/ControllerStylePreferencesFragment.kt:187-203` — six `PreviewButton(...)` calls on 150+-column lines — wrap arguments like the surrounding Compose code — S
- `ui/settings/fragments/AppearancePreferencesFragment.kt:106,110` — `findPreference<Preference>("backup_settings")?.` while every other new fragment uses `!!` for keys it owns — pick one — S
- `impl/debug/DebugLog.kt:18` + `impl/debug/DebugLogRecorder.kt:182-183` — tag names `melonade-debug/-cpu/-audio`, `melonDS-core`, `melonDS-lua` are listed as prose in the header — constants in one place, header built from them — S
- `impl/debug/DebugLogRecorder.kt:75` — "Debug log turned off" uses `Log.i(DebugLog.TAG, …)` directly instead of `DebugLog.event {}` like every other marker — use `DebugLog.event` (still enabled at that point) — S
- `impl/perf/PerformanceLogger.kt:85` vs `impl/debug/ExitReasonReporter.kt:154` and `LogFolder.kt:29` — `"application/octet-stream"`/`"text/plain"` literals vs `LOG_FILE_MIME_TYPE` constant — share (falls out of 2.1) — S
- `impl/perf/PerformanceLogger.kt:783` — nested `if … else if … else` for `audio_xruns` on one 170-column line — extract a small function — S
- `impl/perf/PerformanceLogger.kt:1632-1657` — three top-level `private fun`s after the class while every other helper is nested — move into the companion or a `PerfCsv` object — S
- `impl/LuaScriptDataStore.kt:116` / `impl/LuaScriptStorage.kt:262` — log tags with the "melonDS-" prefix while the fork's other new logging uses "melonade-" — align — S
- `impl/SharedPreferencesSettingsRepository.kt:76-78` — `DEFAULT_THEME` is a second copy of the XML default now that the Android-Q branch is gone — rely on the XML default (`PreferenceManager.setDefaultValues`) or keep and note why — S
- `ui/settings/fragments/SaveFilesPreferencesFragment.kt:513-516` — `val rewindPeriod = rewindPeriodValue` is a no-op rename and `* 10` repeats the "steps of 10 seconds" from line 508 — one `REWIND_WINDOW_STEP_SECONDS` — S
- `ui/settings/fragments/VideoPreferencesFragment.kt:15-20` — field comments restate what `onRendererPreferenceChanged` already expresses — delete — S
- `test:impl/LuaScriptDataStoreTest.kt:55-83` — the last two tests exercise `RomModsConfigDto`, not `LuaScriptDataStore` — move to a `RomModsConfigDtoTest` (or into `RomConfigDtoTest`) — S

## 6. Correctness smells (only where confident)

- `impl/perf/PerformanceLogger.kt:106-135` — if a previous session never finishes (blocked in a provider write), every later `startSession` waits 5 s, warns and returns, yet `currentSession` stays set so `isSessionActive()` is true and `EmulatorViewModel.updatePerformanceLoggingSession` never retries — clear `currentSession` when giving up — M
- `impl/SettingsBackupManager.kt:146-149` — `toModel()` on the DTOs can throw non-`IOException`s (e.g. `IllegalArgumentException` from `enumValueOfIgnoreCase`); the launcher still shows the generic error, but wrapping in `IOException("$fileName can't be read", e)` makes the restore error path uniform — S
- `impl/debug/DebugLogRecorder.kt:56-57` says the repository's flow map isn't thread safe, yet `PerformanceLogger.buildSettingsSnapshot` calls `observeRenderConfiguration()` from `Dispatchers.IO` — either the comment is misleading or the perf logger has the same race; check `getOrCreatePreferenceSharedFlow` — S

## 7. Tests: cheap gaps

- `DurationHistogram` (`PerformanceLogger.kt:1546-1629`) is pure and `internal` but untested: nearest-rank percentile, `meanOfSlowest`, overflow-bucket average, `coerceIn(min,max)` — 4 small tests — S
- `SessionStatistics.trackFrame`/`trackAudioFrame`/`trackAudioFastForwardSpeed` (`:1078-1168`) — only the display path is covered (`DisplayCadenceStatisticsTest`); add fast-forward/after-pause exclusion, missed-frame threshold and the -1 sentinels — S
- `StringBuilder.appendMillis`, `String.toCsvField`, `Session.buildFilePrefix` (`:581-589, 1639-1657`) — make `internal` and test rounding, negatives and the 48-char cut — S
- `RefreshRateStatistics` init (`:1526-1535`) — 60/90/120/144 Hz on-cadence holds and `expectedLongestRepeatRun` — S
- `LayoutConfigurationDto.toModel` legacy `wideAspect` copy (`dtos/layout/LayoutConfigurationDto.kt:52-60`) and `PositionedLayoutComponentDto` spacing clamp (`:35`) — Gson round-trip tests like `RomConfigDtoTest` — S
- `SettingsBackupFormat.getBackupLong` and `preferencesToJson` for `Float`/`Set` — only indirectly covered; direct test is 10 lines — S
- `PreferenceKeys` ↔ `res/xml` consistency test (3.5) — S
- `LuaScriptStorage.readAtMost` (`:248-259`) — make a top-level `internal fun` and test the limit boundary — S
- Not cheap (need Android): `ExitReasonReporter`, `LogFolder`, `DebugLogRecorder`, `SliderPreference`.

## Top 5 highest-value items in this area

1. Split `impl/perf/PerformanceLogger.kt` (1657 lines) into logger/session, summary rendering, statistics (+histogram) and a shared log-destination file, and make the 240-line `buildSummary` table-driven (3.1, 3.2) — L.
2. Merge the three parallel diagnostics paths: one log-folder resolver for `PerformanceLogger` and `LogFolder`, one state-change map instead of `PerformanceLogger.logStateChange` + `DebugLog.stateChange` called back to back at every site, one settings/device snapshot shared by the perf summary and the debug log header (2.1–2.4) — M.
3. Delete dead code the branch left behind: the `github_updates_*` `KEPT_PREF_KEYS` entries that can never match, `GitHubModule.provideGitHubApi` and the now-unbound GitHub updates repositories, the redundant `ExitReasonReporter.settingsRepository` check, `LogFolder.LogFile` (section 1) — S/M.
4. Comment pass: ~110 multi-sentence comments in new code (31 in `PerformanceLogger` alone, plus 5–13-line class KDocs on `LuaScriptHeader`, `LuaScriptFolderContent`, `PendingSettingsRestore`, `PreferenceCardDecoration`, `DebugLogRecorder`, `ExitReasonReporter`), each with a one-sentence replacement in section 4; also the six repeated "Missing in configs saved before…" DTO notes and the history-narrating ones in `LuaScriptStorage:237` and `LayoutConfigurationDto:24` — M.
5. Cheap tests and one source of truth for settings: a `PreferenceKeys` ↔ `res/xml` consistency test (keys and defaults), `DurationHistogram`/frame-statistics tests, and one `ControllerStyle` default/encoding instead of four copies (3.5, 7, 1.8) — S.
