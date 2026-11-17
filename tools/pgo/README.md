# PGO (profile-guided optimization)

Clang instrumentation PGO for the arm64-v8a native code (frontend, core and libraries). You build an instrumented APK, play on the phone, merge the counters it writes into a profile, and rebuild with that profile. Other ABIs build as if PGO were off.

## 1. Build the instrumented APK

```
./gradlew assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false -PnativePgo=generate
```

This adds `-fprofile-generate` to every compile and link, and `MELONADE_PGO_GENERATE` to the frontend. The instrumented build is slower than a normal one, so don't take perf numbers from it.

## 2. Collect

Install it and play the scenes you care about. Android kills the app instead of letting it exit, so the profile runtime's own dump at exit never happens; the frontend writes the counters itself every time the emulation pauses (pause menu, back, leaving the app, a save or load state) and when it stops. Each dump goes to its own file and resets the counters, so nothing is counted twice:

```
/data/user/0/me.magnum.melonds.dev/files/pgo/<pid>-<process start time>-<n>.profraw
```

Each dump logs one line under `melonade-debug` (`PGO: dumped profile on pause to ...`). Pause or leave the game before you kill the app, or the counters since the last pause are lost.

## 3. Merge

```
tools/pgo/merge.sh <adb serial>
```

Pulls every `.profraw` through `run-as` into `logs/pgo-raw/<time>/` and merges them with the NDK's own `llvm-profdata` into `app/pgo/arm64-v8a.profdata`. `--clear` deletes the phone's files after a good merge, `--local <dir>` merges files already on the PC, and `OUT=<path>` writes the profile somewhere else. The files pile up on the phone until you clear them, so a later merge includes the earlier sessions too.

## 4. Build with the profile

```
./gradlew assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false -PnativePgo=use
```

Build it the same way as the generate build: the same variant (debug vs release, so the same `MELONADE_DEBUG_TOOLS`), the same `-PnativeBuildType`, and the same `-PnativeO3/-PnativeLto/-PnativeArmv82`. Those change the code clang sees, and functions whose control flow doesn't match the profile are dropped silently (the out-of-date and mismatch warnings are turned off), so a mismatched build quietly loses most of the profile.

`-PnativePgoProfile=<path>` (relative to the repo root) uses another profile. The configure step fails if the profile is missing. A changed profile re-runs the configure and recompiles everything.

## Stale profiles

The profile matches functions by their control flow. When code changes, clang skips the functions that no longer match (without a warning, those are turned off) and still uses the rest, so an old profile slowly stops helping rather than breaking the build. Re-collect after big changes to the hot paths (the JIT, the 2D and 3D renderers, the scheduler).
