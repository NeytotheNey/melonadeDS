# WideMelon on the software renderer (shelved)

Looked at on 2026-10-30 and set aside. It's a much bigger job than Compute was, and it only matters if someone plays on Software, which is already the slow path on phones. The 3D passes would also cost about W/256 more: ~1.33× at 16:9, ~1.66× at 20:9. How WideMelon works on OpenGL and Compute is in [MODS.md, "WideMelon"](../../MODS.md#widemelon).

Paths are under `melonDS-android-lib/src/` unless they start with `app/`. Line numbers are as of core commit `f3da2729`.

## Why it's bigger than Compute

Compute reused the GL compositor, which draws the wings and the wing hold. Software has no GL compositor: the core builds two 256×192 CPU framebuffers (`GPU.cpp`, stride 256 in `GPU2D_Soft.cpp:171`) and the app uploads them as is (`app/src/main/cpp/MelonInstance.cpp` `glTexSubImage2D`, `topScreenWidth` fixed at 256). So the wings would have to be filled on the CPU in `GPU2D_Soft::DrawScanline`'s software branch, running the wings rule (`:304-337`) directly and filling like `WingPixel()` in `GPU_OpenGL_shaders.h`. The control-word flags aren't written in this mode (`job.WideView` needs `Accelerated`, `GPU.cpp:1031`). Both framebuffers would need a W stride, since the wings follow unit A to either screen.

## What would need changing

- `GPU3D_Soft`: fixed 258×194 static buffers (`.h:463-481`), span clamps `xlimit > 256` (`.cpp:860-913`, `1109-1298`), `x < 256` loops in edge marking, fog and AA (`:1465-1578`), border column at `x+257` and a rear-plane `u8` X that wraps at 256 (`:1643-1700`). Edges come from the 9-bit `FinalPosition[0]` (`& 0x1FF`, `GPU3D.cpp:1148`); the wide X only goes into `HiresPosition` today, so it needs a 10+ bit wide X. It also needs `SetViewWidth`/`GetViewWidth`/`GetOutputWidth` overrides.
- The 3D line handoff: `GPU3D::GetLine`'s 256-wide `ScrolledLine` and `Line3DBuffer[256]` (`GPU2D.h:171`). The per-scanline semaphore for threaded 3D doesn't change.
- Capture must stay center 256: `DoCapture` reads `_3DLine` (`GPU2D_Soft.cpp:421-469`), so pass it `_3DLine + (W-256)/2`.
- Other users of the framebuffers that would need the W stride: `fbsize`, `LogHash2D`, the Lua overlay CPU blend, `ScreenshotRenderer`.
- App: the frame upload and `topScreenWidth` in `MelonInstance.cpp`, and the "Software → 256" rule in the Kotlin gating (`WidescreenLaunch`, `EmulatorViewModel`, `RomModsUi`).
- The wing hold only exists in the GL compositor. It would need a CPU port, or go without it (Phantom Hourglass and Mario Kart DS would flicker).
