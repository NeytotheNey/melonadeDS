# Handoff: the polish pass (PLAN-polish.md)

Read this, then `PLAN-polish.md`. The plan has the per-item file references, the phase order and the verification list per phase. This file says where things stand, what is untested, and what the next session does first.

## Where things stand (2026-11-01)

- **Branch:** `claude/friendly-heisenberg-3eipz2` in this repo, on top of `material3`. Phases 1, 2 and 3 are applied, one commit per concern (21 for Phase 1, 9 for Phase 2, 8 for Phase 3), each phase followed by a status commit on the plan. The core submodule is untouched by the pass.
- **Phase 1 (delete and fix):** verified on the Z Fold 8 on 2026-11-01 (commit `4768a989` records what was checked: debug build, 147 JVM tests, `LuaStorageTest`, reset, rewind, fast-forward, a Wide pick, the four button styles, a Lua options save, the licence entries).
- **Phase 2 (native consolidation):** applied in `8a0a19ac..7ac57ee9`, written without the Android SDK and syntax-checked with host `clang++` only.
- **Phase 3 (split `LuaEngine.cpp`):** applied in `4cd096a6..90b696be`, same host `clang++` check. It touched no Kotlin.
- **Phases 2 and 3 verified together** on the Z Fold 8 on 2026-11-01: the whole checklist below passes (`PLAN-polish.md` status has the detail), except that only Low audio latency can be tested, because the setting is hidden and always sends Low. The run found an older bug, a blocked `melon.mem` I/O read raising "invalid option '%0'", fixed in `73105f89`.
- **Phases 4 to 7:** not started. Phase 4 (Kotlin consolidation) is next.

## What Phase 2 changed, in the order it should be tested

| Commit | Change | What can break |
|---|---|---|
| `8a0a19ac` | `debug/DebugProperties.{h,cpp}`: one table for the fourteen `debug.melonade.*` switches; the threaded2d/hirescap overrides only exist in debug builds | A switch no longer flipping its target. The summary line moved from the core's tag to `melonade-debug` |
| `18f19126` | `applyFastForwardPacing()`, `FramePacingState::reset()`, static clock helpers in `MelonDSAndroidJNI.cpp` | Fast-forward on/off, a settings change while fast-forwarding, pacing after a pause |
| `59347132` | `MonotonicClock.h`; all logcat tags under `melonade-*` in `MelonLog.h`; `LOG_*` macros instead of raw `__android_log_print` | **Logcat filters on `melonDS`, `melonDS-core`, `melonDS-lua` or `OpenGL` match nothing now.** `emu.sh log` and the tester agent were updated |
| `80423514` | `renderer/FramePacing.cpp` (bodies out of the header); `EMULATOR_TARGET_FPS` in `Configuration.h` | Link errors only |
| `a757d74d` | `logDebugFrameState()` out of line; `DebugLog.h` forward-declares `NDS` | Compile errors only |
| `55cb0429` | `MelonDSAudio.cpp` state all static; `enum class AudioLatency` in `Configuration.h`; the achieved-speed meter moved into `AudioDiagnostics` | The audio latency setting (Low/Medium/High must still pick LowLatency/None/PowerSaving); `ff_speed` in the debug log's audio line and perf field 9 |
| `0d1eccb0` | `sanitizeAscii()`, `formatOptional()`, `PackedTouch` | The `core start: cart title=` line; `n/a` fields in the audio line; touch input, with and without a Lua script running |
| `1f8afac7` | UPPER_SNAKE renames (`REWIND_BUFFER_SIZE`, `MIN_DRIFT_SECONDS`, `MAX_DATA_LENGTH`, SaveManager's two) | Compile errors only |
| `7ac57ee9` | Native getters for the perf sample field count, audio totals count and event data size; Kotlin `check()`s them when `MelonEmulator` is first used and in `EmulatorMessageQueue.start()` | A failed check throws `IllegalStateException` at emulator start, which is the point |

The tag mapping: `melonDS` → `melonade` (core messages while the debug log is off, and anything general), `melonDS-core` → `melonade-core`, `melonDS-lua` → `melonade-lua` (native and the two Kotlin Lua stores), `OpenGL` → `melonade-gl`, `melonade-cpu` and `melonade-audio` unchanged, and `melonade-debug` now also carries the native debug-only lines (CAPWAIT, REWIND, CPU pinning, the switch summary). The debug log recorder's header lists the tags.

## What Phase 3 changed, in the order it should be tested

| Commit | Change | What can break |
|---|---|---|
| `4cd096a6` | `LOG_DEBUGLOG(tag, ...)` in `debug/DebugLog.h`, used for the five debug-log-only lines in the engine | Nothing visible; the lines still only appear while the debug log is on |
| `fc53a1ab` | `luaSetBoolRequest<&LuaScript::x>` for swap/show_controls/fast_forward; `queueScriptSwitch()`; `LuaScript::clearRequests()` | Screen swap, controls and fast-forward requests from a script; disable/enable/restart from the Mods tab |
| `9e914df5` | `raiseLimitError()` (ends in `std::abort()` after `luaL_error`, which never returns); `findInFolder()`; `callSavestateHost()` | A script hitting the instruction, work or draw limit must be switched off with the right message; `require` and `melon.image.load` lookups; `melon.state` save and load |
| `a090716f` | **The split.** `LuaEngine.cpp` keeps lifecycle, callbacks, `setupScript` and the game/input/screen/emu functions. New: `LuaEngineInternal.h` (`LuaScript`, `LuaDrawCommand`, `LuaImage`, `getScript()`, the API structs), `LuaSandbox.cpp`, `LuaMemoryApi.cpp`, `LuaDrawApi.cpp`, `LuaFolderApi.cpp`, `LuaStateApi.cpp`, `LuaStorageApi.cpp`; CMakeLists lists them | Link errors; a missing function in a `melon.*` table (every API table was moved by line range, so compare `melon.mem.arm7`, `draw`, `image`, `state`, `storage` against MODS.md); the image metatable (`img:size()` and `getmetatable(img) == false`) |
| `9c59d358` | `addDrawCommand()` takes type, screen, geometry and colour | Every `melon.draw` call, text with a background, images with `src`/`scale`/`alpha` |
| `3da285ca` | `LuaEngine.h` class comment | Nothing |
| `ac3868da` | `kOptionType*` constants in `LuaScriptingJNI.cpp` | Options of each type (boolean, integer, number, string, layout) reaching `melon.options` |
| `90b696be` | Test recipe to MODS.md "Testing" | Nothing |

Design notes a reviewer will ask about: the API files are `struct LuaXxxApi { static const luaL_Reg table[]; private: static int fn(lua_State*); }`, and `LuaEngine` befriends the four that read its private state (`nds`, `isDrawingAllowed`, `folderContent`, the savestate fields); `LuaStorageApi` and `LuaSandbox` need no friendship. `setupScript` registers the moved tables with `newLibrary()` (`lua_newtable` + `luaL_setfuncs`) because `luaL_newlib` needs the array's size, which an out-of-line static member doesn't expose. `LuaSandbox::open()` pops each library table instead of the old `lua_settop(L, 1)`, and the image metatable is now made in `LuaFolderApi::install()` before the `melon` table instead of after it; neither is observable from a script.

## Phone checklist for Phases 2 and 3 (do together)

1. Debug and release builds, the JVM tests and `LuaStorageTest`.
2. A game under OpenGL and compute, and a reset. Touch with and without a Lua script running (PackedTouch).
3. Fast-forward on and off at a limited speed and unlimited; change a setting from the pause menu while fast-forwarding; pause and resume with frame pacing on.
4. Each `setprop debug.melonade.*` switch once (debug build): the `melonade-debug` summary line should show it. `cpusemu`/`cpus2d` pin lines are under the same tag.
5. The audio latency setting at each value: the `audio stream opened` line's `performance=` and `requested_latency=` fields.
6. One debug log session and one perf log session; compare the summary and the audio line against a pre-Phase-2 run (`ff_speed`, perf field 9, the `n/a` fields).
7. Phase 3: a script that uses every part of the module once: `melon.mem` reads and writes on both CPUs (including a TCM address on the ARM9), every `melon.draw` call, `require` of a module in a subfolder (and a `require` loop, which must error with the chain), `melon.image.load` and `melon.image.new` drawn with `src`/`scale`/`alpha`, `melon.state` save, `has` and load, `melon.storage` set/get/keys/flush, `melon.screen.swap`, `melon.emu.fast_forward` and `set_layout`, `melon.log` with the debug log on. Then a script that runs a long loop and one that draws over 4096 times: both must be switched off with the limit's message in the app.
8. Phase 3: options of every type (boolean, integer, number, string, layout) read from `melon.options`; disable, enable and restart with new options from the Mods tab; a reset with a running script.

## Phase 4 (next), notes for whoever starts it

- All Kotlin. The plan section lists the work under five headings (diagnostics, emulator UI, layout editor and shared views, Compose rows, settings and package placement) and `docs/polish-findings/kotlin-*.md` have the `path:line` references. Its verification needs the emulator-tester agent on the foldable plus a perf-log and debug-log session each, so unlike Phases 2 and 3 it can't be checked in a container without the SDK; if the SDK is still unavailable, do the diagnostics split and the pure refactors first (they're testable by the JVM tests) and leave the UI items for a session that can run the app.
- `LuaScriptOptionValue.TYPE_*` → `enum class LuaScriptOptionType(nativeId)` (plan: "Domain/package placement") pairs with Phase 3's `kOptionType*` constants in `LuaScriptingJNI.cpp`: keep the native ids 0..4 as they are.
- Don't touch `app/src/main/cpp/lua/` (vendored) or the submodules.

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
