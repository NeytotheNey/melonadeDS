# Precise depth (White 2 Castelia pier)

Branch `fix/precise-depth` in app and core (11-18), committed, not pushed. Setting: Settings → Video → Precise depth (`video_precise_depth`, `GPU::PreciseDepth`, desktop `--precise-depth`), off by default.

## The bug

On the Castelia City pier in Pokémon White 2, the shadows under the benches break up and come back as you walk, and the black ship's red window decals bleed through its hull in stripes. It happens on every renderer, in upstream melonDS and in WatermelonDS, with widescreen on or off.

## What it isn't

- **Polygon limits:** 0 drops; the pier peaks at about 1,055 of 2,048 polygons. The game never reads the count registers.
- **Box tests:** forcing every box test to pass gave pixel-identical frames over two 1,199-frame walks, and all 13,202 logged box tests matched an exact float check.
- **Game culling:** the field code (found with RAM dumps at the pier, since REA's Ghidra here takes x86 only) box-tests each map block, then each prop (32 slots of 0x14 bytes at block object +0x9C), and draws whole models. There's no CPU frustum, distance, LOD or budget check. The ship (`c03ship031`, 116 polygons) is one model under one box test.
- **Polygon ID rule:** the shadows are ID 40, the mist ID 8; no same-ID skips.

## What it is

Both are decals the game lifts just above the surface under them, by less than the DS's depth rounding:

- The bench shadows (material `h_kage`, attr `0x281F8084`, Z-buffer) sit 1.78 world units above the deck. From that camera distance that's only about half of one 0x200 depth step.
- Per-vertex Z is `((Z*0x4000)/W + 0x3FFF)*0x200` (GBATEK, upstream). Its truncation error is often bigger than the lift, so the floor wins on some frames. Walking the pier on soft: 6,262 shadow pixels lost to the floor with it off, 38 with it on.

Whether a real DS keeps more precision isn't known. Only a hardware capture of the night pier would settle it, which is why this is an opt-in enhancement.

## The fix

- **Core** `26478852`: Z-buffer vertex depth `(Z*0x800000)/W + 0x7FFE00` (s64, clamped), computed once in `SubmitPolygon`, latched at the polygon RAM swap, kept per polygon.
- **OpenGL** (same commit): `SetupVertex` kept 16 bits plus a shift; `PackDepth` puts the dropped low bits in spare attribute bits and `kRenderVS` adds them back.
- **Compute** `61834c32`: the DS-style Z interpolation (`InterpolateZZBuffer`, copied from soft's `Interpolator::InterpolateZ`) drops the low 9 bits along spans and keeps only 10 bits of the difference along edges. With the setting on, the Z-buffer InterpSpans and Rasterise programs use an exact `InterpolateZPrecise` (three `umulExtended`, no divide). The renderer recompiles on a toggle, like ComputeOpt.
- **App** `a5828dcc`, `680091f3`: the setting, the desktop flag, docs.

Off is bit-exact on soft, OpenGL and Compute (HASH3D on W2, Platinum, SoulSilver, Mario Kart, Contra 4, Golden Sun), and Compute's shader sources are byte-identical with it off.

## Results

- **User on the Fold, 11-18:** OpenGL is fixed completely. Compute before `61834c32` was about 95%, with the ship's windows still striped.
- **Desktop after `61834c32`:** the ship is clean on Compute at 4x and 8x and matches OpenGL. The raster pass costs about +2–4% at 8x on the RTX.
- **Phones:** the OnePlus 7 Pro (Adreno 640) and the Fold compile both renderers with it on and off, with the same FPS. Neither tester run caught the flicker on video, but the user sees it by eye.

## Open

- The Fold check of the Compute fix (`61834c32`): the APK is installed as `me.magnum.melonds.dev`. Look at the ship's windows and the FPS at the user's scale.
- The Pixel 8 (Mali) compile check: the phone wasn't reachable on 11-18.
- The software renderer still truncates Z interpolation like Compute did. The same exact interpolation could go into `Interpolator::InterpolateZ`, gated on the polygon flag.
- Depth-equal decals (attr bit 14) with it on are untested; none of the scenes drew one.
- Side finding: `BoxTestHits` (and upstream's box test) clips the far plane only when the last `POLYGON_ATTR` has bit 12 set, so a box crossing the far plane can miss depending on that. Not hit here; hardware behavior unconfirmed.

## Test data

- Desktop repro: `logs/w2-pier/` has the night pier state `fold.ml7` and `fold.sav`. Always pass `--sav` a copy. The desktop shortcuts "White 2 - Castelia pier" and "…, precise depth" load it.
- The user's Fold White 2 files as of 11-18 (slot 0, slot 7, the .sav) are backed up in `logs/w2-fold-backup-2026-11-18/`.
- Left on the phones by the tester (the user deletes them): on the Fold, `/storage/emulated/0/Documents/nds/pd-test/`, `/data/local/tmp/pd_launch.sh`, `/data/local/tmp/pd_setpd.sh`, `/sdcard/pd_A.png`, `/sdcard/pd_s.png` and `/sdcard/pd_rec.mp4`. On the OnePlus, `/sdcard/Documents/ROMs/pd-test/`, `/data/local/tmp/setpd.sh`, `/data/local/tmp/launch.sh` and `/sdcard/ui.xml`. Phone run data is in `perf_logs/2026-11-18-precise-depth-{oneplus,fold}/`.
- The investigation's logging switches (BoxTestForceHit, BoxTestLog, PolyLog, PixLogId, SOFTPIX) are on `debug/w2-pier-tools` in core (`c3ed2030`) and app (`c17c950c`), not for merge.
