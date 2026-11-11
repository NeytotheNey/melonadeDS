# Test: the engine B no-hold fix on the OnePlus 7 Pro

Moved out of `HANDOFF.md` on 11-11. The fix is merged (core `4c8a9f46`, app `1481b16c`, 11-06) and on in debug builds (`debug.melonade.bnohold`, see `docs/debug-switches.md`). Release builds keep it off until this test and the steps at the end are done.

**Why:** during deferred capture, engine B's part of a line now draws straight away and only engine A's stays held until line 192, so engine B writes no longer force the capture readback early. It took Golden Sun Dark Dawn's title on the Fold at 4x from 40.8 to 60 fps on compute and 47.6 to 60 on OpenGL, with the wait down 96% (PERF-NOTES "Do games read their captures?"). White 2 and Phantom Hourglass didn't move, because they already map at line 192. The question here is how much of that a slower GPU keeps, since the readback may wait at line 192 instead.

**Baseline:** PERF-NOTES "OnePlus 7 Pro: what limits the capture scene" (White 2 title at 4x, before the fix): compute 24–28 fps, GPU maxed out, about 18 M clocks per frame; OpenGL 52 fps, CPU-limited. Don't expect it to fix White 2's 30 fps at compute 2x; measure it anyway.

**Build:** any current debug build (the fix is on by default). Install with `adb install -r`; never uninstall or `pm clear`.

**Connecting:** the user turns on wireless debugging, keeps the phone unplugged and the screen on. If mDNS doesn't list it, scan `192.168.1.214` ports 30000–50000 and `adb connect` the open ones (only one is real; the others show `offline`). Then `adb tcpip 5555` for a stable port. Always pass `-s`, since the Fold may be attached too.

**Runs:** cold boots, no game input, no states, unplugged, a fresh start per run, interleaved, hi-res capture on, rewind off. `setprop debug.melonade.bnohold 0` or `1` before the game starts, and `debug.melonade.capreads 1` for the map timing (`CAPFLUSH`, `CAPTRACE`). Confirm the path from the `Debug switches` log line. Dark Dawn's capture scene is about frames 253–1529 of a cold boot. White 2's title starts about 70 s in. Check Dark Dawn's ROM is on the phone first, and ask the user if it isn't.
1. Dark Dawn: compute 2x, OpenGL 2x, compute 4x, OpenGL 4x, with the switch `1 0 0 1` each.
2. White 2 title: compute 2x and OpenGL 2x, `1 0` each.

**Report:** fps, 1% low, work mean and p95, map point and `map_us`, frames over budget, GPU counters if cheap (the OnePlus recipe is in `perf_logs/2026-11-06-oneplus-2d/`), and a look at both screens per config. Adapt `perf_logs/2026-11-06-bnohold/` (`bnrun.sh`, `bnsum.py`). Long maps at line 192 on the first capture frames are normal.

**Protect the user's data:** back up each `.sav`, prefs and `rom_data.json` first, compare after each run, and restore settings at the end. Loading a state overwrites the `.sav`.

**Before turning it on for release** (the core default `GPU::EngineBNoHold` is false, and only `DebugProperties::apply` sets it): the user plays some gameplay on the Fold, and a save state save and load gets tested with the `.sav` backed up first, since a state taken with held lines has never run. A bottom-screen HASH2D difference between on and off isn't automatically a bug (PERF-NOTES).
