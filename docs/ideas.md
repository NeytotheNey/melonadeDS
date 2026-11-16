# Ideas

## Lua and game features

- **Extend 2D scenes that WideMelon doesn't support.** Duplicate the screen edges and repeat them with a blur, or auto-crop to expose the background for a while.

## Rendering

- **2D rendering on the GPU.** - WIP
- **Filtering for 2D assets.** Add texture filtering options.
- **Share the blitafterread code between OpenGL and compute.** Both renderers already share `GLCompositor`, but the kept/spare 3D buffer swap and the early capture readback (second pixel buffer, optional fence, valid flag, the drop list) are written twice in `GPU3D_OpenGL.cpp` and `GPU3D_Compute.cpp`. Two small helpers owned by each renderer could hold that logic, with callbacks for what really differs: texture creation, the readback itself, and how the output is bound. Roughly 100–150 lines less, and no behavior change, so check it against the current build with matching pictures and capture data.

## Performance

- **Switch `debug.melonade.*` props without restarting the game.** Each A/B config needs a fresh boot today, because the props are only read at startup. If the core re-read them on a broadcast, a whole A/B could run in one session, and pixel checks could compare the same scene back to back.

## App features

- **Cheat search.** Search memory for values, narrow the results, and turn them into cheats.
- **Game library revamp.** Grid and mosaic views are mocked up behind the view menu; see [plan-launcher.md](plan-launcher.md) for what is left.
- **RetroAchievements metadata.** Answered in [plan-launcher.md](plan-launcher.md): the connect API gives title and icon, box art and credits need the user's Web API key, cached once per game.
