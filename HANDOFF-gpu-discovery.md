# Handoff: GPU discovery (OpenGL translucent work)

The plan and results are in [docs/plan-gpu-discovery.md](docs/plan-gpu-discovery.md); numbers are in `PERF-NOTES.md` under "GPU discovery Phase 0" and "GL translucent skips, T1". This file is the current state and what's next. Build and install commands are in `HANDOFF.md`.

## State (11-11)

Branch `compute-efficiency` in the app and the core. Phase 0 (instruments) is committed: core `3b12e30a`, app `92108551`.

**T1 is committed, not pushed:** core `1ecb3e65`, app `d00aca6c`. App `ac00b98b` makes LeakCanary ignore a second path of the Android framework `TextKeyListener` leak (Android 17 reaches it through the show-secrets setting observer); it compiles, not checked on the phone. Leave `.claude/launch.json` and `gradle/gradle-daemon-jvm.properties` out of commits.

T1 is the `debug.melonade.gltrans` switch (bitmask, read at game start, debug builds only, default 0, settable from the Diagnostics switch field):
- **Bit 0 (T1a):** when the clear-alpha-0 pass (pass 2) ran, the translucent pass (pass 3) skips its opaque redraw of alpha-31 translucent ("needopaque") polygons. Pass 2 already drew them, so the redraw fails depth everywhere and writes nothing. Not skipped for depth-equal (attr bit 14) polygons, since GL_LEQUAL could pass again.
- **Bit 1 (T1b):** each needopaque polygon's texture (A3I5/A5I3) is scanned on the CPU once per frame, deduplicated. No texel that reaches alpha 31 → skip the opaque draw; no texel with alpha 1–30 → skip the translucent draw (not for shadows). Texturing off or decal mode count as all alpha 31.
- **Stats:** a `GLTRANS` line when `scenestats` is on (needopaque counts by class, skips, `scan_us`). At renderer init the debug log lists the three framebuffer-fetch extensions.
- Reviewed by a fresh subagent: no correctness bugs.

**Fold result (White 2 title, OpenGL 8x, unplugged):** 5.00 → 4.25 GPU Mcycles per frame (−15%; bit 0 −10%, bit 1 −5%). HASH3D matched on all 7213 aligned frames. Logs: `perf_logs/2026-11-11-gltrans/`. The Fold has `GL_EXT_shader_framebuffer_fetch` (not the ARM or non-coherent variants).

**T1c is written, not committed, not run on a phone** (11-11, later). It's `gltrans` bit 2, so the A/B is `gltrans=3` vs `7`. With the clear alpha 0 and `GL_EXT_shader_framebuffer_fetch` listed, pass 2 is skipped and pass 3's non-shadow translucent draws use a fetch build of the render shader (`RenderShader[2]/[3]`, built lazily, old path if they fail): blending off, the shader writes the source as is where dst alpha is 0 and otherwise does the fixed-function math (RGB by src alpha if DISP3DCNT bit 3, alpha = max). Shadows keep the normal program and fixed-function blend. Need-opaque opaque parts draw once in pass 3; bit 0's `redrawn` needs pass 2 to have run. In fetch frames the clear writes stencil 0xBF, not 0xFF, so translucent ID 63 isn't kept off the clear plane by pass 3's `NOTEQUAL 0x40|id`. GLTRANS ends with `clear0_frames= fetch_frames=` (counts). A fresh review found no bugs; old path unchanged with bit 2 off or no extension. Builds: APK and desktop pass. APK on the NAS as `melonade-gltrans-t1c-2026-11-11.apk`. The PC's RTX 5090 doesn't list the extension, so the fetch path has never run.

Expected `gltrans=7` vs `3` hash differences, all where the old path disagrees with software: pass 2's draws now happen in list order (a shadow before a translucent on a clear pixel; an earlier no-depth-write translucent no longer blends over a later need-opaque's opaque pixel, known issue 2); matching-ID shadows and translucents on mask-flagged (0xFE) clear pixels draw once, not twice (pass 2's INVERT leaves `0x40|(id^1)` there, an old bug); known issue 1 is gone; clear-bitmap pixels with alpha set now blend. **Rounding risk:** the blend is now in the shader at highp with the unquantized src alpha; Adreno's blend unit may quantize first, so blended pixels can be off by one step. If hashes differ, compare pixels before blaming logic; if only blended pixels are off, try quantizing src alpha to 8 bits.

**Phone results (11-11, PERF-NOTES "T1 wider check and T1c"):** T1 is pixel-exact on the W2 title, W2 overworld and Dark Dawn title (Fold) and the W2 title (OnePlus), saving 4–15%; on the OnePlus it lifts the title from 54 to 60 fps at 8x. T1c ran on both phones (EXT listed, fetch on every clear0 frame) but is mixed: Dark Dawn −7%, OnePlus title −1%, Fold W2 title +8%, because the fetch translucent pass costs nearly as much as the clear0 pass it replaces. Its hash differences are blend rounding (1–2 steps, 0.5% of pixels in a held frame), not logic.

## Next, in order

1. **Make T1 (`gltrans=3`) the default,** including release builds (today the switch is debug-only, default 0). Keep a debug switch to turn it off for A/Bs.
2. **T1c: commit it as an off-by-default switch** (core then app), and don't make it the default. Ideas if it's picked up again, all untested: use fetch only for frames with few translucent fragments; find out why the fetch pass costs more per fragment on Adreno (blending off with `inout` may serialize fragments per pixel); quantize src alpha to 8 bits if hash-exactness matters. Shadows over alpha-0 pixels could use the fetch program too (software writes them unblended). Mali's ARM variant isn't supported.
3. Then the rest of Phase 1 in the plan's order: R2 (W-buffer `gl_FragDepth` only where needed) before R1 (no-`discard` opaque programs), E2 (skip the dead edge pass), K1 (compositor), D1.
4. Phone leftovers to clean: OnePlus `/data/local/tmp/t1c_title{0,3,7}/`, `t1crun.sh`, `op2d_setcfg.sh`, `op2d_gpuprod`, `gpuprod.log`, `/data/misc/perfetto-traces/t1c_*.pftrace`; Fold `/data/local/tmp/t1cf`. Phone runs go to the `phone-tester` agent (Sonnet) from now on.

## Known, left alone

- In pass 2, a matching-ID shadow turns blending on, and the next non-needopaque translucent polygon in pass 2 then blends when it shouldn't. Pre-existing; T1 keeps the same behavior.
- Pass 3 can blend an earlier translucent polygon over a later needopaque polygon's opaque pixel when the earlier one doesn't write depth (GL draws pass 2's results first). Pre-existing GL inaccuracy, not checked against software. Both are gone with `gltrans` bit 2 on a GPU with framebuffer fetch.
- Frames with a nonzero clear alpha still clear stencil to 0xFF, so translucent ID 63 can't draw over the clear plane there. `CountCoveredPixels`' `NOTEQUAL 0x7F` relies on that value; left out of T1c.
- Desktop: Mesa GLES contexts (Intel/AMD) may list the EXT extension, so the desktop build isn't always on the fallback. The startup log line shows it.
