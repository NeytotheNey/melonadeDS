# Plan: Deband at screen size (A) and a 6x output cap (B)

## Why

On the Fold inner screen (White 2 title, OpenGL 8x, 426 view, 11-14) the frontend Deband pre-pass is 64% of our GPU cycles: 9.75 Mcyc a frame with it on, 3.53 with it off, against WatermelonDS Vulkan's 4.5. It runs `DebandPrePassShader` over the whole frame texture (3408x3088, 10.5 Mpx) every new frame, while the screens only show ~4.5 Mpx. Forcing the cap to 4x (`outcapneed=4`) gave 4.11. Numbers in PERF-NOTES "GPU gap vs WatermelonDS, again".

The inner screen needs 6x (each DS pixel covers ~5.7 panel pixels, rounded up). The output cap (K4) only divides S3D by a whole number (`CappedScale`, `GPU_OpenGL.cpp:663`: Sout = S3D/d, d 2–4 dividing S3D), so from 8x there's no way to get 6 and the output stays at 8x.

Deband cost scales with pixels (~0.59 Mcyc per Mpx). The estimates below come from that.

| | Deband pixels | Est. Fold 8x Mcyc |
|---|---|---|
| today | 10.5 Mpx (frame texture) | 9.75 |
| A only | ≤4.5 Mpx (screen) | ~5–6 |
| B only | 5.9 Mpx (6x texture) | ~6.5 |
| A + B | ≤4.5 Mpx, base ~3.0 | ~5.5 |
| deband off | | 3.53 |

They stack but each adds only ~0.5–1 on top of the other. **Build A first:** Kotlin only, works at every scale and on every surface, and saves the most. B next; it also helps with deband off.

## Facts both lean on

- **Deband** (`ShaderProgramSource.kt:796-860`): a center tap plus up to 3 rings of 4 LINEAR taps (5–13 taps), radius 3 DS px × (0.5–1), doubling twice (up to 12 DS px). The radius is converted with `texSize.y / FRAME_TEXTURE_HEIGHT`, so it's in DS pixels and stays right at any output size as long as the taps read the source texture. Grain is hashed on `gl_FragCoord`.
- **Other pre-passes:** FXAA (scale 1), MMPX (scale 2, native only), the Lua tilt-shift passes, comp1x's expand pass. Targets come from `VideoFilterPipeline.renderPass` (`:346`) at input size × an integer scale, RGBA8 LINEAR (`PrePass.prepareTarget`, `:476`).
- **Repeat frames are already skipped** (`isSameFrame`, `:272`); the panel runs at 60 Hz, so every present is a new frame.
- **One `DSRenderer` (and pipeline) per surface** (`DSRenderer.kt:445`): the inner screen, cover screen and an external display each deband the whole texture on their own today.
- **The frontend need is already an integer** (`DSRenderer.kt:291`, ceil).
- **The desktop frontend** (`desktop/src/Presenter.cpp`) is C++ and doesn't run the Kotlin shaders, so A can't be checked there. B can (`--set OutputCapNeed=N`).

## A: deband inside the screen shader

Evaluate deband once per screen pixel inside the final screen draw, instead of once per frame texel in a pre-pass. Also drops the pre-pass's full-texture write (~42 MB RGBA8 a frame at 8x).

| Where | Change | Lines |
|---|---|---|
| `ShaderProgramSource.kt` | Deband body into `debandFunctions(textureFunction)` (same pattern as `screenBandFunctions`, `:97`); the pre-pass calls it with `uv`. `#ifdef DEBAND c = deband(p, c, band);` around the final sample in Sharp (`:1008`) and Linear, the single-sample LINEAR screen shaders | ~40 |
| `ShaderFactory.kt:41` | More than one define (`DEBAND` next to `COLOR_CORRECTION`) | ~10 |
| `VideoFilter`, `VideoFilterShaderProvider.kt:34-40` | Deband as its own flag instead of a prepended pre-pass; mark which filters can take it in the screen shader (no own pre-passes, single-sample screen shader) | ~15 |
| `VideoFilterPipeline` | Build the `DEBAND` screen shader lazily; skip the pre-pass when folding it in; keep the cache logic | ~40 |
| `DSRenderer.kt:286-300, 495-531` | Sum the `drawRect` areas; fold deband in when the filter allows it, no tilt-shift is active, and drawn pixels < frame texels; otherwise the pre-pass as today (upscaled frames stay on it). New debug switch `debandfold` (1 default once proven, 0 = today) | ~25 |
| **Total** | Kotlin only, no core change | **~130** |

Not chosen: a screen-size pre-pass target in the frame's layout only stays exact (gap rows, `screenBand`) at an integer scale, which is just B; one target per screen gets to the screen's pixel count but needs a texture and UV remap per quad and turns deband into a resampler ahead of Sharp. Skipping repeat frames is already done.

**Risks**
- Not bit-exact: it's deband of Sharp's sample, not Sharp of debanded texels. They differ only where every ring is within 2.5/63 of the center, by ~1/255 plus grain. Grain becomes per screen pixel instead of per texel (negligible at ~1.33 texels per pixel).
- Tilt-shift would change order, so it keeps the pre-pass. xBR, HQx, Scale2x, FXAA and MMPX keep it too. NoFilter (NEAREST) would need a LINEAR sampler for the ring taps; leave it out at first. Quilez, LCD and Scanlines: check later.
- At 120 Hz each present would deband again. Count that if the panel ever runs at 120.

**Verification**
1. Device-free: a JVM test of the decision (like `ScreensDrawingTest`), a check that the shader sources assemble with the define, glslang if installed (it isn't now).
2. Fold: `debandfold` 0 vs 1 screenshots of the White 2 title's sky gradients; expect ≤ ~2/255 outside the grain.
3. Fold: interleaved Mcyc, pre-pass vs folded vs deband off, quick recipe.
4. Cover screen and an external display look right.

**Open:** the real drawn area on the inner screen (add `drawRect` next to `nativeRect` in the `OUTCAP:` line).

## B: integer Sout at a non-integer ratio (8x → 6x)

Keep Sout an integer but stop requiring it to divide S3D. With integer Sout, output pixel edges never cross a native DS pixel, so 2D stays exact and nearest; a fractional Sout (5.5) would make 2D pixels uneven and break the integer sizes `FrameQueue`, hi-res capture banks, `scaleAt1x` and comp1x rely on.

| Where | Change | Lines |
|---|---|---|
| `CappedScale` (`GPU_OpenGL.cpp:663`) | Smallest s ≥ need where S3D % s == 0 (S3D/s ≤ 4) or S3D/2 < s < S3D: 8 → 5–7, 12 → 7–11, 16 → 9–15. The 3D footprint is then ≤3 texels per axis (2 for (q+1)/q ratios like 4/3) | ~10 |
| `GPU_OpenGL_shaders.h:536-982` | `int u3DRatio` → an S3D uniform. Output x covers 3D texels `[x·S3D/Sout, (x+1)·S3D/Sout)` with integer weights in 1/Sout, so an integer ratio gives equal weights and today's output. Weighted loops in `FirstSubPixel`, `SubPixel3D`, `MiddleDown3D`, `WingDown3D`, `FilterDown3D` and mode 2's `Average3D`, ending in `(Σw·c + S3D²/2) / S3D²`. `Block3DAlphas`: one gather for a footprint of 2, four for 3. `Bilinear3DBlock`: one tap at the weighted center for 2 (4/3 gives 0.25/0.5 offsets, exact in bilinear), four for 3. FILTER2D's corner check uses the last covered sub-pixel | ~150 |
| `HiresCaptureDown3DFS` (`:1039-1110`) | Same weights; alphaA when at least half the weight has it | ~40 |
| Host | Uniforms at `GPU_OpenGL.cpp:1366`, `:2377` (`Scale3D / Scale`); the log's `ratio %d` → p/q (`:659`); comp1x's filtered halves off when `Scale3D % Scale != 0` (their argument at `:1301-1305` assumes an integer ratio) | ~20 |
| **Total** | core and shaders | **~220** |

Unaffected: the 30-frame hold, the frontend need, `FrameQueue`, the banks (256·Sout), `FitScale` (fits S3D), and Smooth 2D's map (stays at S3D's draw scale; 6 isn't a multiple of its 4, so the slower corner-compare path runs more often).

**Risks**
- Mode 0 stays exact, but against an area resample rather than a box filter.
- The 4/3 weights repeat every 3 output pixels (3:1, 2:2, 1:3), which can show as a faint ripple on thin moving 3D edges.
- Moiré should be no worse than today: uncapped, Sharp already bilinear-downscales 8x onto the ~6x screen.
- Sout changes on every need change now, so a fold/unfold reallocates more often (the hold covers flicker).

**Verification**
1. Desktop: the K4 plan's checks 1, 3 and 4 as they are; check 2 still holds at 16 → 8.
2. Desktop, new: `--scale 8 --set OutputCapNeed=6` equals a CPU area resample of the uncapped `--scale 8` shot, bit for bit, each screen half separately (rows 194·8 vs 194·6). Again with `--wide 426` and with hi-res capture.
3. Fold: `OUTCAP: capped, output 6x, 3D 8x, ratio 4/3`; interleaved Mcyc vs uncapped; look at 3D edges for ripple.
4. Pixel and OnePlus: one quick pass each (their needs differ; the OnePlus is the CPU limit).

## Order

1. A, behind `debandfold`; Fold A/B and screenshots; default on if it holds up.
2. B, behind the existing `outcap` switch (a new bit for non-integer ratios, off by default until the phones pass).
3. Re-measure A + B against WatermelonDS on the Fold at 8x and 16x, deband on and off.
