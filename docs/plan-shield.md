# NVIDIA Shield TV optimization

Plan written 11-14 after the first Shield runs (`perf_logs/2026-11-14-shield/`). The Shield TV Pro (Tegra X1+, 4x Cortex-A57 at 2.0 GHz, ARMv8.0, Maxwell GPU, GLES 3.2, Android 11, 1080p output) is the slowest CPU we've tested, but its GPU is fine. The goal is White 2 at 60 FPS on the 3D title screen, and finding out what holds it back so the fixes help other older phones too. Not a supported device yet, just a curiosity run that turned up useful data.

## Where it stands

White 2, one 30 s pass each, `capreads=1`, threaded 2D on, built with `-PnativeArmv82=false`:

| Scene | GL 4x | GL 8x | GL 16x | Compute 4x | Compute 8x | Compute 16x |
|---|---|---|---|---|---|---|
| Overworld fps | 59.9 | 58.2 | 32.8 | 60.0 | 59.6 | 59.8 |
| Title fps | 35.7 | 35.7 | 20.2 | 42.9 | 29.0 | 17.2 |
| Title emu work ms | 28.0 | 28.0 | 48.4 | 21.1 | 30.9 | 58.1 |

What the run files show:

- **The OpenGL title is CPU-bound.** At GL 4x the emulator thread is about 90% busy (905 ms of CPU a second at 2014 MHz) and takes 28 ms a frame, with no sleep. The same scene costs the OnePlus about 15.5 ms on-CPU. The other three cores are mostly idle: the process uses about 1.05 cores.
- **The Compute title at 8x and up waits on the GPU.** At Compute 8x the emulator thread is only about 61% busy. `CAPWAIT` shows 21.2 ms of capture-readback wait plus 6.1 ms of `capspin` per frame. At 4x Compute beats OpenGL (43 vs 36 fps) because it takes the 3D work off the CPU.
- **The overworld is fine** except GL 16x. Its emu work rises to 30 ms, which includes waits on the GPU, so it's probably GPU-bound. Not confirmed: the Shield has no kgsl and blocks the GPU devfreq and thermal sysfs without root, so the logger has no GPU time, clocks or temperatures.
- **Compute 8x title had a 3.9 fps 1% low.** From one pass, so it could be shader compiling or a one-off.
- **No framebuffer fetch on Tegra** (neither ARM nor EXT). That costs nothing today: the default `gltrans=3` path doesn't use fetch.
- **The default APK doesn't run at all.** It crashes at startup with SIGILL, see step 0.

On a 1080p TV, 4x (1024x768 per screen) already covers about the output's height. 8x and 16x are supersampling there. So 4x is the scale that matters most on the Shield. 16x is a stress test, not a goal.

## Step 0: the CPU target (tentative: roll back armv8.2)

Since the 11-03 Fold profile every arm64 build uses `-march=armv8.2-a` (`nativeArmv82` in `app/build.gradle.kts`, default on; `MELONADE_ARMV82` in `app/CMakeLists.txt`). Nothing checks the CPU at startup, so debug and release builds crash at launch on any ARMv8.0 device: Cortex-A53, A57, A72 and A73 chips that still meet minSdk 29. The `armeabi-v7a` library in GitHub builds doesn't help, because Android loads the arm64 one.

What we get for it is small. All three build flags together (`-O3`, ThinLTO, armv8.2) cut emulator-thread work by 2.4% with no fps change (PERF-NOTES "Native build flags"). armv8.2 alone was never measured. No code in the core or the JIT uses LSE atomics or dotprod directly.

**Tentative decision (11-14): turn `nativeArmv82` off by default**, and keep `-PnativeArmv82=true` for perf builds on our own phones. Not done yet. Before making it final:

1. Measure armv8.2 alone on the Fold, White 2 title, on vs off with `-O3` and LTO kept on. Use the quick recipe unless the gap is under 1%.
2. If it's worth keeping, the other option is a startup check (`getauxval(AT_HWCAP)` for `HWCAP_ATOMICS`) that shows an "unsupported CPU" message instead of crashing. That still locks those devices out, so it's only worth it if the gain is real.

Until then, every Shield build needs `-PnativeArmv82=false`.

## Step 1: measure what the logger can't see

No code changes. Its only job is to make steps 2 and 3 aim at the right thing:

- **simpleperf on the emulator thread**, GL 4x title. The debug build is debuggable, so app profiling should work without root (check this first). Split the 28 ms the way the OnePlus profile did: bus, geometry, 2D, driver, JIT. Reuse `perf_logs/simpleperf/scene/` scripts.
- **`gltimers`** for GPU time per GL pass, GL 4x and GL 16x title and overworld. This answers whether GL 16x is GPU-bound.
- **`computetimers`** at Compute 4x and 8x title, to see how much of the 21 ms capture wait is real GPU work.
- **Compute 8x title again, two passes**, to see if the 3.9 fps 1% low repeats.
- **Compute 2x and 6x title**, one pass each, to find the scale where the readback wait starts to grow.

## Step 2: the OpenGL title on a slow CPU

Fix what step 1 finds. Candidates from the OnePlus profile (PERF-NOTES "OnePlus 7 Pro: what limits the capture scene"), where bus was 30%, geometry 21%, 2D 16% and driver 14% of the emulator thread:

- **Let threaded 2D work while capture is armed.** It sits idle then today, and the Shield has three spare cores. This is the biggest likely win for any phone with weak cores. It needs care, since capture reads the 2D output.
- **The hot spots in [plan-optimization.md](plan-optimization.md)**, now with a second, slower CPU to check them on.
- **Driver time.** Count GL calls per frame on the title with `scenestats` and see if any are redundant state changes.

Every change here helps all phones, so test it on the OnePlus as well as the Shield.

## Step 3: Compute capture waits

Add the Shield to [plan-compute-efficiency.md](plan-compute-efficiency.md) as a second test device. It's the first non-Adreno, non-Mali GPU we have. Its question is different from the OnePlus: the GPU keeps up at 4x, but at 8x the emulator thread waits on the capture readback.

- Check whether the readback can be deferred a frame, or read at 1x when hi-res capture is off.
- Check whether `capspin`'s 6 ms of spinning helps on Tegra or just burns CPU. It may need a per-GPU setting.

## Step 4: defaults for the Shield (after steps 2 and 3)

Only once the numbers are in. The current data says Compute 4x is the best setup on the title and holds 60 in the overworld up to 16x. Whether that becomes a default for Tegra, or a tip in the settings, depends on what steps 2 and 3 change.

## Out of scope

Android TV support itself: a leanback launcher entry, D-pad navigation for the whole UI, controller setup. The app runs on the Shield, but the UI was driven over adb. That's its own plan if the Shield becomes a real target.

## Testing notes

- adb: `adb connect 192.168.1.87:5555` (the TV's plain network debugging, not TLS, so it doesn't show in `adb mdns services`).
- Build with `-PnativeArmv82=false` until step 0 is decided.
- White 2's 3D title shows up 60 to 70 s after a cold launch. Title runs wait 75 s, then measure 30 s, and check the end screenshot shows "PRESS START".
- Overworld: save state slot 3. Restore the pristine `.sav` from `perf_logs/2026-11-14-shield/backup/` before each run.
- `input tap` doesn't work in the DocumentsUI file picker. Use D-pad key events there.
- Screenshots come out 3840x2160 even with the 1080p override.
- `make_runs_csv.py` has no Shield entry in its device map yet.
- Scripts: `perf_logs/2026-11-14-shield/scripts/` (`shieldrun.sh`, `chain_shield.sh`, `shieldsum.py`).
