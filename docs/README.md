# Docs

Notes for working on melonadeDS. The user-facing overview is the [README](../README.md) at the root, and the mods (WideMelon and Lua) are documented in [MODS.md](../MODS.md).

- [plan-audio.md](plan-audio.md): the audio pass. Current: steps 1 and 2 done, 3 to 7 open.
- [plan-vulkan.md](plan-vulkan.md): a Vulkan 3D renderer. Plan only, waiting on its gate after the Phase 0 profile.
- [plan-optimization.md](plan-optimization.md): an optimization session, first build flags (`-O3`, LTO, a newer CPU target), then the hot spots from the phone profile. Plan only.
- [plan-gpu-2d.md](plan-gpu-2d.md): filtered, hi-res and wide 2D, with a GPU renderer as a display-only shadow of CPU 2D. Steps 1 (counters) and 2 (filtered 2D, MMPX) built and OnePlus-tested; step 2's cost in capture scenes is open.
- [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md): plan for plan-gpu-2d's step 3, the GPU shadow 2D renderer: a gameplay counter run, then seven increments from text BGs to bitmap BGs, drawing past 0..255 for WideMelon. Increments 1 and 5 done and phone-checked; the OnePlus stall in mode `all` and windows (increment 4) are next.
- [plan-compute-efficiency.md](plan-compute-efficiency.md): less GPU time, memory and battery from the Compute renderer, aimed at the OnePlus 7 Pro at 4x. Timers, frame queue trim and render pacing built and OnePlus-tested (NSMB's map at 4x went from 2 to 60 FPS shown); shader work and the Fold check open.
- [plan-widemelon.md](plan-widemelon.md): improving WideMelon, mainly 2D in the wings, from PokeDaisy's widescreen notes. Steps 1 to 3 merged; wide 2D waits for the GPU 2D renderer.
- [plan-gpu-discovery.md](plan-gpu-discovery.md): where the GPU's cycles go on OpenGL and Compute, and cheap experiments behind debug switches. Phase 0 and T1 (translucent draw skips, -15% at White 2's title) done; T1c next.
- [plan-shield.md](plan-shield.md): the NVIDIA Shield TV (Tegra X1+, ARMv8.0), the slowest CPU we've tested. First runs done; the armv8.2 rollback is tentative, measurements and fixes open.
- [hires-capture.md](hires-capture.md): upscaled display capture. Phase 1 done, Phase 2 (bitmap BGs) open.
- [blackmagic3.md](blackmagic3.md): what we borrowed from upstream's blackmagic3 instead of porting it. Done, with a few follow-ups.
- [polish.md](polish.md): the cleanup and polish pass over `material3`, with its leftovers and two device-free check recipes. Done.
- [debug-switches.md](debug-switches.md): what the older `debug.melonade.*` switches do and how to set them without adb.
- [code-notes.md](code-notes.md): non-obvious facts about the code, and known issues left alone.
- [test-engine-b-nohold.md](test-engine-b-nohold.md): the pending OnePlus test of the engine B no-hold fix.
- [ideas.md](ideas.md): loose feature ideas, not planned.
- [shelved/frame-interpolation.md](shelved/frame-interpolation.md): smoothing 30 FPS games. Old attempts shelved; a halfway-camera prototype is merged behind a debug switch, compute only.
- [shelved/widemelon-software.md](shelved/widemelon-software.md): WideMelon on the software renderer. Shelved.
- [shelved/compute-on-mali.md](shelved/compute-on-mali.md): the Compute renderer on a Pixel 9 Pro Fold. It runs, but the 3D is broken in tile-sized blocks; what was tested and what to try next. Shelved.

`PERF-NOTES.md` (phone measurements) and `HANDOFF.md` (working notes) are the maintainer's local files at the repo root. They are not in git, so where these docs cite `PERF-NOTES.md` the numbers only exist on that PC.
