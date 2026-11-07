# Handoff: GPU 2D (filtered, hi-res and wide 2D)

The plan, the design decisions and all results so far are in [docs/plan-gpu-2d.md](docs/plan-gpu-2d.md). Read that first. This file is the current state and the next steps. Build, install and phone-connection commands are in `HANDOFF.md`.

## State (11-07)

Branch `gpu-2d` in the app (from `material3`) and in the core (from `melonade`). **Nothing is committed or pushed.** The core submodule pointer in the app is dirty for the same reason.

- **Step 1, counters:** done. `debug.melonade.gpu2dstats`, in the core's new `src/GPU_2DStats.h` (598 lines) plus hooks in `GPU.cpp`/`GPU.h`, with `LayerType` made public in `GPU_CaptureReads.h`. Reviewed, run on the AVD over 12 games, HASH2D identical with it on.
- **Step 2, filtered 2D:** built, reviewed, and tested on the OnePlus 7 Pro. Not run on the Fold yet.
  - The core changes are in `GPU_OpenGL.cpp/.h` (`PrepareFilter2D`, `RunFilter2D`, `FreeFilter2D*`), `GPU_OpenGL_shaders.h` (key and MMPX passes, `FILTER2D` compositor variant) and `GPU.h` (`Filter2D`, `Filter2DLogging`).
  - The app changes are the "Smooth 2D edges" setting, plumbed like "Upscaled display capture", and the `filter2d` switch in `DebugProperties`.
  - `.claude/shadercheck/` knows the new shaders.
- **Not part of this work, uncommitted in the app:** `docs/shelved/compute-on-mali.md` and its `docs/README.md` line, `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties`. Leave them out of these commits unless the user says otherwise.

How filtered 2D works, in one line: a pre-pass runs MMPX over a key image of the 2D planes and writes a map from each sub-pixel to a native pixel. The compositor reads planes 0–2 through that map and runs `CompositePixel` unchanged, so blending and 3D stay exact, and 3D is still fetched at the real position.

## Next up

1. **Fold run of filtered 2D.** The Fold is available again. GL changes don't count as fixed until they've run on it, because a past bug only showed on its Adreno. Repeat the OnePlus checks:
   - `filter2d` 1 vs 0 at 4x on OpenGL and compute: edges smoothed on both screens and in the lower half of each, 3D sharp, no stray rows or columns.
   - `filter2d` 3 vs 0 at 2x, 4x and 8x, and with WideMelon on: must be identical.
   - The cost table, unplugged, with the same scenes as the OnePlus. The Fold held 60 in White 2's title before; does it still?
2. **Find why White 2's capture scene slows down.** On the OnePlus at OpenGL 4x it went from 53.6 to 36.8 fps with the filter on. The GPU stayed under half its top clock, so it isn't simply out of GPU. The guess is that the next frame's capture readback waits behind the heavier compositor. Measure with `debug.melonade.capreads 1` (map timing in `CAPFLUSH`/`CAPTRACE`) for `filter2d` 0 vs 1 on both phones before changing anything.
3. **Then make it cheaper.** Options, roughly in order:
   - Skip all passes when the 2D planes and control words didn't change since the last frame; most 2D scenes are still most of the time.
   - Use a cheaper second pass: it's most of the cost (`filter2d` 2 runs only the 2x pass).
   - Change the pass order around the capture readback, depending on what step 2 finds.
4. **Commit once the user agrees.** Suggested split: core counters, core filter, app setting, docs. Offer to push afterwards (core first, then the app).
5. **Step 3, the shadow renderer**, starts after filtered 2D is settled. Before designing it, run the counters over real gameplay. Last night's runs were mostly title screens and menus. Golden Sun Dark Dawn is the case to design for, with lots of rotated layers and lots of mid-frame changes.

## Testing notes

- **Both phones can be used.** Pass `-s` on every adb command, since both may be attached. The Fold is serial `R5GL64A7LKN`, 192.168.1.32, and its connect port comes from `adb mdns services`. The OnePlus is GM1917 at 192.168.1.214; use `adb tcpip 5555` once connected.
- **The OnePlus is a dev phone:** it stays on power, doesn't sleep and has no lock screen. Charging can change clocks and heat, so mark its numbers as plugged in, or unplug it for comparisons with the 11-06 baseline. Last night it was left with the screen off, which usually stops wireless debugging, so turn that back on at the phone.
- **Protect saves on both phones:** back up each `.sav`, prefs and `rom_data.json` first. Never load a save state, since that overwrites the real `.sav`. Never uninstall or `pm clear`. Compare and restore at the end.
- **Recipes and data:**
  - Cost runs: `perf_logs/2026-11-07-filter2d/` (`f2run.sh`, `f2sum.py`, `cost_table.md`).
  - Counter logs: `logs/emulator/gpu2dstats/`.
  - Screenshots and crops: `logs/phone/filter2d/`.
- **The AVD** has no GL renderer, so it can only run the counters.

## Leftovers

- **OnePlus scratch files:** 60 `f2_*` files in `/data/local/tmp` and 16 `f2_*.pftrace` in `/data/misc/perfetto-traces`. A safety hook held the delete, so this needs the user's OK. The older ones from 11-06 are listed in `HANDOFF.md`.
- **AVD:** stray TWEWY files (PNGs, `.nds`, `.ml1`, `.sav`) in `/sdcard/Games`.
- **A possible existing bug, not ours to fix here:** the compositor reads with mediump `fTexcoord`, which may at 8x non-wide show a black last column or a last row from the other screen. It's credible from the code, but nobody has seen it on a phone. The filter path already avoids it.
- **Small MMPX quirks** on fine detail, like a hollow diamond on Advance Wars' lion. That's how MMPX looks.
