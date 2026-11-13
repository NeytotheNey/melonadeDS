# K4: output capped at the screen's scale (design, 11-13)

Two scales in the compositor: S3D = 3D target scale, Sout = output scale, r = S3D/Sout integer 1..4. The compositor's existing `Scale` becomes Sout (output size, padding rows, held wings, hi-res banks, filter map scale, comp1x, OutputLayout already key on it). Only code reading `_3DTex` needs S3D. r == 1 runs the existing programs unchanged.

## Output scale
- Frontend: `DSRenderer.updateScreenCoordinates()` (app/src/main/java/me/magnum/melonds/ui/emulator/DSRenderer.kt:262-312) gets drawRect + nativeRect per screen from `getScreensDrawing()` (domain/model/layout/ScreensDrawing.kt:36-58). nativeRect is the 256-column part. Recomputed on surface change (:435), layout change (:130), frame width change (:456). One DSRenderer per surface (EmulatorActivity.kt:438, render/ExternalPresentation.kt:52). FrameRenderCoordinator.kt:155-159 already combines a per-surface value (comp1x).
- Rule: per screen need = ceil(max(nativeRect.w/256, nativeRect.h/192) - 1e-3); per surface max over non-null screens; max over surfaces. Sout = S3D/d with largest d in 1..4 dividing S3D and S3D/d >= need. need 0 = unknown = no cap.
- 12x: integer only -> 12, 6, 4, 3. No 3:2 path.
- Plumbing: `fun outputScaleNeed(): Int = 0` on EmulatorRenderer (render/EmulatorRenderer.kt), volatile field set in DSRenderer.updateScreenCoordinates, passed through EmulatorSurfaceView (next to canDrawHalvesAt1x, :108), max in FrameRenderCoordinator (:155-159), new `MelonEmulator.setOutputScaleNeed(n)` only on change. Separate JNI function (don't change the callback's Z return, getRenderFrameMethodId MelonDSAndroidJNI.cpp:111/365). Native atomic int in MelonDS.cpp next to halvesAt1xSupported (:356-366), declared MelonDS.h:136-140.
- MelonInstance::runFrame before screenWidth (:359-377): read atomic, debounce (accept a new value once it's been stable ~30 frames), call glRenderer.SetOutputNeed(gpu.OutputCap ? need : 0) (OutputCapNeed overrides), size the frame from new GetOutputScale() instead of GetScaleFactor() at :362 and :372. Before validateRenderFrame (:401) and RunFrame.
- Core: GLRenderer and ComputeRenderer store need; ResizeTargets (GPU3D_OpenGL.cpp:547-560; Compute GPU3D_Compute.cpp:850) passes (CappedScale(scale, need), scale, targetWidth) to GLCompositor::SetRenderSettings. SetOutputNeed only calls the compositor: no TargetsChanged, no 3D realloc. Early return in GLCompositor::SetRenderSettings (GPU_OpenGL.cpp:429) must compare scale3D too; existing scale-change handling (:433-460) is what we want.

## 3D downscale: supersampled composite (exact)
- Compositor reads only 3D color+alpha (`_3DTex`, unit 1, GLRenderer::SetupAccelFrame :2433-2437). Readers: main/wide Fetch3D (GPU_OpenGL_shaders.h:197-210, called :420, :242, wing-edge probe :256), FilterFetch3D (:318-334), Shadow2D composite (:1473), capture Fetch3DCapture (:499-506).
- CompositePixel (:30-97) is nonlinear in 3D alpha (a>0 picks 3D vs 2D; compmode 4 blends eva=a+1; 0/2/3 unblended). So averaging 3D first (blit/bilinear) is wrong at edges.
- Per output pixel: the 2D work is the same for all r^2 sub-pixels. Each sub-pixel k = base*r + (i,j): Fetch3D at S3D (same truncating ivec2(pos*S3D) form as uncapped so negative scrolls match), CompositePixel, brightness, 8-bit expansion; average in integers (sum + n/2)/n; write k/255. Bit-exact box filter of the uncapped S3D output. Shortcut: textureGather alphas, all 0 -> one composite with _3dpix = 0.
- `#define DOWN3D` variant of each compositor program, built at the first capped SetRenderSettings, never per frame, always highp. Uniform u3DRatio; u3DScale stays the output scale. Restructure main() (:339-454) so 3D fetch..oColor is a per-sub-sample function looped under DOWN3D.

## Follows Sout automatically
PrepareOutput (GPU_OpenGL.cpp:467-502), ScreenW/H (:443-444), ClearOutput (:721), viewport (:1054), vertices (:144-180). FrameQueue validateRenderFrame (app/src/main/cpp/renderer/FrameQueue.cpp:177-215). Frontend filters (texSize, scaleAt1x = OutputLayout::Scale). comp1x (:1015, :1174, :789-796, CheckComp1x). QueueHash3D (GPU3D_OpenGL.cpp:2463-2493) and ReadBackCaptureFrame (:2306-2333) read MainFramebuffer at S3D, so hash3d must be identical with outcap on/off. Compute uses the same GLCompositor (GPU3D_Compute.cpp:850, :3199-3202), needs SetOutputNeed/GetOutputScale too.

## Excluded (cap forced off = r 1 while active)
First pass: wide view (ViewWidth != 256), Filter2D, Shadow2D (gpu2d), HiresCapture. Wide view and HiresCapture are ported since 11-13 (see Status); Filter2D and Shadow2D (gpu2dwide's too) are still excluded.

## Switches
Read in app/src/main/cpp/debug/DebugProperties.cpp:229-245 into GPU int fields (GPU.h:844-877, documented), summary :313-315, docs DebugProperties.h:139-160, desktop kIntSwitches desktop/src/main.cpp:184-198.
- `outcap` / GPU::OutputCap, default 1: 1 caps from the frontend's need, 0 off (the old output exactly). A need of 0 (unknown, the desktop build's) is no cap.
- `outcapneed=N` / GPU::OutputCapNeed: overrides the need (A/Bs, desktop).
- `outcap3d` / GPU::OutputCap3D, default 3: 0 exact supersample, 1 bilinear average + one composite, 2 alpha-weighted, 3 edge-aware (textureGather the block's alphas: all the same → mode 1's path, mixed → mode 0's exact composite; mixed-alpha blocks are exact, no silhouette fringe). The desktop build takes GPU's defaults.
- Saturating blends: compmode 1 (3D on bottom, blending) clamps at 0x3F, and EVA + EVB can reach 32. Above 16 together, clamping the composite of the averaged 3D isn't the average of the clamped composites (EVA = EVB = 16, 2D 32, texels 63/63/0/0: exact 192, one composite 255). Mode 3 sends those pixels (`(val3.a & 0xF) == 1 && val3.g + val3.b > 16`, `BlendSaturates`) to the exact composite in the middle, the wings and hi-res capture's source A, so its same-alpha path is only off by rounding. Modes 1 and 2 keep that error. No other compmode or master brightness can clamp: compmode 4's factors sum to 32 with a 5-bit shift, 0/2/3 and brightness stay in 0..0x3F with factors that don't depend on the 3D's color, and compmode 4's EVA is the alpha, which mode 3's same-alpha path has the same in every sub-pixel.

## Exactness checks (desktop)
1. r=1: --screenshot byte-identical with OutputCap=0 vs OutputCap=1 OutputCapNeed=16 at --scale 16; HASH3D identical.
2. --scale 16 --set OutputCap=1 --set OutputCapNeed=8 must equal a 2:1 box filter ((sum+2)/4 per channel) of the uncapped --scale 16 screenshot, bit for bit. Box-filter each half separately (padding: half 1 starts at row 194*16 in source, 194*8 in result). --fixed-time, same frame count, hi-res capture and gpu2d off, filter off, 256 view.
3. vs a straight --scale 8: differences only in native pixels showing 3D (sanity count).
4. HASH3D same in all runs.

## Risks
Variant build hitches (build at SetRenderSettings), fp16 (DOWN3D highp; reference at 8x and below is mediump), fold/unfold reallocs (debounce), thread handoff (atomic), external displays keep max.

## Status (11-13)
Steps 1–3 are in (plumbing, exact DOWN3D for the 256 view, frontend need), plus `outcap3d` 1/2 and the `OUTCAP:` logs. Results in PERF-NOTES "K4". File:line refs above are from before the change.
Then (11-13, second pass): WideMelon and hi-res capture are ported. Third pass: mode 3 takes the exact composite where a blend can saturate, `outcap` defaults to 1 and `outcap3d` to 3.
- **WideMelon:** a wide DOWN3D variant of each mode (`Down3D[1][mode]`, WIDE + DOWN3D), built at a capped SetRenderSettings in a wide view. The 3D target is as wide as the output, so the 3D column of an output sub-pixel is its column at S3D plus the scroll, middle and wings alike. Wings composite per sub-sample in mode 0 (edge probe at the probe's S3D column and the sub-sample's row); modes 1 and 2 composite once on the averaged 3D and the probe asks whether any of the block's rows has 3D. Held wings are copies of the capped output (Sout), read as before. Comp1x's small halves use the wide DOWN3D program's u3DScale.
- **Hi-res capture:** banks and scratch stay at `256*Scale` (= Sout). The capture pass has DOWN3D variants per mode (`HiresCaptureDown3DFS`, `u3DRatio`), built with the compositor's while hi-res capture is on (`SetOutputCap3D` now takes `gpu.HiresCapture`). Mode 0: composite each sub-sample, average colA (6 bits, rounded), alphaA set when at least half the sub-samples have it. Modes 1/2: one composite on the averaged 3D (mode 2 keeps the plain color where no sub-sample has alpha, as a 3D-only capture keeps transparent pixels' color). Word maths once. The display side reads banks at `gl_FragCoord` (Sout) in the plain, wide and DOWN3D entries; comp1x never draws a hi-res line at 1x. `OUTCAP:` lines end with `hirescap N`.

## Remaining steps
- **Hi-res capture (done, see Status).** Not bit-exact vs the uncapped capture (5-bit words). Banks drop on a scale change, so a few frames show the native capture. Alternative if that's ever a problem: keep the capture at S3D (exact, 32 MB banks at 16x).
- **Smooth 2D (Filter2D).** `FilterFetch3D` builds its sub-sample positions from `subPixel*r + (i,j)`. The map texel is the same for all r² sub-pixels when Sout is a multiple of the map scale. Identity mode (`Filter2D 3`, mediump) stays uncapped. On the cover screen at Sout=4 with a 4x map, comp1x's filtered halves fall back to a full draw.
- **Shadow2D (gpu2d, debug-only):** port its composite tail or leave it excluded.
- **Check mode `outcap=2`** (like `comp1x=3`): every 30 frames, draw capped into a check target and compare with a CPU box filter of the full draw. Exact only above 8x on fp16 GPUs (the uncapped reference at 8x and below is mediump).
- **Follow-ups:** `ScaleFits` only needs `192*S3D` and `(384+2)*Sout` to fit once capped, so 4096-limit GPUs could run 16x 3D. Clear new frame textures on the GPU instead of the zeroed CPU vector (`FrameQueue.cpp:200`), which now runs on each fold/unfold.
