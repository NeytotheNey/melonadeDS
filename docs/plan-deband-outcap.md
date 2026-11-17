# Deband at screen size (A) and a 6x output cap (B)

Both built 11-15 and on by default. Deband is now the single "Smooth gradients" setting, which also turns on Compute's dithering (the separate switch is gone). Numbers in PERF-NOTES "GPU gap vs WatermelonDS, again".

## Why

On the Fold inner screen (White 2 title, OpenGL 8x, 426 view, 11-14) the frontend Deband pre-pass was 64% of our GPU cycles: 9.75 Mcyc a frame with it on, 3.53 with it off, against WatermelonDS Vulkan's 4.5. It ran `DebandPrePassShader` over the whole frame texture (3408x3088, 10.5 Mpx) every new frame, while the screens only show ~4.5 Mpx (the real drawn area is 2.37 Mpx: 1470x1103 + 998x749).

The inner screen needs 6x (each DS pixel covers ~5.7 panel pixels, rounded up), but the output cap (K4, [plan-k4-output-cap.md](plan-k4-output-cap.md)) only divided S3D by a whole number, so from 8x the output stayed at 8x.

Deband cost scales with pixels (~0.59 Mcyc per Mpx).

## Facts both lean on

- **Deband** (`ShaderProgramSource.kt`): a center tap plus up to 3 rings of 4 LINEAR taps (5–13 taps), radius 3 DS px × (0.5–1), doubling twice (up to 12 DS px). The radius is converted with `texSize.y / FRAME_TEXTURE_HEIGHT`, so it's in DS pixels and stays right at any output size as long as the taps read the source texture. Grain is hashed on `gl_FragCoord`.
- **Repeat frames are already skipped** (`isSameFrame` in `VideoFilterPipeline`); the panel runs at 60 Hz, so every present is a new frame.
- **One `DSRenderer` (and pipeline) per surface:** the inner screen, cover screen and an external display each deband on their own.
- **The desktop frontend** (`desktop/src/Presenter.cpp`) doesn't run the Kotlin shaders, so A can't be checked there. B can (`--set OutputCapNeed=N`).

## A: deband inside the screen shader (`debandfold`, default 1)

Deband runs once per screen pixel inside the final Sharp or Linear screen draw (`#ifdef DEBAND`, `DEBAND_FUNCTIONS`), instead of once per frame texel in a pre-pass, and the pre-pass's full-texture write goes too. `DSRenderer` folds it in when the filter allows it, no Lua tilt-shift runs, and the screens draw fewer pixels than the frame has texels; every other case keeps the pre-pass. Kotlin only (`DebandFold.kt`, `VideoFilter`, `VideoFilterPipeline`, `ShaderFactory`). The `DEBANDFOLD` logcat line says which one each surface uses.

Fold inner 8x: 9.79 → 6.34 Mcyc (deband off 3.52), looks the same. Pixel (Mali): Sharp 8.09 → 6.45 Mcyc, the Linear variant runs. Cover screen checked; **an external display isn't yet.**

**Gotchas**
- Not bit-exact: it's deband of Sharp's sample, not Sharp of debanded texels. They differ only where every ring is within 2.5/63 of the center, by ~1/255 plus grain. Grain is per screen pixel instead of per texel.
- Tilt-shift would change the order, so it keeps the pre-pass. xBR, HQx, Scale2x, FXAA and MMPX keep it too. NoFilter (NEAREST) would need a LINEAR sampler for the ring taps, so it's left out. Quilez, LCD and Scanlines: check later.
- At 120 Hz each present would deband again. Count that if the panel ever runs at 120.

## B: integer Sout at a non-integer ratio (8x → 6x)

Sout stays an integer but no longer has to divide S3D. With integer Sout, output pixel edges never cross a native DS pixel, so 2D stays exact and nearest; a fractional Sout would break the integer sizes `FrameQueue`, hi-res capture banks, `scaleAt1x` and comp1x rely on.

- `GLCompositor::CappedScale` takes the smallest s ≥ need where S3D % s == 0 (S3D/s ≤ 4) or S3D/2 < s < S3D: 8 → 5–7, 12 → 7–11, 16 → 9–15. The 3D footprint is then ≤3 texels per axis.
- The DOWN3D shaders weigh the 3D texels `[x·S3D/Sout, (x+1)·S3D/Sout)` by coverage (an area resample of the full-scale frame); an integer ratio gives equal weights and the K4 output. Mode 0 stays exact against that resample. The capture pass sets alphaA when at least half the weight has it. Comp1x's filtered halves are off when `Scale3D % Scale != 0`.
- **Only while deband is on**, since it saves ~10% with deband and nothing without (the weighted 3D downsample costs about what the smaller frontend passes save). The frontend sends a deband flag with the need (any surface debanding, follows in-game toggles, same 30-frame hold). `outcap=1` takes non-whole ratios only with it; bit 2 (`outcap=3`) forces them on, bit 4 (`outcap=5`) off. The `OUTCAP` line says `ratio 4/3` and ends with `ratios any (deband on)` and the like.

Fold inner 8x → 6x: 6.31 → 5.71 Mcyc with deband on, flat with deband off (3.50 vs 3.59); Pixel 8x → 5x: 6.36 → 5.58. Shaders build on Adreno and Mali, no ripple seen. The in-game toggle switches after the hold, no GL errors. A + B lands at 5.71 on the Fold against WatermelonDS's 4.5 with deband on; with deband off we're already below it.

**Gotchas**
- The 4/3 weights repeat every 3 output pixels (3:1, 2:2, 1:3), which can show as a faint ripple on thin moving 3D edges.
- Sout changes on every need change now, so a fold/unfold reallocates more often (the hold covers flicker).
- Smooth 2D's map stays at S3D's draw scale; 6 isn't a multiple of its 4, so the slower corner-compare path runs more often.

**Desktop check:** `--scale 8 --set OutputCapNeed=6` equals a CPU area resample of the uncapped `--scale 8` shot, bit for bit, each screen half separately (rows 194·8 vs 194·6); again with `--wide 426` and with hi-res capture. The K4 plan's checks still hold.
