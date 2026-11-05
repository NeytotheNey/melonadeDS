# Ideas

## Lua and game features

- **Type weakness/resistance Lua script for Pokémon White 2.** Show a Pokémon's weaknesses and resistances during battle. Needs the battle Pokémon's species and types read from memory.
- **Extend 2D scenes that WideMelon doesn't support.** Duplicate the screen edges and repeat them with a blur, or auto-crop to expose the background for a while.

## Rendering

- **2D rendering on the GPU.**
- **Filtering for 2D assets.** Add texture filtering options.
- **Fix floor texture separation in classic OpenGL** for older Pokémon games like SoulSilver.
- **Controller-style backgrounds using vectors**, so they scale cleanly when the window is resized.
- **Background support for landscape and unfolded mode** on phones.

## Performance

- **Profile the OnePlus 7 Pro.**

## App features

- **Cheat search.** Search memory for values, narrow the results, and turn them into cheats.
- **Game library revamp.** Grid and mosaic views are mocked up behind the view menu; see [plan-launcher.md](plan-launcher.md) for what is left.
- **RetroAchievements metadata.** Answered in [plan-launcher.md](plan-launcher.md): the connect API gives title and icon, box art and credits need the user's Web API key, cached once per game.

## Questions to look into

- Ask Fable for a better understanding of how the CPU works (for the emulator's CPU core and the Lua side).
- **`Failed to allocate memory using ftruncate! (Invalid argument)`** logs once per game load (seen on all 13 launches on 2026-11-02, and before that in the polish pass), and every game runs. It comes from the JIT fastmem setup in the core's `src/ARMJIT_Memory.cpp` (around line 919). On Android the region is made with `ASharedMemory_create`, which already sets the size, and the ftruncate after it likely fails because ashmem doesn't support it. If so it's only noise and the fix is to skip ftruncate there. Check that fastmem is really mapped and in use before calling it harmless.
