# The engine B no-hold fix on the OnePlus 7 Pro (done)

The fix (core `4c8a9f46`, app `1481b16c`, 11-06) passed this test and is **on in release since 11-16** (core `ac19bc04`, `GPU::EngineBNoHold = true`; `debug.melonade.bnohold` still overrides it, see `docs/debug-switches.md`). Before that the user also played gameplay on the Fold and tested a save state save and load with held lines: both fine.

**What it does:** during deferred capture, engine B's part of a line draws straight away and only engine A's stays held until line 192, so engine B writes no longer force the capture readback early. On the Fold at 4x it took Golden Sun Dark Dawn's title from 40.8 to 60 fps on compute and 47.6 to 60 on OpenGL (PERF-NOTES "Do games read their captures?"). A bottom-screen HASH2D difference between on and off isn't automatically a bug (PERF-NOTES).

## Result (11-16, OnePlus 7 Pro, plugged in, no throttling)

Dark Dawn, cold boots, means over runs ordered 1 0 0 1:

| Config | fps off → on | 1% low | work mean (ms) |
|---|---|---|---|
| compute 2x | 26.2 → 47.5 (44.5–51.4) | 14.1 → 32.4 | 38.1 → 20.5 |
| compute 4x | 18.9 → 35.3 | 10.8 → 25.5 | 52.9 → 27.9 |
| OpenGL 2x | 59.75 → 59.78 | 52.7 → 54.2 | 15.2 → 15.0 |
| OpenGL 4x | 59.4 → 59.8 | 40.8 → 54.8 | 16.25 → 15.1 (frames over budget 430 → 32) |

The map moves from lines 1–191 to line 192 on 1275 of 1276 captures. On compute, the GPU goes from waiting on the readback to busy (4x: 48% → 86% utilization). Nothing looked wrong on the top screen; the bottom screen wasn't visible in this layout, and HASH2D wasn't run. No GL errors, and the `.sav` files were unchanged. Data: `perf_logs/2026-11-16-bnohold-oneplus/`.

White 2 title, compute 2x (order 1 0 0 1): 44.5 → 59.1 fps, 1% low 32.7 → 43.1, work mean 22.0 → 13.9 ms. It maps at line 192 either way; what changes is the map wait, 9.4 → 2.2 ms per capture. Data: `perf_logs/2026-11-16-bnohold-oneplus-w2/raw/`.

## Rerunning it

**Connecting:** the user turns on wireless debugging, keeps the phone unplugged and the screen on. If mDNS doesn't list it, scan `192.168.1.214` ports 30000–50000 and `adb connect` the open ones (only one is real; the others show `offline`). Then `adb tcpip 5555` for a stable port. Always pass `-s`, since the Fold may be attached too. Install with `adb install -r`; never uninstall or `pm clear`, and compare the backups and restore settings at the end.

**Runs:** cold boots, no game input, no states, a fresh start per run, interleaved, hi-res capture on, rewind off. `setprop debug.melonade.bnohold 0` or `1` before the game starts, and `debug.melonade.capreads 1` for the map timing (`CAPFLUSH`, `CAPTRACE`). Confirm the path from the `Debug switches` log line. Dark Dawn's capture scene is about frames 253–1529 of a cold boot. White 2's title starts about 70 s in. Scripts: `perf_logs/2026-11-06-bnohold/` (`bnrun.sh`, `bnsum.py`). Long maps at line 192 on the first capture frames are normal. Back up each `.sav`, prefs and `rom_data.json` first; loading a state overwrites the `.sav`.

Configs: Dark Dawn at compute 2x and 4x and OpenGL 2x and 4x, then the White 2 title at 2x, each with the switch `1 0 0 1`.

**Report:** fps, 1% low, work mean and p95, map point and `map_us`, frames over budget, GPU counters if cheap (the OnePlus recipe is in `perf_logs/2026-11-06-oneplus-2d/`), and a look at both screens per config.
