# Plan: an optimization session

Status: not started (planned 2026-11-02). Build flags first, since they're cheap and share one baseline; then the code-level items, biggest first.

## Why, and what to expect

On the Z Fold 8 the emulator thread uses about 10 ms of a 16.7 ms frame (Phase 0 profile in `PERF-NOTES.md`, 2026-11-01), so games already hold 60 there. The gains this plan is after are battery and heat, weaker phones (the OnePlus 7 Pro manages 26 fps in the compute capture scene at 4x), and the heaviest menus that aren't perfectly smooth. Keep a change only if it measurably helps one of those.

Shares of the emulator thread from that profile (OpenGL 4x, threaded 2D off): software 2D 33%, bus/memory/interpreter 29%, 3D geometry 11%, Adreno driver 10%, JIT code 8%.

## Baseline (do once, before any change)

On the Z Fold 8 with the current debug APK, record perf log sessions (Settings → Performance → Diagnostics) in a 3D-heavy scene and a 2D-heavy one, e.g. Phantom Hourglass in a busy area and White 2's overworld at the Floccesy Ranch slot used for Phase 0. Use OpenGL and compute, start the game fresh for every run, run unplugged, and do at least two runs per case. Compare in cycles per frame rather than ms, and check logcat for `limitCPUFreq` and `thermal_core` before trusting a slow run (the perf logger workflow in `PERF-NOTES.md`). If the OnePlus 7 Pro is reachable, repeat the compute capture scene there too, since that's where gains would show most.

## Part 1: build flags

What's true today: every native build is `RelWithDebInfo`, which is `-O2 -g`, for both debug and release APKs (debug through `-DCMAKE_BUILD_TYPE=$nativeBuildType`, release by AGP's default). The core's `CMakeLists.txt` and `cmake/DefaultBuildFlags.cmake` turn on `-O3` and LTO only for CMake's `Release` configuration, which we never use. On 2026-11-02 none of the `app/.cxx/*/arm64-v8a/build.ninja` files contained `-flto`, and every compile line ended in `-O2`. The CPU target is the NDK's default, generic armv8-a.

Add each flag behind its own opt-in Gradle property so they can be measured one at a time and together:

1. **`-O3`** (e.g. `-PnativeO3=true`): raise the optimization level for every native target, the core included. It mostly helps loops the compiler can vectorize, like the software 2D renderer. Check that the flag really lands on the core's compile lines, since the core sets its own per-configuration flags.
2. **LTO** (e.g. `-PnativeLto=true`): link-time optimization lets the compiler inline across files, including between the static core library and `melonDS-android-frontend`. The simplest route is `set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)` in `app/CMakeLists.txt` before the `add_subdirectory` calls. Two things to check: the app's `cmake_minimum_required(VERSION 3.4.1)` predates policy CMP0069, so IPO may be ignored until that policy is set to NEW or the minimum is raised to 3.9; and which LTO flavor CMake picks for clang. Prefer ThinLTO (`-flto=thin`), which links much faster than full LTO; if CMake picks full LTO, set the flags directly. Confirm `-flto` shows up in `build.ninja`.
3. **A newer CPU target** (e.g. `-PnativeArmv82=true`, adding `-march=armv8.2-a` for arm64 only): both test phones support armv8.2, which adds faster atomics (LSE) and a few other instructions. The catch is that the APK would no longer run on armv8.0 phones (older Cortex-A53/A73 SoCs) that still meet minSdk 29. Measure it, but keeping it is the user's call, not just a numbers call. The 32-bit `armeabi-v7a` build on GitHub releases is unaffected.

For each variant: build the debug APK, run the JVM tests and `LuaStorageTest`, and boot a game under OpenGL, compute and software. Watch for link errors and for JNI symbols going missing: every `JNIEXPORT` function must still be exported (`llvm-nm -D` on the built `.so`). The JIT emits its code at run time, so these flags don't change it, but check that it still turns on. Then repeat the baseline runs, and note the `.so` size, a clean native build's time, and an incremental rebuild after touching one core file.

Keep a flag if the heavy scenes use clearly fewer cycles per frame (a few percent or more, beyond run-to-run noise) with no regressions. If a flag pays but makes incremental builds painfully slow (most likely LTO), turn it on for release builds only. Record the numbers either way.

**Later:** if LTO pays, profile-guided optimization (PGO: build once with instrumentation, play a session, rebuild using the profile) is the next step up. It's a bigger job because the profile has to be collected on the phone and kept current.

## Part 2: code-level, biggest first

4. **Threaded 2D's busy-wait.** With threaded 2D on, the 2D work leaves the emulator thread and the average frame drops (9.75 → 9.30 ms), but p95 gets worse (11.9 → 13.4 ms) and `GPU::Wait2DJobs` busy-waits for 11% of the emulator thread. Find out what it waits for and whether the wait can be shorter, a proper sleep, or overlapped with other work. This is likely the biggest single gain, and it may let threaded 2D become the default.
5. **Held 2D lines in capture scenes.** Move the lines the 2D renderer holds in capture scenes to the worker thread too (an open item in `PERF-NOTES.md`). Check against the hi-res capture work in [hires-capture.md](hires-capture.md) first, since both touch the capture path.
6. **The emulator thread's CPU clock.** The thread is only about 60% busy, so Samsung's power manager keeps its clock low, which costs smoothness more than average speed. Try the two ideas from the ADPF section of `PERF-NOTES.md`: an opt-in lower ADPF target, or spinning the end of the pacing wait. Also still open: why a second busy thread lowers the emulator thread's effective clock even on another cluster.
7. **Bus, memory and interpreter (29%).** The hot functions are `RunFrame`, `ARMv5::Execute`, `RunTimers`, `ARM9IOWrite32`, `SlowWrite9`, `SlowBlockTransfer9` and `LookUpBlock`. This is core code in the submodule, so changes are bigger and should stay close to upstream. Profile with call graphs first to see which callers send so much through the slow paths.
8. **Adreno driver time (10%).** Fewer GL calls and state changes per frame would trim it a little; the bigger answer is the Vulkan renderer in [plan-vulkan.md](plan-vulkan.md), which has its own gate.

Results go in `PERF-NOTES.md` (local) as conclusions, and each decision goes back into this file.
