# Handoff: GPU 2D (filtered, hi-res and wide 2D)

The work is merged into `material3` / core `melonade` and the `gpu-2d*` branches are gone. The plan, results and what's next are in [docs/plan-gpu-2d.md](docs/plan-gpu-2d.md) (steps 1, 2 and 4) and [docs/plan-gpu-2d-shadow.md](docs/plan-gpu-2d-shadow.md) (step 3; next is the OnePlus stall in mode `all`, then windows). Build, install and phone-connection commands are in `HANDOFF.md`. This file only keeps the test data paths and the cleanup that needs the user's OK; it can be deleted once the leftovers are dealt with.

## Testing notes

- **Recipes and data:**
  - OnePlus cost runs: `perf_logs/2026-11-07-filter2d/` (`f2run.sh`, `f2sum.py`, `cost_table.md`).
  - Fold cost runs: `perf_logs/2026-11-07-filter2d/fold/` (`f2frun.sh`, `f2fsum.py`, `cost_table_fold.md`). The Fold has no perfetto GPU counters; its GPU column is kgsl busy × clock.
  - Capture timing: `perf_logs/2026-11-07-filter2d/capreads/` (`capsum.py`).
  - Counter logs: `logs/emulator/gpu2dstats/`. Screenshots and crops: `logs/phone/filter2d/` (Fold in `fold/`).
  - Save backups from the filter runs: `perf_logs/2026-11-07-filter2d/backup-fold/` and `backup-oneplus/`.
- **The AVD** has no GL renderer, so it can only run the counters. The desktop build (`desktop/`) runs OpenGL and Compute on the PC as a first logic check; Fold runs still decide whether a GL change is fixed.

## Leftovers

- **Fold scratch files from the 11-08 skip A/B, need the user's OK to delete:** in `/data/local/tmp`, `fs_apk_base.apk` and `fs_apk_skip.apk` (about 44 MB each), the `fs_base`, `fs_skip`, `fs_id_base` and `fs_id_skip` folders, `fs_fchain.log`, the three `fs_*` scripts, `fs_prefs_fold.xml` and `fs_user_prefs.xml`. All pulled to `perf_logs/2026-11-08-filterskip/fold/`.
- **OnePlus scratch files, need the user's OK to delete:** the 60 `f2_*` files from the first run plus `f2_crw_*`, `f2_crn_*`, `f2_crchain*.log`, `f2_b*`, `bl_*` and `f2_work/bl_gpuprod.log` in `/data/local/tmp`, and the matching `.pftrace` files (plus `bl_probe.pftrace`) in `/data/misc/perfetto-traces`. Everything is already pulled. Older ones from 11-06 were listed in the old `HANDOFF.md`, archived in `logs/docs-archive-2026-11-11/HANDOFF.md`.
- **OnePlus setting:** a tester ran `svc power stayon true`, so `stay_on_while_plugged_in` is now 7. The old value wasn't recorded.
- **App logs from the test runs:** about 150 perf/debug logs in the Fold's `Documents/2DS/Logs` (11-07 09:2x–10:3x) and in the OnePlus's app `perf_logs` folder.
- **AVD:** stray TWEWY files (PNGs, `.nds`, `.ml1`, `.sav`) in `/sdcard/Games`.
- **A possible existing bug, not ours to fix here:** the compositor reads with mediump `fTexcoord`, which may at 8x non-wide show a black last column or a last row from the other screen. It's credible from the code, but nobody has seen it on a phone. The filter path already avoids it.
