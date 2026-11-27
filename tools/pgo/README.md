# PGO (profile-guided optimization)

Clang instrumentation PGO for the arm64-v8a native code (frontend, core and libraries). You build an instrumented APK, play on the phone, merge the counters it writes into a profile, and rebuild with that profile. Other ABIs (armeabi-v7a, x86_64) build as if PGO were off.

PGO is off by default for both release and debug builds, and CI builds without it. Pass `-PnativePgo=generate|use|off` to pick a mode for either. `use` reads `app/pgo/arm64-v8a-release.profdata` for release and `app/pgo/arm64-v8a.profdata` for debug; both are git-ignored and stay local. Steps 1-4 below are the debug build; "Release builds" further down is the one that ships.

## 1. Build the instrumented APK

```
./gradlew assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false -PnativePgo=generate
```

This adds `-fprofile-generate` to every compile and link, and `MELONADE_PGO_GENERATE` to the frontend. The instrumented build is slower than a normal one, so don't take perf numbers from it.

## 2. Collect

Install it and play the scenes you care about. Android kills the app instead of letting it exit, so the profile runtime's own dump at exit never happens; the frontend writes the counters itself every time the emulation pauses (pause menu, back, leaving the app, a save or load state) and when it stops. Each dump goes to its own file and resets the counters, so nothing is counted twice. The hooks (`perf/PgoDump.cpp`) are called in every build and are empty outside generate builds, so their callers compile the same way in generate and use builds and keep their profile. The files go to the app's external files dir, which adb can read without `run-as` (internal storage if the phone has none):

```
/sdcard/Android/data/io.github.neytotheney.melonadeds.dev/files/pgo/<pid>-<process start time>-<n>.profraw
```

Each dump logs one line under `melonade-debug` (`PGO: dumped profile on pause to ...`). Pause or leave the game before you kill the app, or the counters since the last pause are lost.

## 3. Merge

```
tools/pgo/merge.sh <adb serial>
```

Pulls every `.profraw` (from the external files dir, plus `files/pgo/` through `run-as` where older generate builds wrote) into `logs/pgo-raw/<time>/` and merges them with the NDK's own `llvm-profdata` into `app/pgo/arm64-v8a.profdata`. `--clear` deletes the phone's files after a good merge, `--local <dir>` merges files already on the PC, `PKG=<package>` pulls another package, and `OUT=<path>` writes the profile somewhere else. The files pile up on the phone until you clear them, so a later merge includes the earlier sessions too.

## 4. Build with the profile

```
./gradlew assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false -PnativePgo=use
```

Build it the same way as the generate build: the same variant (debug vs release, so the same `MELONADE_DEBUG_TOOLS`), the same `-PnativeBuildType`, and the same `-PnativeO3/-PnativeLto/-PnativeArmv82`. Those change the code clang sees, and functions whose control flow doesn't match the profile are dropped silently (the out-of-date and mismatch warnings are turned off), so a mismatched build quietly loses most of the profile.

`-PnativePgoProfile=<path>` (relative to the repo root) uses another profile. The configure step fails if the profile is missing. A changed profile re-runs the configure and recompiles everything.

## Release builds

The shipped APK is a release build, so its profile has to come from a release generate build: release has no `MELONADE_DEBUG_TOOLS`, so a debug profile misses every function that code touches. Both build types are RelWithDebInfo (`-PnativeBuildType` only changes debug). Build it the same way as the release you ship apart from `-PnativePgo` (the same `-PnativeO3/-PnativeLto/-PnativeArmv82`). Generate and use builds both name static functions by file name rather than full path (`-static-func-full-module-prefix=false`), so a profile merged on this PC also matches in CI's checkout.

1. Build the training APK:

   ```
   ./gradlew assembleGitHubProdRelease -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false -PnativePgo=generate
   ```

   The APK is `app/build/intermediates/apk/gitHubProd/release/app-gitHub-prod-release-pgo.apk` (`-pgo` so it can't be mistaken for the shipping APK; `-PsideloadRelease` builds get the same name). A release generate build gets the ID `io.github.neytotheney.melonadeds.pgo` and is signed with the debug key, so it installs beside the real app (the OnePlus has the Play Store's `me.magnum.melonds`) and needs no release keystore. Neither reaches the native compile. It isn't debuggable and has no debug switch field, so set the scenes up through normal settings. It's a separate app with its own settings and data: pick the ROM folder, set the renderer and scale, and point it at the savestates it needs. Loading a state rewrites that game's save, so back the save up if it's shared with another install. A nightly-flavor training build is `io.github.neytotheney.melonadeds.nightly.pgo` (pass `PKG=` to merge.sh); its native code is the same as prod's.

2. Train on both GPU families, since one phone's profile marks the other's paths cold (PERF-NOTES "PGO": a Pixel-only profile made the OnePlus's Compute 2x slower). About 10 minutes each, threaded 2D off, pausing or leaving the game every few minutes and before you stop:
   - **Pixel 8** (Mali): White 2 title on OpenGL 4x and Compute 4x, the Dark Dawn capture scene on OpenGL 4x, the White 2 overworld (state 3) on OpenGL 4x, Phantom Hourglass, NSMB and Mario Kart titles.
   - **OnePlus 7 Pro** (Adreno): White 2 on Compute 2x and OpenGL 4x, Dark Dawn on OpenGL 4x, the White 2 overworld.

3. Merge both phones into one profile. Pull each phone, then merge the two raw folders together:

   ```
   tools/pgo/merge.sh 39071FDJH002W8 --release
   tools/pgo/merge.sh 192.168.1.214:5555 --release
   mkdir -p logs/pgo-raw/release-combined && cp logs/pgo-raw/<pixel time>/*.profraw logs/pgo-raw/<oneplus time>/*.profraw logs/pgo-raw/release-combined/
   tools/pgo/merge.sh --local logs/pgo-raw/release-combined --release
   ```

   Each merge writes `app/pgo/arm64-v8a-release.profdata`, so the last one is the combined profile. Add `--clear` to the first two once you're happy, or the files stay on the phones and the next round includes them.

4. Keep `app/pgo/arm64-v8a-release.profdata` local (~2.6 MB, git-ignored). It isn't committed, so CI's release and nightly builds stay without PGO.

5. Build release with `-PnativePgo=use` to use the profile. The configure step logs `MELONADE_PGO=use with ... (md5 ...)`, and `.cxx/<build type>/<hash>/arm64-v8a/build.ninja` has `-fprofile-use=` in every compile line.

   To A/B it on a phone first, build the use APK with `-PsideloadRelease` (step 1's command with `-PnativePgo=use` instead of `generate`). It gets the training build's ID and debug signing, so it replaces the training build and keeps its data; native flags are the same as the shipped release. Without `-PsideloadRelease` a local release build needs the release keystore in `local.properties`. An off build to compare against: leave out `-PnativePgo` (off is the default).

## Stale profiles

The profile matches functions by their control flow. When code changes, clang skips the functions that no longer match (without a warning, those are turned off) and still uses the rest, so an old profile slowly stops helping rather than breaking the build. Re-collect after big changes to the hot paths (the JIT, the 2D and 3D renderers, the scheduler).
