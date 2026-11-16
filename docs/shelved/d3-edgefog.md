# D3: one edge+fog final pass (shelved 11-15)

Experiment D3 from `docs/plan-gpu-discovery.md`. It draws wrong on Adreno, and Mali can't run it, so it was shelved instead of committed. Numbers are in PERF-NOTES "D3".

## What it did

A debug switch `debug.melonade.glfinalpass` (`GPU::GLFinalPass`, default 0). With bit 0 set, when edge marking and fog are both on and the GPU has `GL_EXT_blend_func_extended`, one draw does both. The edge FS gets an `EdgeFog` build with the fog density code and a second output. The blend is `glBlendFuncSeparate(SRC1_COLOR, ONE_MINUS_SRC1_COLOR, SRC1_ALPHA, ONE_MINUS_SRC1_ALPHA)`, so each pixel gets the edge pass's or the fog pass's blend shape. Draw buffers are cut to the color target for that draw. If the extension is missing or the build fails, it draws the two passes as before. A `GLFINALPASS:` log line says whether the extension is there.

## Why it was shelved

- **The OnePlus (Adreno 640) draws it wrong.** With the switch on, the top screen loses its 3D geometry (buildings, player, fog), and GLTIME's opaque pass drops from ~2.5 ms to ~7 µs. That's stable over 4 runs, with scenestats on and off, and there are no GL errors. The desktop (RTX 5090) drew it right. Likely suspect, not proven: the dual-source blend state left set after the combined draw (the review found the blend func isn't restored, only the draw buffers) breaks the next frame's opaque pass on Adreno.
- **The Pixel 8 (Mali) has no `GL_EXT_blend_func_extended`,** so it always takes the two-pass path.
- **It isn't exact.** About 0.1% of pixels are one 6-bit step off in White 2's town: the ones both edge-marked (antialiasing's 0.5 alpha) and fogged. Two passes round the edged color to 8 bits before fog, and one blend can't. Phone blend precision may add more.
- **The depth detach can't go.** Edge marking reads neighbors' depth, so depth has to be a texture either way. On the OnePlus with edge marking off, fog alone is 1.25 ms, and edge+fog with it on is ~2.5 ms. So only ~0.26 ms of E1's edge cost was the detach moving between passes; the rest is the edge pass's own work.
- Desktop GLTIME: 8x 28.7 → 37.5 µs (slower), 16x 84.6 → 80.7 µs.

Untried alternative: a framebuffer-fetch version (both phones have fetch), doing both blends in the shader. It would likely be a step off on some pixels, like T1c, and T1c's fetch pass cost about as much as the pass it removed.

## To bring it back

Apply on core `2e98367c` and app `58ed79d8` (both checked to apply on 11-15):

```bash
git -C melonDS-android-lib apply ../docs/shelved/d3-edgefog-core.patch
```

```bash
git apply docs/shelved/d3-edgefog-app.patch
```

The core patch touches `GPU.h`, `GPU3D_OpenGL.cpp/.h` and `GPU3D_OpenGL_shaders.h`. The app patch touches `DebugProperties.cpp/.h`, `desktop/src/main.cpp` and `docs/debug-switches.md`. Fix the review's finding first: put the blend func back (or disable blend) right after the combined draw, next to the `glDrawBuffers` restore, then check whether the Adreno bug goes away. Phone logs: `perf_logs/2026-11-15-d3/` (`oneplus/cmp_p0_p1.png` shows the bug).
