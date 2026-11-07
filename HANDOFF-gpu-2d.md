# Handoff: GPU 2D (filtered, hi-res and wide 2D)

The plan, the design decisions and all results so far are in [docs/plan-gpu-2d.md](docs/plan-gpu-2d.md). Read that first. This file is the current state and the next steps. Build, install and phone-connection commands are in `HANDOFF.md`.

## State (11-07)

Branch `gpu-2d` in the app (from `material3`) and in the core (from `melonade`). **Committed, not pushed:** core `57c1e735` (counters) and `97341228` (filter), app `87f008eb` (setting and switches) and `7a52c106` (plan and handoff). Neither branch has an upstream yet.

- **Step 1, counters:** done. `debug.melonade.gpu2dstats`, in the core's `src/GPU_2DStats.h` plus hooks in `GPU.cpp`/`GPU.h`.
- **Step 2, filtered 2D:** works on both phones. All correctness checks passed on the OnePlus and the Fold (OpenGL and compute, mode 3 identity at 2x/4x/8x and wide). The Fold holds 60 fps in every scene with it on (measured plugged in). The open problem is cost on the OnePlus: White 2's title drops to about 37–45 fps, because its GPU is full and the capture readback waits behind it. Details in the plan doc.
  - Core: `GPU_OpenGL.cpp/.h` (`PrepareFilter2D`, `RunFilter2D`, `FreeFilter2D*`), `GPU_OpenGL_shaders.h` (key and MMPX passes, `FILTER2D` compositor variant), `GPU.h` (`Filter2D`, `Filter2DLogging`).
  - App: the "Smooth 2D edges" setting, plumbed like "Upscaled display capture", and the `filter2d` switch in `DebugProperties`.
- **Not part of this work, uncommitted in the app:** `docs/shelved/compute-on-mali.md` and its `docs/README.md` line, `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties`. Leave them out of these commits unless the user says otherwise.

How filtered 2D works, in one line: a pre-pass runs MMPX over a key image of the 2D planes and writes a map from each sub-pixel to a native pixel. The compositor reads planes 0–2 through that map and runs `CompositePixel` unchanged, so blending and 3D stay exact, and 3D is still fetched at the real position.

## Next up

1. **Two savings, tested on both phones** (plan doc, "Two savings"). Both phones have a build with both; the old APK is in `perf_logs/2026-11-07-filter2d/savings/old-apk-*`.
   - **Deband early-out:** committed in the app (`cfdcf4f9`).
   - **Filter skip:** on hold, on core branch `gpu-2d-filter-skip` (`72104fd7`), not in `gpu-2d`. It makes White 2 stutter on the OnePlus. The user chose to revisit it after the filter is moved off the readback's critical path (item 2).
2. **White 2's real limit is the line-192 readback stall** (see the plan doc, "What sets the pace"). Next options for the filter: the quarter-size second pass the filter coder proposed (512x768 writing four results, picked by sub-pixel parity), and running the passes off the readback's critical path. A non-blocking capture readback would help every capture game, filter or not, but needs care with accuracy.
3. **Optional: a long Fold run plugged in** (20–30 min of gameplay, filter on, main screen), logging fps, clocks, SIOP and `limitCPUFreq`. The user treats plugged in as the worse thermal case. The 70 s runs so far never got hot enough to throttle.
4. **Push once the user agrees** (core first, then the app). Extra commits for the cheaper filter go on the same branches.
5. **Step 3, the shadow renderer**, starts after filtered 2D is settled. Before designing it, run the counters over real gameplay, not title screens. Golden Sun Dark Dawn is the case to design for, with lots of rotated layers and lots of mid-frame changes.

## Testing notes

- **Both phones can be used.** Pass `-s` on every adb command. The Fold is serial `R5GL64A7LKN`, 192.168.1.32, and its connect port comes from `adb mdns services`. The OnePlus is GM1917 at 192.168.1.214; use `adb tcpip 5555` once connected. Both now have the filter build.
- **The OnePlus is a dev phone:** it stays on power and has no secure lock screen, but a swipe lock screen can still come up (`wm dismiss-keyguard`). Charging can change clocks and heat, so mark numbers as plugged in or unplug it.
- **Protect saves on both phones:** back up each `.sav`, prefs and `rom_data.json` first. Never load a save state, since that overwrites the real `.sav`. Never uninstall or `pm clear`. Compare and restore at the end. The latest backups are in `perf_logs/2026-11-07-filter2d/backup-fold/` and `backup-oneplus/`.
- **Recipes and data:**
  - OnePlus cost runs: `perf_logs/2026-11-07-filter2d/` (`f2run.sh`, `f2sum.py`, `cost_table.md`).
  - Fold cost runs: `perf_logs/2026-11-07-filter2d/fold/` (`f2frun.sh`, `f2fsum.py`, `cost_table_fold.md`). The Fold has no perfetto GPU counters; its GPU column is kgsl busy × clock.
  - Capture timing: `perf_logs/2026-11-07-filter2d/capreads/` (`capsum.py`).
  - Counter logs: `logs/emulator/gpu2dstats/`. Screenshots and crops: `logs/phone/filter2d/` (Fold in `fold/`).
- **The AVD** has no GL renderer, so it can only run the counters.

## Leftovers

- **OnePlus scratch files, need the user's OK to delete:** the 60 `f2_*` files from the first run plus `f2_crw_*`, `f2_crn_*`, `f2_crchain*.log`, `f2_b*`, `bl_*` and `f2_work/bl_gpuprod.log` in `/data/local/tmp`, and the matching `.pftrace` files (plus `bl_probe.pftrace`) in `/data/misc/perfetto-traces`. Everything is already pulled. Older ones from 11-06 are listed in `HANDOFF.md`.
- **OnePlus setting:** a tester ran `svc power stayon true`, so `stay_on_while_plugged_in` is now 7. The old value wasn't recorded.
- **App logs from the test runs:** about 150 perf/debug logs in the Fold's `Documents/2DS/Logs` (11-07 09:2x–10:3x) and in the OnePlus's app `perf_logs` folder.
- **AVD:** stray TWEWY files (PNGs, `.nds`, `.ml1`, `.sav`) in `/sdcard/Games`.
- **A possible existing bug, not ours to fix here:** the compositor reads with mediump `fTexcoord`, which may at 8x non-wide show a black last column or a last row from the other screen. It's credible from the code, but nobody has seen it on a phone. The filter path already avoids it.
- **Small MMPX quirks** on fine detail, like a hollow diamond on Advance Wars' lion. That's how MMPX looks.
