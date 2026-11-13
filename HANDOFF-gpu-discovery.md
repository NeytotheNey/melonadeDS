# Handoff: GPU discovery (OpenGL efficiency)

The plan and results are in [docs/plan-gpu-discovery.md](docs/plan-gpu-discovery.md); numbers are in `PERF-NOTES.md` from "GPU discovery Phase 0" on. This file is the current state and what's next. Build and install commands are in `HANDOFF.md`. Leave `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties` and the local Compute-on-Mali bypass in `VideoPreferencesFragment.kt` (`isAdrenoGpu = true // MALI-TEST`) out of commits.

## State (11-13)

Branch `compute-efficiency` in the app and the core. Latest: core `3efacbb9` and app `15506188` (K4, the output cap), not pushed as of 11-13; everything before them is pushed.

**Defaults in every build:** T1 (`gltrans=3`), K2c (`comp1x=1`), K3 (`filter2drows=1`), E2 (`gledgeskip=1`), D1 (`glclear=3`), K4 (`outcap=1`, `outcap3d=3`). Off-by-default switches: T1c (`gltrans` bit 2), K2a (`compclear`), R2 is shelved.

**E2 and D1** (PERF-NOTES "E2 and D1" through "Fold at 12x and 16x"). `gledgeskip=1` skips the final-pass edge draw (the edge FS returns 0 unless attr G is set, and every attr writer writes 0 or masks G) and the whole final pass when fog is off; once E1 lands, it must key on whether any polygon this frame can set the flag. `glclear` bit 0 = `glClearBuffer` instead of the plain clear quad (color `{b,g,r,a}/31`, attr `{id/63,0,fog,1}`, depth `z/2^24`, same stencil ref, so T1c's 0xBF and `CountCoveredPixels`' 0xFF still hold), bit 1 = invalidate attr and depth/stencil after the frame. Reviewed clean, hash3d-exact on every aligned frame (desktop, Fold, Pixel). `gledgeskip=1 glclear=3` saves 4–7% GPU cycles at 12x/16x on both phones and 4% at 8x on the Pixel; at 8x on the Fold it's noise. **`gledgeskip=1 glclear=3` is the default since 11-12**; `gledgeskip=0 glclear=0` for A/Bs.

**12x and 16x internal resolution** (OpenGL; Compute is capped at 8x in `RendererConfiguration.kt`, its work buffers reach 805 MB at 16x). Both renderers fall back to the largest scale that fits the GPU limits (`FitScale`, logs a warning). Above 8x the compositor's plain program is built highp (fp16 would cap 3D detail at ~6–8x on the right side). Reviewed clean. The Fold holds 60 fps at 16x in White 2 (GPU ~83% busy, heats up); the Pixel gets ~41 fps at 16x and holds 60 at 12x. The user wants the options left as they are, even though the UI shows the setting rather than the scale used (Compute "16x" runs 8x).

**Pixel 8 (Mali-G715, dev phone, wipe/root OK) is in every benchmark, at 8x.** GPU cycles: `adb shell gpu_counter_producer -f &`, then perfetto `gpu.counters` with explicit `counter_ids` (1 = any queue active cycles; parser `perf_logs/2026-11-12-pixel8-scale16/scripts/gpuff.py`). GL holds 60 at 8x, so pair it with fast-forward at unlimited speed when fps matters. GLTIME doesn't work on Mali (disjoint queries). Compute renders cleanly with the Mali fix but drops to ~41 fps at 8x.

## Next, in order

0. **K4 phase 3: Smooth 2D under the output cap. Start here.**

   **Where K4 stands.** It's committed and on by default (`outcap=1`, `outcap3d=3`). Background is in PERF-NOTES "K4" and "K4 second pass"; the design, switches and "Remaining steps" are in [docs/plan-k4-output-cap.md](docs/plan-k4-output-cap.md).
   - The compositor, frame queue and frontend run at the screen's scale, Sout = S3D/r with r a whole number from 1 to 4. The 3D still renders at full scale.
   - The need comes from the DS screen rects in `DSRenderer`, takes the max over surfaces in `FrameRenderCoordinator`, and is held for 30 frames in `MelonInstance::runFrame`.
   - Capped compositing works in the 256 view, the WideMelon wide view and hi-res capture.
   - Mode 3 is an edge-aware bilinear: `textureGather` reads each block's alphas. It takes the exact per-sub-sample path on mixed-alpha blocks and on saturating compmode-1 blends (EVA+EVB > 16).
   - At 16x: Fold graphics PSS 1252 → ~500 MB, compositor 3.1 → 1.6 ms; Pixel −14% GPU cycles. Runtime rescale (size change, rotation) was checked on the Pixel.
   - `OUTCAP:` log lines (change-only, also in release) say which scale was picked, and why when it isn't capped.
   - Still excluded: Smooth 2D (`Filter2D`, pref `video_filter_2d`) and the gpu2d shadow, in `GLCompositor::OutputNeed`. So players with Smooth 2D on get no cap.

   **This phase, in order:**
   1. Port Smooth 2D to the capped (DOWN3D) compositor for all `outcap3d` modes, then remove its exclusion. Notes:
      - `FilterFetch3D` builds its sub-sample positions from `subPixel*r + (i,j)`.
      - The filter map's texel is the same for all r² sub-pixels when Sout is a multiple of the map scale.
      - The identity mode (`Filter2D 3`, mediump) stays uncapped.
      - Check comp1x's filtered halves at Sout=4. On the cover screen they fall back to a full draw.
   2. Leftovers:
      - (d) relax `ScaleFits` when capped: only `192*S3D` and `(384+2)*Sout` need to fit, so 4096-limit GPUs could run 16x 3D;
      - (e) clear new frame textures on the GPU instead of with the zeroed CPU vector (`FrameQueue.cpp:200`), which now runs on every fold/unfold;
      - (f) the Kotlin comments in `DSRenderer`, `FrameRenderCoordinator` and `MelonEmulator` still call the cap only `debug.melonade.outcap`;
      - (g) a capped rescale drops the hi-res banks, so a held captured image shows native until the next capture. Decide if that matters.
      - Skip the `outcap=2` check mode unless it's cheap. gpu2d stays excluded (debug-only).
   3. Validate on the desktop first. The earlier scripts lived in a session scratchpad and are gone, so rebuild what's needed:
      - capped vs a box filter of uncapped screenshots, each screen half separately (padding rows: half 1 starts at row 194·S);
      - HASH3D identical (needs a desktop build with `MELONADE_DEBUG_TOOLS`);
      - mode 0 bit-exact, r=1 byte-identical to `OutputCap=0`;
      - modes 1 and 3 differing only on pixels that show 3D.
      
      Use `--fixed-time`, the same frame count, and `--set OutputCap=1 --set OutputCapNeed=8` (the desktop has no frontend need, so it never caps on its own).
   4. Phone A/B at 16x with Smooth 2D on, `outcap=0` vs the default, Fold (inner screen) and Pixel 8: White 2 overworld, plus the NSMB title, which is on both phones. Use the phone-tester agent and the quick recipe, and put the logs in `perf_logs/`.

   **Done looks like:** Smooth 2D runs under the cap on both phones with numbers in PERF-NOTES; the leftovers are handled or deferred with a reason; a clean fresh review; this file and the plan doc updated.

   **Open, not this phase:** the Fold's GLTIME total stays ~11 ms while the compositor gets 1.5 ms cheaper.

   **Caution:** before pushing any app file to a phone (rom_data.json, prefs), force-stop the app, or it writes its own copy back. A K4 run lost White 2's `wideViewWidth` 342 on the Fold that way; it has been restored.
1. **Check the highp compositor on Adreno.** The Fold's cover screen downsamples too much to judge 12x/16x. Use the inner screen or a native-resolution capture and compare the right side of the top screen at 8x/12x/16x. The coder also suspected an old fp16 issue: on fp16 GPUs the lower part of the bottom screen's 3D may show only 4x vertical detail at 5–8x. Unconfirmed.
2. **R1 for Z-buffer scenes** (plan table). R2 showed no phone has `GL_EXT_conservative_depth`, so W-buffer frames can't get exact early-Z.
3. **Later: memory and GPU work vs Vulkan** (plan "Later: memory" and "WatermelonDS at 16x"). At 16x on the Fold we use 1932 MB PSS / 1047 MB graphics, about the same as WatermelonDS's Vulkan 16x. The bigger gap is GPU work: W2 title 8x ~3.6 vs 2.5 Mcyc (was 4.9 on 11-11), 16x ~12.7 vs 5.0. After K4, the next suspects are their per-pixel 3D (T2, a texture cache decoded once instead of per fragment; NoAttr/color-only pipelines, D2) and D3 (one edge+fog pass, no mid-frame depth detach). Big memory items: the frame queue (up to 9 × ~101 MB at 16x), the GL 3D targets (~250 MB), Compute's work buffers.
4. **Leftovers:** the Pixel's app logs have a CRASH_NATIVE exit report from 05:43 on 11-12, from before the benchmark runs, not looked into. Phone files to clean: OnePlus `/data/local/tmp/t1c_title{0,3,7}/`, `t1crun.sh`, `op2d_setcfg.sh`, `op2d_gpuprod`, `gpuprod.log`, `/data/misc/perfetto-traces/t1c_*.pftrace`; Fold `/data/local/tmp/t1cf` and `/data/local/tmp/k2c`. PC: duplicate `pl_*` folders in `perf_logs/2026-11-12-pixel8-*`. Also from the K4 runs (rm was held each time): Fold `/data/local/tmp/foldchain*.sh`, `foldrun.sh`, `chain_fold*.sh`, `shots_fold.sh`, `k3/`, `k4/`, `ctf`; Pixel `/data/local/tmp/pxs/`, `pxrun.sh`, `chain*.sh`, `chain.txt`, `chain_px*.sh`, `shots_px.sh`, `t_test`, and `/data/misc/perfetto-traces/s_o*.pftrace`, `s_t_test.pftrace`, `s_d_*`/`s_n_*` traces; PC: empty `backup${n}_w2.*` files in `perf_logs/2026-11-13-k4-outcap5/`. Phone runs go to the `phone-tester` agent.

## Done earlier (details in PERF-NOTES and the plan)

- **T1** (`gltrans` bits 0–1): skip dead need-opaque redraws and scan A3I5/A5I3 textures to skip all-opaque or all-translucent draws. Exact, −4 to −15% on the Fold and OnePlus.
- **T1c** (`gltrans` bit 2, off): framebuffer fetch replaces the clear-alpha-0 pass. Mixed (−7% to +8%); its hash differences are blend rounding. Untested ideas: fetch only for frames with few translucent fragments, find out why the fetch pass costs more per fragment on Adreno, quantize src alpha to 8 bits, use fetch for shadows over alpha-0 pixels. Mali's ARM variant isn't supported.
- **K1** (compositor parts in GLTIME), **K2a** (`compclear`, nothing on Adreno or Mali), **K2c** (plain screens at 1x, −6 to −8%), **K3** (cheaper Smooth 2D edges, −10 to −14%). Untried K3 idea: skip the map fetch on lines with display mode ≥2 or hi-res lines.
- **R2** shelved (`docs/shelved/r2-fragdepth.md`): exact, but almost nothing qualifies.

## Known, left alone

- In pass 2, a matching-ID shadow turns blending on, and the next non-needopaque translucent polygon in pass 2 then blends when it shouldn't. Pre-existing; T1 keeps the same behavior.
- Pass 3 can blend an earlier translucent polygon over a later needopaque polygon's opaque pixel when the earlier one doesn't write depth (GL draws pass 2's results first). Pre-existing, not checked against software. Both are gone with `gltrans` bit 2 on a GPU with framebuffer fetch.
- Frames with a nonzero clear alpha still clear stencil to 0xFF, so translucent ID 63 can't draw over the clear plane there. `CountCoveredPixels`' `NOTEQUAL 0x7F` relies on that value.
- Desktop: Mesa GLES contexts (Intel/AMD) may list the EXT framebuffer-fetch extension, so the desktop build isn't always on the fallback. The startup log line shows it.
