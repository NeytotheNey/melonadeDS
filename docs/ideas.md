# Ideas

## Lua and game features

- **Extend 2D scenes that WideMelon doesn't support.** Duplicate the screen edges and repeat them with a blur, or auto-crop to expose the background for a while.

## Rendering

- **2D rendering on the GPU, with filtering for 2D assets.** WIP in [plan-gpu-2d.md](plan-gpu-2d.md) and [plan-gpu-2d-shadow.md](plan-gpu-2d-shadow.md); the "Smooth 2D edges" and "Sharper rotated 2D" settings are in, and shadow increments 1, 2, 4 and 5 are merged.
- **Share the blitafterread code between OpenGL and compute.** Both renderers already share `GLCompositor`, but the kept/spare 3D buffer swap and the early capture readback (second pixel buffer, optional fence, valid flag, the drop list) are written twice in `GPU3D_OpenGL.cpp` and `GPU3D_Compute.cpp`. Two small helpers owned by each renderer could hold that logic, with callbacks for what really differs: texture creation, the readback itself, and how the output is bound. Roughly 100–150 lines less, and no behavior change, so check it against the current build with matching pictures and capture data.

## Performance

- **Switch `debug.melonade.*` props without restarting the game.** Each A/B config needs a fresh boot today, because the props are only read at startup. If the core re-read them on a broadcast, a whole A/B could run in one session, and pixel checks could compare the same scene back to back.

## App features

- **Cheat search.** Search memory for values, narrow the results, and turn them into cheats.
- **Custom folder for save backups.** Like PokeDaisy's `CHANGE FOLDER` / `USE DEFAULT` under Settings > `FOLDERS` > `SAVE BACKUPS`: one folder the user picks for every game's backups, instead of a `save-backups` folder next to each save. Backups that already exist stay where they are, and the restore list still finds them. Two games with the same `.sav` name from different folders would collide in a shared folder, so the names need something extra, like the save folder or a ROM hash. Builds on `SaveBackupManager`.
- **Game library revamp and RetroAchievements metadata.** Built (grid and mosaic views, a metadata sheet, art from the user's Web API key); see [plan-launcher.md](plan-launcher.md) for the phone checks and what's left.

## Bugs to look at

- **Layout background missing behind the right control panel (Fold).** Seen 11-18 on the Fold's inner screen in the landscape layout with the bottom screen between two control panels: the left panel shows the carbon-fiber background, the right one (X/Y/A/B, R, Start/Select, settings, fast forward) is plain black. Same on OpenGL and Compute, so it's the app's layout drawing, not the renderer. Start with how a layout variant assigns its background image to each region.
