# Docs

Notes for working on melonadeDS. The user-facing overview is the [README](../README.md) at the root, and the mods (WideMelon and Lua) are documented in [MODS.md](../MODS.md).

- [plan-audio.md](plan-audio.md): the audio pass. Current: steps 1 and 2 done, 3 to 7 open.
- [plan-vulkan.md](plan-vulkan.md): a Vulkan 3D renderer. Plan only, waiting on its gate after the Phase 0 profile.
- [plan-optimization.md](plan-optimization.md): an optimization session, first build flags (`-O3`, LTO, a newer CPU target), then the hot spots from the phone profile. Plan only.
- [hires-capture.md](hires-capture.md): upscaled display capture. Phase 1 done, Phase 2 (bitmap BGs) open.
- [blackmagic3.md](blackmagic3.md): what we borrowed from upstream's blackmagic3 instead of porting it. Done, with a few follow-ups.
- [polish.md](polish.md): the cleanup and polish pass over `material3`, with its leftovers and two device-free check recipes. Done.
- [ideas.md](ideas.md): loose feature ideas, not planned.
- [shelved/frame-interpolation.md](shelved/frame-interpolation.md): smoothing 30 FPS games. Shelved.
- [shelved/widemelon-software.md](shelved/widemelon-software.md): WideMelon on the software renderer. Shelved.

`PERF-NOTES.md` (phone measurements) and `HANDOFF.md` (working notes) are the maintainer's local files at the repo root. They are not in git, so where these docs cite `PERF-NOTES.md` the numbers only exist on that PC.
