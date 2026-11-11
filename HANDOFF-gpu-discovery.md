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

## Next, in order

1. **Push** core then app, if not done yet (`git log origin/compute-efficiency..compute-efficiency` in each).
2. **Wider check of T1:** the White 2 overworld (no clear0 pass there, so only bit 1 can matter), one or two other 3D games, and the OnePlus 7 Pro. Same switches: `gltimers=1 scenestats=1 hash3d=1 gltrans=0/3`. If it holds, make `gltrans=3` the default.
3. **T1c: remove the clear-alpha-0 pass with framebuffer fetch.** It's now the biggest piece left, about 16% of the title frame. With the destination color readable in the shader, pass 3's translucent draw can skip blending where the destination alpha is 0, which is what pass 2 exists for. Then pass 2 goes. Notes:
   - **Needs a runtime check and the old path as fallback.** Use it only when `GL_EXT_shader_framebuffer_fetch` is listed; otherwise keep today's two passes. Adreno (both our phones) has it. Mali drivers have long shipped `GL_ARM_shader_framebuffer_fetch`, and newer ones may also list the EXT version. It's unverified on the Pixel 9 Pro Fold (Mali), which the init log line will show. Desktop NVIDIA/AMD GL doesn't have it, so the desktop build would run the fallback. The ARM variant reads color only, which may be enough here (only dst alpha is needed), but it has its own rules with multiple render targets; check before supporting it.
   - The hard part is shadows: pass 2 draws shadows over the clear plane only when their ID matches the clear plane's, and its stencil marks feed pass 3. Keep that behavior.
   - Check with `hash3d` against `gltrans=3`, plus screenshots against software. Bitmap-clear games may change, since pass 2 today tests the clear attribute's alpha, not the bitmap's per-pixel alpha.
4. Then the rest of Phase 1 in the plan's order: R2 (W-buffer `gl_FragDepth` only where needed) before R1 (no-`discard` opaque programs), E2 (skip the dead edge pass), K1 (compositor), D1.

## Known, left alone

- In pass 2, a matching-ID shadow turns blending on, and the next non-needopaque translucent polygon in pass 2 then blends when it shouldn't. Pre-existing; T1 keeps the same behavior.
- Pass 3 can blend an earlier translucent polygon over a later needopaque polygon's opaque pixel when the earlier one doesn't write depth (GL draws pass 2's results first). Pre-existing GL inaccuracy, not checked against software.
