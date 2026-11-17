# K4: output capped at the screen's scale

Built 11-13 and on by default (`outcap=1`, `outcap3d=3`). Results in PERF-NOTES "K4" through "K4 phase 3". Part B of [plan-deband-outcap.md](plan-deband-outcap.md) (11-15) later let the cap take output scales that don't divide the 3D scale while deband is on; this file describes the whole-ratio design underneath it.

Two scales in the compositor: S3D = 3D target scale, Sout = output scale, r = S3D/Sout. The compositor's `Scale` is Sout (output size, padding rows, held wings, hi-res banks, filter map scale, comp1x and OutputLayout key on it). Only code reading `_3DTex` needs S3D. r == 1 runs the plain programs unchanged.

## Output scale
- Frontend: `DSRenderer.updateScreenCoordinates()` gets drawRect + nativeRect per screen from `getScreensDrawing()` (`ScreensDrawing.kt`). nativeRect is the 256-column part. Recomputed on surface change, layout change and frame width change. One DSRenderer per surface (`EmulatorActivity`, `ExternalPresentation`); `FrameRenderCoordinator` takes the max over surfaces.
- Rule: per screen need = ceil(max(nativeRect.w/256, nativeRect.h/192) - 1e-3); per surface max over non-null screens; max over surfaces. With whole ratios, Sout = S3D/d with the largest d in 1..4 dividing S3D and S3D/d >= need (12x: 12, 6, 4, 3). need 0 = unknown = no cap. The non-whole ratios with deband on are in `GLCompositor::CappedScale` and plan-deband-outcap part B.
- Plumbing: `outputScaleNeed()` on EmulatorRenderer, set in `DSRenderer.updateScreenCoordinates`, passed through EmulatorSurfaceView, max in FrameRenderCoordinator, then `MelonEmulator.setOutputScaleNeed(n)` only on change (a separate JNI function, so the render callback's return is untouched) into a native atomic in `MelonDS.cpp`.
- `MelonInstance::runFrame` reads the atomic, accepts a new value once it's been stable for 30 frames, calls `SetOutputNeed(gpu.OutputCap ? need : 0)` (`OutputCapNeed` overrides) and sizes the frame from `GetOutputScale()`.
- Core: GLRenderer and ComputeRenderer store the need; `ResizeTargets` passes the capped scale, S3D and the target width to `GLCompositor::SetRenderSettings`. `SetOutputNeed` only calls the compositor: no TargetsChanged, no 3D realloc.

## 3D downscale: supersampled composite (exact)
- The compositor reads only 3D color+alpha (`_3DTex`). Readers: main/wide Fetch3D (and the wing-edge probe), FilterFetch3D, the Shadow2D composite, and capture's Fetch3DCapture.
- CompositePixel is nonlinear in 3D alpha (a>0 picks 3D vs 2D; compmode 4 blends eva=a+1; 0/2/3 unblended). So averaging 3D first (blit/bilinear) is wrong at edges.
- Per output pixel the 2D work is the same for all r² sub-pixels. Each sub-pixel: Fetch3D at S3D (same truncating `ivec2(pos*S3D)` form as uncapped so negative scrolls match), CompositePixel, brightness, 8-bit expansion; average in integers (sum + n/2)/n. That's a bit-exact box filter of the uncapped S3D output.
- A `#define DOWN3D` variant of each compositor program, built at the first capped SetRenderSettings, never per frame, always highp. Uniform `u3DRatio`; `u3DScale` stays the output scale.

## Follows Sout automatically
PrepareOutput, ScreenW/H, ClearOutput, the viewport and vertices, FrameQueue's `validateRenderFrame`, the frontend filters (texSize, scaleAt1x = OutputLayout::Scale), comp1x. QueueHash3D and ReadBackCaptureFrame read MainFramebuffer at S3D, so hash3d must be identical with outcap on and off. Compute uses the same GLCompositor, so it gets the cap too.

## What it covers
The 256 view, WideMelon, hi-res capture and Smooth 2D modes 1 and 2. Still excluded (cap forced off while active): Smooth 2D's identity mode (`Filter2D 3`) and Shadow2D (gpu2d, debug-only, gpu2dwide's too).

- **WideMelon:** a wide DOWN3D variant of each mode (`Down3D[1][mode]`). The 3D target is as wide as the output, so the 3D column of an output sub-pixel is its column at S3D plus the scroll, middle and wings alike. Wings composite per sub-sample in mode 0; modes 1 and 2 composite once on the averaged 3D and the probe asks whether any of the block's rows has 3D. Held wings are copies of the capped output.
- **Hi-res capture:** banks and scratch stay at `256*Scale` (= Sout). The capture pass has DOWN3D variants per mode (`HiresCaptureDown3DFS`). Mode 0: composite each sub-sample, average colA (6 bits, rounded), alphaA set when at least half the sub-samples have it. Modes 1/2: one composite on the averaged 3D. Not bit-exact vs the uncapped capture (5-bit words). Banks drop on a scale change, so a few frames show the native capture. If that's ever a problem: resample the banks to the new Sout (~60–100 lines), or keep the capture at S3D (exact, 32 MB banks at 16x).
- **Smooth 2D (phase 3):** `Filter2D.Down3D[wide][mode]` variants, built by `PrepareCappedFilter` for the current mode only (plus the exact one if that fails); `SetRenderSettings` won't cap with the filter on unless one is built. The map is made at the scale the uncapped draw uses (from Scale3D), so mode 0 stays exact. When Sout is a multiple of the map scale every sub-pixel shares one map texel; otherwise the corner sub-pixels are compared, then the full planes, and `FilterDown3D` composites each sub-pixel exactly if the planes differ. Comp1x's filtered halves at Sout=4 with the 4x map still fall back to a full draw.
- **New frame textures:** `FrameQueue` makes storage with `glTexImage2D(NULL)` and clears it on the GPU through a temporary framebuffer (zero upload as a fallback if it's incomplete).

## Switches
Documented in `DebugProperties.h`; also in the desktop build's `kIntSwitches`.
- `outcap`, default 1: caps from the frontend's need, 0 off (the old output exactly). A need of 0 (unknown, the desktop build's) is no cap. Bits 2 and 4 come from plan-deband-outcap part B.
- `outcapneed=N`: overrides the need (A/Bs, desktop).
- `outcap3d`, default 3: 0 exact supersample, 1 bilinear average + one composite, 2 alpha-weighted, 3 edge-aware (textureGather the block's alphas: all the same → mode 1's path, mixed → mode 0's exact composite, so silhouettes have no fringe).
- Saturating blends: compmode 1 (3D on bottom, blending) clamps at 0x3F, and EVA + EVB can reach 32. Above 16 together, clamping the composite of the averaged 3D isn't the average of the clamped composites (EVA = EVB = 16, 2D 32, texels 63/63/0/0: exact 192, one composite 255). Mode 3 sends those pixels (`BlendSaturates`) to the exact composite in the middle, the wings and hi-res capture's source A, so its same-alpha path is only off by rounding. Modes 1 and 2 keep that error. No other compmode or master brightness can clamp.
- `OUTCAP:` log lines say which scale was picked, why when it isn't capped, and end with `hirescap N filter2d N`.

## Exactness checks (desktop)
1. r=1: `--screenshot` byte-identical with OutputCap=0 vs OutputCap=1 OutputCapNeed=16 at `--scale 16`; HASH3D identical.
2. `--scale 16 --set OutputCap=1 --set OutputCapNeed=8` must equal a 2:1 box filter ((sum+2)/4 per channel) of the uncapped `--scale 16` screenshot, bit for bit. Box-filter each half separately (padding: half 1 starts at row 194*16 in source, 194*8 in result). `--fixed-time`, same frame count, hi-res capture and gpu2d off, filter off, 256 view.
3. vs a straight `--scale 8`: differences only in native pixels showing 3D (sanity count).
4. HASH3D same in all runs.

## Risks
Variant build hitches (built at SetRenderSettings), fp16 (DOWN3D is highp; the uncapped reference at 8x and below is mediump), fold/unfold reallocs (the 30-frame hold), thread handoff (atomic), external displays keep the max.

## Not done
- **Check mode `outcap=2`** (like `comp1x=3`): every 30 frames, draw capped into a check target and compare with a CPU box filter of the full draw. Exact only above 8x on fp16 GPUs. (Bit 2 of `outcap` now means something else since part B, so this would need another value.)
- **`ScaleFits` when capped:** only `192*S3D` and `(384+2)*Sout` would need to fit, but the cap would have to be forced on in every case that turns it off (start-up need 0, `outcap=0`, identity filter, gpu2d, a failed DOWN3D build), and a fallback would reallocate the 3D targets from `SetOutputNeed`. The 4096-limit GPUs it would help can't really run 16x 3D.
- The NSMB title flowers that came and went on the Fold's inner screen weren't the cap: the wide view pushed the frame past the 2048-polygon limit, fixed by WidePolys ([plan-widemelon-polylist.md](plan-widemelon-polylist.md)).
