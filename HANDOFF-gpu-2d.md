# Handoff: GPU 2D (filtered, hi-res and wide 2D)

The plan, the design decisions and all results so far are in [docs/plan-gpu-2d.md](docs/plan-gpu-2d.md). Read that first. This file is the current state and the next steps. Build, install and phone-connection commands are in `HANDOFF.md`.

## State (11-08)

Branch `gpu-2d` in the app (from `material3`) and in the core (from `melonade`). The core is at `0de067ee` (the filter skip) and the app commit after `5a4715c4` points to it. Check `git log origin/gpu-2d..gpu-2d` in both repos to see whether that's pushed yet.

- **Step 1, counters:** done. `debug.melonade.gpu2dstats`, in the core's `src/GPU_2DStats.h` plus hooks in `GPU.cpp`/`GPU.h`.
- **Step 2, filtered 2D:** works on both phones. All correctness checks passed on the OnePlus and the Fold (OpenGL and compute, mode 3 identity at 2x/4x/8x and wide). Its cost in capture scenes was a readback stall, and that's fixed by the Blit after the readback below. White 2's title now holds 60 fps with the filter on, on OpenGL on both phones.
  - Core: `GPU_OpenGL.cpp/.h` (`PrepareFilter2D`, `RunFilter2D`, `FreeFilter2D*`), `GPU_OpenGL_shaders.h` (key and MMPX passes, `FILTER2D` compositor variant), `GPU.h` (`Filter2D`, `Filter2DLogging`).
  - App: the "Smooth 2D edges" setting, plumbed like "Upscaled display capture", and the `filter2d` switch in `DebugProperties`.
- **Not part of this work, uncommitted in the app:** `docs/shelved/compute-on-mali.md` and its `docs/README.md` line, `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties`. Leave them out of these commits unless the user says otherwise.

How filtered 2D works, in one line: a pre-pass runs MMPX over a key image of the 2D planes and writes a map from each sub-pixel to a native pixel. The compositor reads planes 0–2 through that map and runs `CompositePixel` unchanged, so blending and 3D stay exact, and 3D is still fetched at the real position.

- **Blit after the readback:** done, reviewed, tested on both phones, and pushed (core `7516a617` OpenGL, `e5c7c29a` compute; plan doc, "Blit after the readback"). It's `debug.melonade.blitafterread`, on in debug builds only. In capture frames, the compositor and filter go to the GPU after the next frame's readback instead of before it. Results:
  - OnePlus, OpenGL: 37.9 → 59.9 fps with the filter on.
  - Fold, OpenGL: 56.7 → 60 fps on the inner screen.
  - OnePlus, compute: 24.8 → 28.3. Compute is GPU-bound there, so the gain is smaller.

  Pictures matched the old path on exact frames. Core files: `GPU.cpp/.h`, `GPU3D.h`, `GPU3D_OpenGL.cpp/.h`, `GPU3D_Compute.cpp/.h`, `GPU2D_Soft.cpp`, `GPU_OpenGL.cpp/.h`, `GPU_CaptureReads.h`. App: `DebugProperties.cpp/.h`.

- **Filter skip:** merged (11-08, core `0de067ee`, cherry-picked from `72104fd7`). When a CPU check finds the 2D planes unchanged, the key and MMPX passes are skipped and the map is reused. On top of the Blit after the readback it no longer stutters on the OnePlus, and it cut the Fold's GPU work by 16% on White 2's title (plan doc, "Filter skip on top of the Blit after the readback"). Core branches `gpu-2d-filter-skip` and `gpu-2d-filter-skip-2` can go.

## Next step

- Decide whether the Blit after the readback goes on in release builds. Before that: a plugged-in OnePlus run and a few other capture games.
- Optional: a long Fold run plugged in, 20–30 min of gameplay with the filter on. The 70 s runs never got hot enough to throttle.
- The quarter-size second pass (512x768 writing four results, picked by sub-pixel parity) if the GPU gets tight.
- **Step 3, the shadow renderer:** branch `gpu-2d-shadow` (from `widemelon`). Phase 0 and increment 1 are done (11-09, core `e4b893bb`, app `af44aeb7`); the open decisions went with the defaults, recorded in `docs/plan-gpu-2d-shadow.md` with the results. Blending (increment 5) was moved ahead of 1b and passes the device-free checks, uncommitted in the core. `check` passed on both phones (0 mismatches). The cost A/B found the OnePlus display stalling in mode `all` at 4x (NSMB, White 2); the Fold holds 60. Next is finding that stall.

- **OnePlus stall in shadow mode `all` (open, 11-09).** On the OnePlus 7 Pro (Snapdragon 855, Adreno 640, 1440x3120), `debug.melonade.gpu2d all` at 4x keeps emulation near 60 fps on OpenGL, but frames take far longer to reach the screen. Render ms per presented frame, `0` → `all`:
  - NSMB world map, GL: 1.7 → 321 ms (2.4 presented fps). The stall starts as the map loads.
  - White 2 overworld, GL: 2.1 → 29 ms (34 presented fps).
  - On compute both fall to 30–34 emulated fps, with emulator work going from 12–15 to 26–30 ms.
  - No cost on Kirby's boot (51% drawn) or Mario Kart's demo (0% drawn).
  - Mode `1` costs nothing: it draws nothing yet at 4x non-wide.
  - **Fold (SM8850) for comparison:** 60 fps in every mode, render 0.6–1.2 ms, about 1.6 Mclk a frame more GPU.
  - **Not known yet:** whether it scales with screen size (the composite runs at output resolution), with 3D on screen (NSMB and White 2 have 3D, Kirby doesn't), with sprite instances (NSMB 74 a frame, White 2 0), or comes from a sync point.
  - The increment 5 `check` runs missed it because their fps is emulation speed. Phone cost runs need to look at presented fps and render ms too.
  - Both phones were on battery.
  - Data: `perf_logs/2026-11-09-shadow2d-inc5/oneplus-cost/` (`cost_summary.txt`, `runs/`, `scripts/costsum.py`) and `fold/cost_table.md`.
  - **Not the shadow:** NSMB's map at compute 4x on the OnePlus already presents only 2.2 fps with the shadow off (render about 600 ms, emulation 53).
  - Find this before more increments, then windows (increment 4).

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

- **Fold scratch files from the 11-08 skip A/B, need the user's OK to delete:** in `/data/local/tmp`, `fs_apk_base.apk` and `fs_apk_skip.apk` (about 44 MB each), the `fs_base`, `fs_skip`, `fs_id_base` and `fs_id_skip` folders, `fs_fchain.log`, the three `fs_*` scripts, `fs_prefs_fold.xml` and `fs_user_prefs.xml`. All pulled to `perf_logs/2026-11-08-filterskip/fold/`.
- **OnePlus scratch files, need the user's OK to delete:** the 60 `f2_*` files from the first run plus `f2_crw_*`, `f2_crn_*`, `f2_crchain*.log`, `f2_b*`, `bl_*` and `f2_work/bl_gpuprod.log` in `/data/local/tmp`, and the matching `.pftrace` files (plus `bl_probe.pftrace`) in `/data/misc/perfetto-traces`. Everything is already pulled. Older ones from 11-06 are listed in `HANDOFF.md`.
- **OnePlus setting:** a tester ran `svc power stayon true`, so `stay_on_while_plugged_in` is now 7. The old value wasn't recorded.
- **App logs from the test runs:** about 150 perf/debug logs in the Fold's `Documents/2DS/Logs` (11-07 09:2x–10:3x) and in the OnePlus's app `perf_logs` folder.
- **AVD:** stray TWEWY files (PNGs, `.nds`, `.ml1`, `.sav`) in `/sdcard/Games`.
- **A possible existing bug, not ours to fix here:** the compositor reads with mediump `fTexcoord`, which may at 8x non-wide show a black last column or a last row from the other screen. It's credible from the code, but nobody has seen it on a phone. The filter path already avoids it.
- **Small MMPX quirks** on fine detail, like a hollow diamond on Advance Wars' lion. That's how MMPX looks.
