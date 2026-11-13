# Handoff: GPU discovery (OpenGL efficiency)

The plan and results are in [docs/plan-gpu-discovery.md](docs/plan-gpu-discovery.md); numbers are in `PERF-NOTES.md` from "GPU discovery Phase 0" on. This file is the current state and what's next. Build and install commands are in `HANDOFF.md`. Leave `.claude/launch.json`, `gradle/gradle-daemon-jvm.properties` and the local Compute-on-Mali bypass in `VideoPreferencesFragment.kt` (`isAdrenoGpu = true // MALI-TEST`) out of commits.

## State (11-13)

Branch `compute-efficiency` in the app and the core. Latest: core `3f7d5201` and app `9942c685` (K4 phase 3, Smooth 2D under the cap). Not pushed as of 11-13; everything before them (K4 included) is pushed.

**Defaults in every build:** T1 (`gltrans=3`), K2c (`comp1x=1`), K3 (`filter2drows=1`), E2 (`gledgeskip=1`), D1 (`glclear=3`), K4 (`outcap=1`, `outcap3d=3`). Off-by-default switches: T1c (`gltrans` bit 2), K2a (`compclear`), R2 is shelved.

**E2 and D1** (PERF-NOTES "E2 and D1" through "Fold at 12x and 16x"). `gledgeskip=1` skips the final-pass edge draw (the edge FS returns 0 unless attr G is set, and every attr writer writes 0 or masks G) and the whole final pass when fog is off; once E1 lands, it must key on whether any polygon this frame can set the flag. `glclear` bit 0 = `glClearBuffer` instead of the plain clear quad (color `{b,g,r,a}/31`, attr `{id/63,0,fog,1}`, depth `z/2^24`, same stencil ref, so T1c's 0xBF and `CountCoveredPixels`' 0xFF still hold), bit 1 = invalidate attr and depth/stencil after the frame. Reviewed clean, hash3d-exact on every aligned frame (desktop, Fold, Pixel). `gledgeskip=1 glclear=3` saves 4–7% GPU cycles at 12x/16x on both phones and 4% at 8x on the Pixel; at 8x on the Fold it's noise. **`gledgeskip=1 glclear=3` is the default since 11-12**; `gledgeskip=0 glclear=0` for A/Bs.

**12x and 16x internal resolution** (OpenGL; Compute is capped at 8x in `RendererConfiguration.kt`, its work buffers reach 805 MB at 16x). Both renderers fall back to the largest scale that fits the GPU limits (`FitScale`, logs a warning). Above 8x the compositor's plain program is built highp (fp16 would cap 3D detail at ~6–8x on the right side). Reviewed clean. The Fold holds 60 fps at 16x in White 2 (GPU ~83% busy, heats up); the Pixel gets ~41 fps at 16x and holds 60 at 12x. The user wants the options left as they are, even though the UI shows the setting rather than the scale used (Compute "16x" runs 8x).

**Pixel 8 (Mali-G715, dev phone, wipe/root OK) is in every benchmark, at 8x.** GPU cycles: `adb shell gpu_counter_producer -f &`, then perfetto `gpu.counters` with explicit `counter_ids` (1 = any queue active cycles; parser `perf_logs/2026-11-12-pixel8-scale16/scripts/gpuff.py`). GL holds 60 at 8x, so pair it with fast-forward at unlimited speed when fps matters. GLTIME doesn't work on Mali (disjoint queries). Compute renders cleanly with the Mali fix but drops to ~41 fps at 8x.

**K4, the output cap** (PERF-NOTES "K4" through "K4 phase 3"; design in [docs/plan-k4-output-cap.md](docs/plan-k4-output-cap.md)). The compositor, frame queue and frontend run at the screen's scale, Sout = S3D/r (r 1–4), while the 3D renders at full scale. The need comes from the DS screen rects in `DSRenderer`, the max over surfaces in `FrameRenderCoordinator`, held for 30 frames in `MelonInstance::runFrame`. It covers the 256 view, the WideMelon view, hi-res capture and, since phase 3 (11-13), Smooth 2D modes 1 and 2. Only Smooth 2D's identity mode and gpu2d turn it off. `OUTCAP:` log lines say which scale was picked, why when it isn't capped, and end with `hirescap N filter2d N`. Mode 0 is bit-exact vs a box filter of the uncapped output on the desktop, with and without Smooth 2D; mode 3 (default) is off by rounding only.

## Next, in order

0. **Check the highp compositor on Adreno.** The Fold's cover screen downsamples too much to judge 12x/16x. Use the inner screen or a native-resolution capture and compare the right side of the top screen at 8x/12x/16x. The coder also suspected an old fp16 issue: on fp16 GPUs the lower part of the bottom screen's 3D may show only 4x vertical detail at 5–8x. Unconfirmed.
1. **R1 for Z-buffer scenes** (plan table). R2 showed no phone has `GL_EXT_conservative_depth`, so W-buffer frames can't get exact early-Z.
2. **Later: memory and GPU work vs Vulkan** (plan "Later: memory" and "WatermelonDS at 16x"). At 16x on the Fold we use 1932 MB PSS / 1047 MB graphics, about the same as WatermelonDS's Vulkan 16x. The bigger gap is GPU work: W2 title 8x ~3.6 vs 2.5 Mcyc (was 4.9 on 11-11), 16x ~12.7 vs 5.0. After K4, the next suspects are their per-pixel 3D (T2, a texture cache decoded once instead of per fragment; NoAttr/color-only pipelines, D2) and D3 (one edge+fog pass, no mid-frame depth detach). Big memory items: the frame queue (up to 9 × ~101 MB at 16x), the GL 3D targets (~250 MB), Compute's work buffers.
3. **Circle back: NSMB title flowers flicker in the unfolded view** (user, 11-13, deferred). On the Fold's inner screen (WideMelon view 426, 16x, Smooth 2D on, capped) the blue and pink flowers on the NSMB title come and go. Folded (cover screen, 256 view) they're always there, and folding and unfolding switches it live. Not known yet whether it's the cap, Smooth 2D or WideMelon itself: try `outcap=0`, Smooth 2D off, and the 256 view on the inner screen. The phone-tester's ~2 shots/s bursts missed it, and the desktop runs were 256 view only.
4. **K4 leftovers, deferred:**
   - (d) relaxing `ScaleFits` when capped: not done. A 4096-limit GPU at 16x would need the cap forced on through the start-up need 0, `outcap=0`, Smooth 2D identity, gpu2d and a failed DOWN3D build, and falling back would mean reallocating the 3D targets from `SetOutputNeed`. Those GPUs can't really run 16x 3D anyway.
   - (g) a capped rescale drops the hi-res banks, so a game holding one captured image shows it at native until the next capture (only after a fold/unfold or resize). If it ever shows: resample the banks to the new Sout (~60–100 lines) or keep the capture at S3D (~32 MB at 16x).
   - Fold/unfold with logs: the user folded and unfolded live with Smooth 2D on and the picture came back each time, but nobody read the `OUTCAP:` lines or looked for GL errors then. That path covers the GPU clear of new frame textures and the Smooth 2D map kept across Sout changes.
   - With Sout below the map scale (e.g. 4x 3D shown at 2x) every pixel takes the exact per-sub-pixel Smooth 2D path, about the uncapped cost.
   - The `outcap=2` check mode. gpu2d stays excluded (debug-only).
5. **Leftovers:** the Pixel's app logs have a CRASH_NATIVE exit report from 05:43 on 11-12, from before the benchmark runs, not looked into. The Pixel's `gpu_counter_producer` was left running (pkill refused; a reboot clears it). Phone files to clean: OnePlus `/data/local/tmp/t1c_title{0,3,7}/`, `t1crun.sh`, `op2d_setcfg.sh`, `op2d_gpuprod`, `gpuprod.log`, `/data/misc/perfetto-traces/t1c_*.pftrace`; Fold `/data/local/tmp/t1cf`, `k2c`, `k4`, `ctf`, `chain_fold.sh`, `chain_fold_ns.sh`, `foldchain.sh`, `foldchain2.sh`, `shots_fold.sh`. PC: duplicate `pl_*` folders in `perf_logs/2026-11-12-pixel8-*`, empty `backup${n}_w2.*` files in `perf_logs/2026-11-13-k4-outcap5/`. Phone runs go to the `phone-tester` agent.

   **Cautions:** before pushing any app file to a phone (rom_data.json, prefs), force-stop the app, or it writes its own copy back. Never use `cmd device_state` on the Fold: an override that wasn't reset on 11-13 left the inner screen black until the user folded and unfolded it by hand.

   **Open:** the Fold's GLTIME total stays ~11 ms while the compositor gets 1.5 ms cheaper.

## Done earlier (details in PERF-NOTES and the plan)

- **T1** (`gltrans` bits 0–1): skip dead need-opaque redraws and scan A3I5/A5I3 textures to skip all-opaque or all-translucent draws. Exact, −4 to −15% on the Fold and OnePlus.
- **T1c** (`gltrans` bit 2, off): framebuffer fetch replaces the clear-alpha-0 pass. Mixed (−7% to +8%); its hash differences are blend rounding. Untested ideas: fetch only for frames with few translucent fragments, find out why the fetch pass costs more per fragment on Adreno, quantize src alpha to 8 bits, use fetch for shadows over alpha-0 pixels. Mali's ARM variant isn't supported.
- **K1** (compositor parts in GLTIME), **K2a** (`compclear`, nothing on Adreno or Mali), **K2c** (plain screens at 1x, −6 to −8%), **K3** (cheaper Smooth 2D edges, −10 to −14%). Untried K3 idea: skip the map fetch on lines with display mode ≥2 or hi-res lines.
- **K4** (`outcap`, `outcap3d`): output capped at the screen's scale, then wide view and hi-res capture, then Smooth 2D (phase 3, 11-13). Phase 3 at 16x with Smooth 2D: Pixel −17% GPU cycles and −685 MB graphics in White 2; Fold compositor −41% and −410 MB graphics. Also: new frame textures are cleared on the GPU, and the Smooth 2D map survives an output-only rescale.
- **R2** shelved (`docs/shelved/r2-fragdepth.md`): exact, but almost nothing qualifies.

## Known, left alone

- In pass 2, a matching-ID shadow turns blending on, and the next non-needopaque translucent polygon in pass 2 then blends when it shouldn't. Pre-existing; T1 keeps the same behavior.
- Pass 3 can blend an earlier translucent polygon over a later needopaque polygon's opaque pixel when the earlier one doesn't write depth (GL draws pass 2's results first). Pre-existing, not checked against software. Both are gone with `gltrans` bit 2 on a GPU with framebuffer fetch.
- Frames with a nonzero clear alpha still clear stencil to 0xFF, so translucent ID 63 can't draw over the clear plane there. `CountCoveredPixels`' `NOTEQUAL 0x7F` relies on that value.
- Desktop: Mesa GLES contexts (Intel/AMD) may list the EXT framebuffer-fetch extension, so the desktop build isn't always on the fallback. The startup log line shows it.
