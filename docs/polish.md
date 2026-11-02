# The polish pass over `material3` (done)

A cleanup pass over everything the fork had added since `c42995ca`, on branch `claude/friendly-heisenberg-3eipz2`, finished on 2026-11-02. It started from five read-only reviews (emulator UI and GL, layout editor and shared views, settings/impl/perf logging and tests, native code, resources/build/scripts/docs). The core submodule, vendored Lua and enet/faad2/oboe were out of scope, and so were new features. The full plan, the handoff with its per-commit test tables and the five review findings files are in git history: `git show a41e78b4:PLAN-polish.md`, `a41e78b4:HANDOFF-polish.md` and `a41e78b4:docs/polish-findings/`.

## Ground rules

1. One concern per commit, in phase order, so each can be built and phone-tested on its own.
2. Comments: keep the first sentence; delete anything that narrates history ("used to", "as before X"), restates the code or cites a plan doc. Rationale that still matters goes in the commit message.
3. No behavior changes except the correctness fixes listed in Phase 1.
4. American spelling everywhere: identifiers, comments and UI strings. Stored preference keys and values keep their spelling.
5. Native constants are `UPPER_SNAKE` in frontend files; `kCamel` may stay inside `scripting/` only.
6. Don't touch `app/src/main/cpp/lua/` (vendored) or the submodules.

## What each phase did

**Phase 1, delete and fix** (`30b08e96..d51e8806`): removed the gitHub flavor's dead update-install path (and with it the unused install permission), dead code, unused imports and stale markers; fixed the busy-spin pauses in reset and rewind load, a perf session that could never restart after a stuck one, the Lua options save using the wrong ROM lookup, a popup menu id of 0, a swallowed folder permission failure and the icon tint on pressed buttons; added Lua and a WideMelon credit to the licenses screen.

**Phase 2, native consolidation** (`8a0a19ac..7ac57ee9`): one `DebugProperties` table for the 14 `debug.melonade.*` switches, one `monotonicNanos()`, one tag scheme (every logcat tag is `melonade-*` now; `melonDS`, `melonDS-core`, `melonDS-lua` and `OpenGL` match nothing), `FramePacing.h` bodies moved to a .cpp, `AudioLatency` as an enum, `PackedTouch`, and a startup check of the layouts mirrored by hand between native and Kotlin.

**Phase 3, split `LuaEngine.cpp`** (`4cd096a6..90b696be`): the engine keeps the lifecycle and `setupScript`; the rest is `LuaSandbox.cpp`, `LuaMemoryApi.cpp`, `LuaDrawApi.cpp`, `LuaFolderApi.cpp`, `LuaStateApi.cpp` and `LuaStorageApi.cpp`, sharing `LuaEngineInternal.h`. A blocked I/O read in `melon.mem` raised a garbled error and was fixed on the way (`73105f89`).

**Phase 4, Kotlin consolidation** (`f58360d9..812dc29d` and `043e6634..6c0217d6`): `PerformanceLogger` split into logger, `PerformanceSummary` (table-driven, with a `# Keys` block) and `SessionStatistics`; one log-folder resolver, one state-change map and one settings/device snapshot shared by the perf and debug logs, all in `impl/diagnostics`; `LuaScriptHeader` in the domain model and a `LuaScriptOptionType` enum; the Lua scripts dialog in Compose; the layout editor's snapping, size and aspect-lock state consolidated; `ControllerAppearance` sealed; the Mods tab rows built from the shared preference rows. Logs differ from pre-pass ones on purpose: the perf summary gains a `# Keys` block and new `# App`/`# Device` fields, the debug log header becomes the same `key: value` lines, and the debug log's config keys are renamed (`jit`→`jit_enabled`, `renderer`→`video_renderer`, `resolution_scaling`→`internal_resolution_scaling`). CSVs are byte-identical.

**Phase 5, resources, strings, build** (`0ecffc27..f1cc27d9`): shared row styles and picker defaults, string fixes, a `PreferenceKeys` test against the XML, more JVM tests, a portable `emu.sh`, a merged shader dumper, and the UI strings in American spelling.

**Phase 6, comment pass** (`3d495c59..4597a377`): about 450 multi-sentence comment blocks cut to one sentence, plus American-spelling renames where every reference was in the fork's own code. Dropped rationale is in the commit bodies. The user tried its debug APK on the phone on 2026-11-02 and saw no issues.

**Phase 7, docs and repo root**: the root keeps README.md, MODS.md and LICENSE; the other docs moved under `docs/` and were condensed (see [README.md](README.md)); local paths became upstream repos at a commit.

## Departures and the user's calls

The user's calls: American spelling everywhere except stored preference keys and values (2026-11-02, replacing an earlier British-UI rule; upstream names like `SoftInputBehaviour*`, `FirmwareColour*`, `favouriteColour` and the stored value `super_colours` stay British); the hidden audio latency setting stays until it's measured ([plan-audio.md](plan-audio.md) step 7); the layout editor's fixed colors stay.

Departures from the plan: `MelonDSAndroidJNI.cpp` keeps `<ctime>` for `clock_gettime`, and the Lua license text comes from `lua.h`. The achieved-speed meter moved into `AudioDiagnostics`. The Lua API files are structs of static functions that `LuaEngine` befriends (a namespace can't be a friend), registered with `lua_newtable` + `luaL_setfuncs`. `clearLuaScripts()` keeps its lock so a write can't be lost to a concurrent `copy()`. ProGuard keeps were added for `LuaScriptOptionValue` and `LuaScriptDataSnapshot`, which native code reads by field name. `ControllerAppearance` is sealed `Vector`/`Pixel`, not `Flat`, because Outline and Soft tint are already called flat; the four `Anchor` ternaries stay to keep the float rounding. The preference title/summary pair became two styles rather than one `<include>`, and the chevron is a default only in `StoragePickerPreference`. About seven class-level paragraphs that state a thread or sign rule kept two sentences. Accepted visual changes: the Lua scripts dialog looks like the other Compose dialogs, and "Don't change" in a layout dropdown is single-line.

## Never checked, and known leftovers

- An RA popup dismissing on stop (it can't be raised on demand) and the fallback when the picked log folder isn't writable.
- Back on the Compose dialogs (pause menu → Mods, script options, achievements) was only tried with `KEYCODE_BACK`, not the back gesture or controller B.
- Phase 5's phone checklist: every settings screen in light and dark, the folder pickers' chevrons and pickers, the RetroAchievements switches grayed while logged out, the layout editor item panel's sliders and value dialogs, and a Lua script whose file starts with a UTF-8 BOM.
- Only Low audio latency can be tested, since the setting is hidden and the repository always sends Low.
- `Failed to allocate memory using ftruncate! (Invalid argument)` logs once per game load and the game runs; not looked into.
- Three lint errors older than the pass: `MissingConstraints` in `activity_emulator.xml`, `PropertyEscape` in `local.properties`, `WrongConstant` in `DeviceLayoutDisplayMapper.kt`.
- `.claude/shadercheck/check.sh` couldn't run on the Windows PC (no g++/glslang); the merged dumper's output was byte-identical under WSL.
- `getPlayerLayoutPicks` stays public and top-level in `LuaLayoutRequests.kt` because `LuaUiOverridesTest` calls it; move it into the view model only together with the test.
- Skipped: a `LuaSessionController` (it would move only about 70 lines), merging `LayoutEditorView.clampInside` with `LayoutEditorSnapping.clamp` (they place an item bigger than the editor differently), and `openGridDialog` as Compose (not a straight port).
- Testing notes: for the exit-reason check use `kill -9`, since a recents swipe is a user-requested exit that `ExitReasonReporter` skips. A Lua ITCM read on the script's first frame fails in DS mode because the game hasn't enabled ITCM yet; check after boot.

## Type-checking Kotlin without the SDK

When the Android SDK can't be downloaded (`dl.google.com` was blocked from the cloud container), Maven Central still has the compiler. Fetch `kotlin-compiler-embeddable`, `kotlin-stdlib`, `kotlin-script-runtime`, `kotlin-reflect`, `kotlin-daemon-embeddable` (all at the version in `gradle/libs.versions.toml`), `org.jetbrains.intellij.deps:trove4j:1.0.20200330`, `org.jetbrains:annotations:13.0` and `kotlinx-coroutines-core-jvm` from `https://repo.maven.apache.org/maven2/`, then:

```bash
find app/src/main/java app/src/test/java -name '*.kt' > files.txt
java -Xmx6g -cp '<all the jars>' org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -no-stdlib -no-reflect \
  -cp kotlin-stdlib.jar:kotlinx-coroutines-core-jvm.jar -jvm-target 17 -d /tmp/classes @files.txt > out.txt 2>&1
```

It reports about 18,000 errors, nearly all unresolved Android, Compose, Hilt and AndroidX references. Run it once on the unchanged tree and once after the change, normalize (`sed -E 's#^.*/([A-Za-z0-9_]+\.kt):[0-9]+:[0-9]+:#\1:#'`, sort, uniq) and `comm -13` the two: new lines that aren't an `unresolved reference` to an Android name are real. Inference failures that cascade from an unresolved Android type also show up as new lines in new files, so read the list rather than counting it. It proves nothing about code that touches Android types.

## Syntax-checking native code without the SDK

Needs the `melonDS-android-lib` and `app/src/main/cpp/oboe` submodules checked out (`git submodule update --init --depth 1 <path>`), `apt-get install libgles-dev libegl-dev`, a JDK for `jni.h`, and a stub directory with `android/log.h`, `sys/system_properties.h` (`PROP_VALUE_MAX 92`, `PROP_NAME_MAX 32`, `__system_property_get`), `android/asset_manager*.h`, `android/api-level.h`, `libslirp.h` (typedef `Slirp`, `SlirpCb`), `pcap/pcap.h`, and a `version.h` made from the core's `version.h.in`. Then, once without and once with `-DMELONADE_DEBUG_TOOLS`:

```bash
clang++ -std=c++20 -fsyntax-only -w -include sys/types.h -include cstring -include condition_variable -include atomic -include memory -include cmath \
  -I <stubs> -I <jdk>/include -I <jdk>/include/linux -I app/src/main/cpp -I melonDS-android-lib/src -I melonDS-android-lib/src/frontend \
  -I melonDS-android-lib/src/net -I app/src/main/cpp/oboe/include -I app/src/main/cpp/lua -I app/src/main/cpp/rcheevos/include \
  -I app/src/main/cpp/faad2/include -I app/src/main/cpp/enet/include -DOGLRENDERER_ENABLED '-DMELONDS_GL_HEADER=<GLES3/gl32.h>' \
  -DEGL_EGLEXT_PROTOTYPES -DANDROID -D__ANDROID__ [-DMELONADE_DEBUG_TOOLS] app/src/main/cpp/<file>.cpp
```

The forced `-include`s stand in for headers the NDK pulls in transitively; they are not missing includes to fix. `LuaStorageTest` builds and runs on Linux with the recipe in [MODS.md](../MODS.md#testing), using `gcc`/`g++` in place of the MinGW ones.
