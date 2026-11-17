# The polish pass over `material3` (done)

A cleanup pass over everything the fork had added since `c42995ca`, finished on 2026-11-02: dead code removed, a few correctness fixes, native and Kotlin consolidation (one `DebugProperties` table, `melonade-*` logcat tags, `LuaEngine.cpp` split into per-API files, `impl/diagnostics`), shared resources, a one-sentence comment pass and American spelling. Each phase's commits and rationale are in `git log`; the full plan, handoff and review findings are at `git show a41e78b4:PLAN-polish.md`, `a41e78b4:HANDOFF-polish.md` and `a41e78b4:docs/polish-findings/`.

Rules from the pass that still hold: comments keep the first sentence and drop history, restated code and plan-doc citations (rationale goes in the commit message); native constants are `UPPER_SNAKE` in frontend files, `kCamel` only inside `scripting/`; don't touch `app/src/main/cpp/lua/` (vendored) or the submodules. American spelling everywhere except stored preference keys and values; upstream names like `SoftInputBehaviour*`, `FirmwareColour*`, `favouriteColour` and the stored value `super_colours` stay British. The layout editor's fixed colors stay (the user's call). Don't undo: the four `Anchor` ternaries stay to keep the float rounding; layouts are mirrored by hand between native and Kotlin and checked at startup; the old logcat tags (`melonDS`, `melonDS-core`, `melonDS-lua`, `OpenGL`) now match nothing.

Logs changed on purpose in that pass: the perf summary has a `# Keys` block and `# App`/`# Device` fields, the debug log header uses `key: value` lines, and the debug log's config keys were renamed (`jit`→`jit_enabled`, `renderer`→`video_renderer`, `resolution_scaling`→`internal_resolution_scaling`). CSVs are byte-identical to before.

## Never checked, and known leftovers

- An RA popup dismissing on stop (it can't be raised on demand) and the fallback when the picked log folder isn't writable.
- Back on the Compose dialogs (pause menu → Mods, script options, achievements) was only tried with `KEYCODE_BACK`, not the back gesture or controller B.
- Phase 5's phone checklist: every settings screen in light and dark, the folder pickers' chevrons and pickers, the RetroAchievements switches grayed while logged out, the layout editor item panel's sliders and value dialogs, and a Lua script whose file starts with a UTF-8 BOM.
- Only Low audio latency can be tested, since the setting is hidden and the repository always sends Low. It stays hidden until it's measured ([plan-audio.md](plan-audio.md) step 7).
- Three lint errors older than the pass: `MissingConstraints` in `activity_emulator.xml`, `PropertyEscape` in `local.properties`, `WrongConstant` in `DeviceLayoutDisplayMapper.kt`.
- `.claude/shadercheck/check.sh` couldn't run on the Windows PC (no g++/glslang); it works under WSL.
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
