# Debug switches (debug builds only)

The full, current list with defaults is the comment block in `app/src/main/cpp/debug/DebugProperties.h`. This file explains the older ones. Compute switches are explained in `HANDOFF-compute.md`, and the GPU discovery ones (`gltimers`, `scenestats`, `hash3d`, `gltrans`, ...) in `docs/plan-gpu-discovery.md`.

Set with `adb shell setprop debug.melonade.<name> <value>` before starting a game; clear with `adb shell "setprop debug.melonade.<name> ''"`. Without adb, type `name=value` pairs into the switch field in Settings → Performance → Diagnostics; they apply at the next game start. The debug log's `Debug switches` line shows what took.

- `threaded2d` 1/0: overrides the threaded 2D setting.
- `hash2d` 1: logs `HASH2D frame=<n> top=<hex> bottom=<hex>` (tag `melonade`) each frame, an FNV-1a over both 2D back buffers. Proves a 2D change is output-identical. At 60 FPS the core's log limit (50 lines a second per call site) drops about 15% of the lines.
- `cpusemu` / `cpus2d`: comma lists of CPU ids to pin the emulator / 2D thread.
- `defercap` 0: turns off deferred display capture (OpenGL). `earlyflush` 0: turns off the early GPU flush.
- `capreads` 1: logs how often games read display capture results and why held capture lines get drawn early (`CAPREADS` and `CAPFLUSH` once a second, sampled `CAPTRACE` and `CAPDIRTY` per frame); see `GPU_CaptureReads.h`.
- `bnohold` (OpenGL/compute): `1`, the debug build default, draws engine B's part of held capture lines right away and holds only engine A's; `0` is the old path. Release builds stay off.
- `hirescap`: `0` off, `1` on (overrides "Upscaled display capture"), `log` adds a once-a-second `HIRESCAP` line, `check` also runs the pass at 1x and compares against native VRAM every 30 passes. See `docs/hires-capture.md`.
- `halfcam` (compute only): frame interpolation for 30 FPS games. `4` is the one to use; `1` camera only, `2` adds characters, `3` adds batch logging, `5` a per-pair trace. See `docs/shelved/frame-interpolation.md`.
- `gpu2dstats` 1: once-a-second `GPU2DSTATS eng=A|B` line counting BG types, sprite features and mid-frame VRAM/palette/OAM changes; fields in `GPU_2DStats.h`.
- `gpu2d` (OpenGL/compute): the GPU 2D shadow renderer. `0` off, `1` only lines that gain something, `all` every eligible line, `check` all plus a 1x readback compared with CPU 2D every 30 frames, `log` adds a once-a-second `GPU2D` line. `gpu2dwide` 1: every layer draws past the screen edge (test only). See `docs/plan-gpu-2d-shadow.md`.
- `widedrops` 1: once-a-second `WIDEDROPS` line on polygons dropped because polygon or vertex RAM was full, and how many the native view had room for. See `docs/plan-widemelon.md`.
- `filter2d` (OpenGL/compute): `0` off, `1` on (overrides "Smooth 2D edges"), `2` only the 2x MMPX pass, `3` all passes with identity output, which must match off. See `docs/plan-gpu-2d.md`.
- `vertexid` 1: once-a-second `VERTEXID sec` line on vertex identity between 3D scenes; 2 adds `VERTEXID scene`. Fields in `GPU3D_VertexId.h`.
- `blitflush` 0 (compute): in frames that hold capture lines, no GPU flush right after the line-192 Blit.
- `lateblit` 0: with threaded 2D on OpenGL/compute, the frame-end 2D barrier and the Blit stay at line 192 instead of just before the line-215 3D render.
- `capspin` 0 (compute): the capture readback sleeps in its map instead of first polling a fence for up to 6000 µs; another value sets that limit (max 20000).
- `shadowfix` 0 (OpenGL): clear the shadow mask over the whole screen for every mask, instead of per line like software. `maskdepth` 0: depth-equal shadow masks and shadows use plain GL_LESS/GL_LEQUAL, without the DS margin. `equaldepth` 0: the same for all other depth-equal polygons. `shadowlog` 1 (or a frame count): for the first 300 frames, `SHADOWSTATS` and `SHADOWLOG` lines listing masks and shadows.
