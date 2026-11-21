# Docs

Notes for working on melonadeDS. The user-facing overview is the [README](../README.md) at the root, and the mods (WideMelon and Lua) are documented in [MODS.md](../MODS.md).

- [plan-audio.md](plan-audio.md): the audio pass. Steps 1, 2 and 4 done (no phone listening check on record yet), step 3 and the rest open.
- [plan-optimization.md](plan-optimization.md): build flags, hot spots and the JIT. Build flags on, late Blit and held 2D lines done, dispatcher stub on, linking shelved; PGO built but off by default (11-17, opt-in with `-PnativePgo=use`). Left: scheduler and DMA.
- [plan-vulkan.md](plan-vulkan.md): a Vulkan 3D renderer. Plan only, waiting on its gate.
- [plan-gpu-2d.md](plan-gpu-2d.md): filtered, hi-res and wide 2D, with a GPU renderer as a display-only shadow of CPU 2D. Steps 1 (counters) and 2 (filtered 2D) done; step 3 in progress.
- [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md): step 3, the GPU shadow 2D renderer. Increments 1 and 5 done and phone-checked; next is the OnePlus display stall in mode `all`, then windows (increment 4).
- [plan-compute-efficiency.md](plan-compute-efficiency.md): less GPU time, memory and battery from the Compute renderer. Phase 1 (timers, frame queue trim, render pacing, program cache) built and merged; its open list and Phase 2 (shader work) remain.
- [plan-gpu-discovery.md](plan-gpu-discovery.md): where the GPU's cycles go on OpenGL and Compute, and the experiments behind debug switches (T1, K4, E1, E2/D1, R1, T2). Merged; a short "Left open" list remains.
- [plan-t2-texcache.md](plan-t2-texcache.md): the OpenGL texture cache (T2), as built.
- [plan-k4-output-cap.md](plan-k4-output-cap.md): the output-resolution cap (K4), as built.
- [plan-deband-outcap.md](plan-deband-outcap.md): cheaper Deband and the cap's part B, as built. Open: the external-display check.
- [plan-widemelon.md](plan-widemelon.md): improving WideMelon, mainly 2D in the wings. Steps 1 to 3 and camera stages 1 and 2 merged; wide 2D waits for the GPU 2D renderer.
- [plan-expand-parity.md](plan-expand-parity.md): everything PokeDaisy's EXPAND does that WideMelon doesn't yet (game-aware auto zoom, field and battle 2D in the margins), in phases. Phase 1 (game-aware auto zoom) built and phone-checked on `feature/expand-parity-phase1`, not pushed. Next: the camera's render-width cost.
- [plan-widemelon-polylist.md](plan-widemelon-polylist.md): the bigger polygon list (WidePolys, on since 11-14), as built.
- [plan-precise-depth.md](plan-precise-depth.md): the White 2 Castelia pier depth fights, what was ruled out, and the Precise depth setting (11-18).
- [plan-desktop-app.md](plan-desktop-app.md): melonadeDS as a Windows app, in phases. Phases 0 to 2 merged (shared native layer, app shell, game library with art sync); phase 3 (presentation parity) is next. How to build and run it: [desktop/README.md](../desktop/README.md).
- [plan-pc-mod-runner.md](plan-pc-mod-runner.md): running per-game Lua mods on the PC.
- [plan-launcher.md](plan-launcher.md): the game library and RetroAchievements views. Built; picking a match by hand is open.
- [plan-shield.md](plan-shield.md): the NVIDIA Shield TV (Tegra X1+, ARMv8.0), the slowest CPU we've tested. First runs done; the armv8.2 rollback is still open.
- [plan-swi-test-rom.md](plan-swi-test-rom.md): the clean-room SWI test ROM for FreeBIOS. Built and run; the DSi follow-ups are open.
- [hires-capture.md](hires-capture.md): upscaled display capture. Phase 1 done, Phase 2 (bitmap BGs) open.
- [pokemon-battle-ram.md](pokemon-battle-ram.md): Pokemon battle RAM layout for the battle info mod.
- [blackmagic3.md](blackmagic3.md): what we borrowed from upstream's blackmagic3 instead of porting it. Done.
- [polish.md](polish.md): the finished cleanup pass over `material3`: leftovers and two device-free check recipes (its rules moved to coding-standards.md).
- [test-engine-b-nohold.md](test-engine-b-nohold.md): the engine B no-hold test. Done and passed; on in release since 11-16. Kept for its rerun recipe.
- [debug-switches.md](debug-switches.md): what the `debug.melonade.*` switches do and how to set them without adb.
- [coding-standards.md](coding-standards.md): the rules coding and review agents work to, and how reviewers weigh a rule break.
- [code-notes.md](code-notes.md): non-obvious facts about the code, and known issues left alone.
- [ideas.md](ideas.md): loose feature ideas, not planned.
- [frame-interpolation.md](frame-interpolation.md): smoothing 30 FPS games. The halfway-camera approach is merged as the "Frame interpolation (Experimental)" setting (was "Smooth 30 FPS games") on OpenGL and Compute.
- [compute-on-mali.md](compute-on-mali.md): the Compute renderer on Mali. Fixed (a driver bug with atomics on `uvec4` arrays) and offered on every GLES 3.2 GPU since 11-13.
- [shelved/compute-on-mali-upstream.md](shelved/compute-on-mali-upstream.md): the same Mali fix on an upstream branch, not posted.
- [shelved/widemelon-software.md](shelved/widemelon-software.md): WideMelon on the software renderer. Shelved.
- [shelved/d3-edgefog.md](shelved/d3-edgefog.md) and [shelved/r2-fragdepth.md](shelved/r2-fragdepth.md): two GPU discovery experiments that didn't pay, with their patches.

`PERF-NOTES.md` (phone measurements) and `HANDOFF.md` (working notes) are the maintainer's local files at the repo root. They are not in git, so where these docs cite `PERF-NOTES.md` the numbers only exist on that PC.
