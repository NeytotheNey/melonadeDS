# Handoff: the polish pass (PLAN-polish.md)

Read this, then `PLAN-polish.md`. The plan has the per-item file references, the phase order and the verification list per phase. This file says where things stand, what is untested, and what the next session does first.

## Where things stand (2026-11-01)

- **Branch:** `claude/friendly-heisenberg-3eipz2` in this repo, on top of `material3`. Phases 1 and 2 are applied, one commit per concern (21 for Phase 1, 9 for Phase 2), each followed by a status commit on the plan. The core submodule is untouched by the pass.
- **Phase 1 (delete and fix):** verified on the Z Fold 8 on 2026-11-01 (commit `4768a989` records what was checked: debug build, 147 JVM tests, `LuaStorageTest`, reset, rewind, fast-forward, a Wide pick, the four button styles, a Lua options save, the licence entries).
- **Phase 2 (native consolidation):** applied in `8a0a19ac..7ac57ee9`, **not yet built or run on a device**. The container it was written in has no Android SDK (Google's download host is blocked). Instead, every touched native file was checked with a host `clang++ -fsyntax-only` against the real core and oboe submodules, with stub Android headers, in both the debug (`MELONADE_DEBUG_TOOLS`) and release configurations. That catches syntax and most type errors, not linking or behaviour. Testing was deferred on purpose: the owner tests Phases 2 and 3 together.
- **Phases 3 to 7:** not started. Phase 3 (split `scripting/LuaEngine.cpp`) is next.

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

## Phone checklist for Phase 2 (do with Phase 3's)

1. Debug and release builds, the JVM tests and `LuaStorageTest`.
2. A game under OpenGL and compute, and a reset. Touch with and without a Lua script running (PackedTouch).
3. Fast-forward on and off at a limited speed and unlimited; change a setting from the pause menu while fast-forwarding; pause and resume with frame pacing on.
4. Each `setprop debug.melonade.*` switch once (debug build): the `melonade-debug` summary line should show it. `cpusemu`/`cpus2d` pin lines are under the same tag.
5. The audio latency setting at each value: the `audio stream opened` line's `performance=` and `requested_latency=` fields.
6. One debug log session and one perf log session; compare the summary and the audio line against a pre-Phase-2 run (`ff_speed`, perf field 9, the `n/a` fields).

## Phase 3 (next), notes for whoever starts it

- The plan section and `docs/polish-findings/native.md` section 2 and 3 list every helper to fold while splitting (`setBoolRequest`, `queueScriptSwitch`, `clearRequests`, `raiseLimitError`, `findInFolder`, `addDrawCommand` with geometry, `LOG_DEBUGLOG`, the `bad_alloc` wrappers), the file split by namespace, `LuaEngineInternal.h` for `Script`/`DrawCommand`, named option-type constants in `LuaScriptingJNI.cpp`, the class comment in `LuaEngine.h`, and the MSYS2 recipe moving from `app/src/test/cpp/LuaStorageTest.cpp` to MODS.md.
- `LuaEngine.cpp` already uses `LOG_TAG_LUA` through its local `TAG` constant (Phase 2); keep one tag constant per new file or share it from `LuaEngineInternal.h`.
- Don't touch `app/src/main/cpp/lua/` (vendored) or the submodules.
- `app/src/test/cpp/LuaStorageTest.cpp` is a host test built by hand; its build command is in its header comment until Phase 3 moves it.

## Syntax-checking native code without the SDK

Worked in the cloud container and is worth repeating for Phase 3. Needs the `melonDS-android-lib` and `app/src/main/cpp/oboe` submodules checked out (`git submodule update --init --depth 1 <path>`), `apt-get install libgles-dev libegl-dev`, a JDK for `jni.h`, and a stub directory with `android/log.h`, `sys/system_properties.h` (`PROP_VALUE_MAX 92`, `PROP_NAME_MAX 32`, `__system_property_get`), `android/asset_manager*.h`, `android/api-level.h`, `libslirp.h` (typedef `Slirp`, `SlirpCb`), `pcap/pcap.h`, and a `version.h` made from the core's `version.h.in`. Then:

```bash
clang++ -std=c++20 -fsyntax-only -w -include sys/types.h -include cstring -include condition_variable -include atomic -include memory -include cmath \
  -I <stubs> -I <jdk>/include -I <jdk>/include/linux -I app/src/main/cpp -I melonDS-android-lib/src -I melonDS-android-lib/src/frontend \
  -I melonDS-android-lib/src/net -I app/src/main/cpp/oboe/include -I app/src/main/cpp/lua -I app/src/main/cpp/rcheevos/include \
  -I app/src/main/cpp/faad2/include -I app/src/main/cpp/enet/include -DOGLRENDERER_ENABLED '-DMELONDS_GL_HEADER=<GLES3/gl32.h>' \
  -DEGL_EGLEXT_PROTOTYPES -DANDROID -D__ANDROID__ [-DMELONADE_DEBUG_TOOLS] app/src/main/cpp/<file>.cpp
```

The forced `-include`s stand in for headers the NDK pulls in transitively; they are not missing includes to fix.

## Housekeeping

- This file and `PLAN-polish.md` move under `docs/` in Phase 7, with the other plan and handoff files.
- `docs/polish-findings/` is the working list for Phases 3 to 6 and goes with the plan.
