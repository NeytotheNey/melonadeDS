# R2: skip `gl_FragDepth` where it's exact (shelved 11-11)

Experiment R2 from `docs/plan-gpu-discovery.md`. It works and is exact, but no game we tried has enough polygons it applies to, so it was shelved instead of committed. Numbers are in PERF-NOTES "R2".

## What it did

A debug switch `debug.melonade.gldepth` (`GPU::GLDepth`, default 0):

- **1 (exact):** in W-buffer frames, an opaque polygon whose used vertices all have the same `z<<zshift` is drawn with a program variant (`RenderShader[4]`, `NoFragDepth`) that doesn't write `gl_FragDepth`. The vertex shader is unchanged (z = 0), and `glDepthRangef(d,d)` makes the window depth exactly the `d` the shader would have written, bias included. Batches split on path and depth. The range goes back to (0,1) after the opaque pass.
- **2 (approximate, diagnostic only):** also WatermelonDS's rule (`requiresWBufferFragmentDepth` in its `GPU3D_Vulkan.cpp`): keep `gl_FragDepth` only for textured, modulate, alpha-31, repeat/flip polygons whose W varies, and use linear hardware depth for the rest. That isn't exact where W varies.
- With `scenestats`, a GLDEPTH line counts polygons, area and draws per path.

## Why it was shelved

- White 2: 0 qualifying polygons under either rule. Dark Dawn's title on the Fold: none for rule 1, GPU cycles flat, HASH3D matched every frame. Other games on the PC: under 2% of opaque area.
- The exact way to get early-Z for perspective polygons, `GL_EXT_conservative_depth`, isn't listed on the Fold, OnePlus or Pixel 8.
- A fresh review found no bugs. Two open exactness questions only a phone A/B with real qualifying polygons could settle: whether the GPU returns a constant varying exactly, and whether rasterized and shader depth round to 24 bits the same way.

## To bring it back

Apply on core `4ae8d6ea` and app `77fdfdf2` (both checked to apply on 11-11):

```bash
git -C melonDS-android-lib apply ../docs/shelved/r2-fragdepth-core.patch
```

```bash
git apply docs/shelved/r2-fragdepth-app.patch
```

The core patch touches `GPU.h`, `GPU3D_OpenGL.cpp/.h`, `GPU3D_OpenGL_shaders.h` and `GPU3D_GLStats.h`. The app patch touches `DebugProperties.cpp/.h`, `desktop/src/main.cpp` and `docs/debug-switches.md`. Later changes to the GL renderer may need the patch fixed up by hand. Fold logs and scripts: `perf_logs/2026-11-11-r2-fold/`.
