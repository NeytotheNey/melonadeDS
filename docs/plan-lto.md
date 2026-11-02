# Plan: try link-time optimization

Status: not started (planned 2026-11-02).

## Why

The native code is compiled one `.cpp` file at a time, so the compiler can't inline a function from one file into another or see that an argument is always the same. Link-time optimization (LTO) optimizes the whole program at the link step instead. The emulator core calls across files many times per frame (CPU interpreter, memory access, GPU), so it may gain a few percent there. It may also gain nothing. This plan measures it on the phone and keeps it only if it pays.

## What's true today

No build has LTO. `app/CMakeLists.txt` and `app/build.gradle.kts` never ask for it. The core's own `CMakeLists.txt` has `ENABLE_LTO_RELEASE` (on by default) and `ENABLE_LTO` (off), but the first only sets `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`, which applies to CMake's `Release` configuration. Gradle builds both debug and release APKs as `RelWithDebInfo` (debug through `-DCMAKE_BUILD_TYPE=$nativeBuildType`, release by AGP's default), so it never applies. On 2026-11-02 none of the `app/.cxx/*/arm64-v8a/build.ninja` files contained `-flto`.

## Steps

1. **Baseline.** On the Z Fold 8, with the current debug APK, record perf log sessions (Settings → Performance → Diagnostics) in a 3D-heavy scene and a 2D-heavy one, e.g. Phantom Hourglass in a busy area and White 2's overworld. Use OpenGL and compute, start the game fresh for every run, and do at least two runs per case. Compare in cycles per frame rather than ms, and check logcat for `limitCPUFreq` and `thermal_core` before trusting a slow run (both from the perf logger workflow in `PERF-NOTES.md`).
2. **Add an opt-in switch.** Add a Gradle property, e.g. `-PnativeLto=true`, that turns IPO on for every native target, the core and `melonDS-android-frontend` alike, so calls between the static core library and the frontend can be inlined too. The simplest route is `set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)` in `app/CMakeLists.txt` before the `add_subdirectory` calls. Two things to check: the app's `cmake_minimum_required(VERSION 3.4.1)` predates policy CMP0069, so IPO may be ignored until that policy is set to NEW or the minimum is raised to 3.9; and which LTO flavor CMake picks for clang. Prefer ThinLTO (`-flto=thin`), which links much faster than full LTO; if CMake picks full LTO, set the flags directly instead. Confirm with `-flto` showing up in `build.ninja`.
3. **Build and sanity-check.** Build the debug APK with the switch, run the JVM tests and `LuaStorageTest`, and boot a game under OpenGL, compute and software. Watch for link errors and for JNI symbols going missing: every `JNIEXPORT` function must still be exported (`llvm-nm -D` on the built `.so`). The JIT emits its code at run time, so LTO doesn't change it, but check that the JIT still turns on.
4. **Measure.** Repeat step 1's runs with the LTO build. Also note the `.so` size, a clean native build's time, and an incremental rebuild after touching one core file.
5. **Decide.** Keep it if the heavy scenes use clearly fewer cycles per frame (a few percent or more, beyond run-to-run noise) with no regressions. If the gain is real but the incremental build gets painfully slow, turn it on for release builds only. If there's no gain, leave the switch off, record the numbers and close this plan.

Results go in `PERF-NOTES.md` (local) as conclusions, and the decision goes here.

## Later

If LTO pays, profile-guided optimization (PGO: build once with instrumentation, play a session, rebuild using the profile) is the next step up. It's a bigger job because the profile has to be collected on the phone and kept current.
