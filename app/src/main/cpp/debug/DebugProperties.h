#ifndef DEBUGPROPERTIES_H
#define DEBUGPROPERTIES_H

#ifdef MELONADE_DEBUG_TOOLS

#include <string>

namespace melonDS { class GPU; }

/**
 * Debug builds only: `adb shell setprop debug.melonade.<switch> <value>` before a game starts changes one of the switches below, and an unset or
 * empty value (`adb shell "setprop debug.melonade.<switch> ''"`) means the default.
 *
 * Without adb, the "Debug switches" setting (Settings → Performance → Diagnostics, debug builds only, DebugSwitches.kt) sets the same switches
 * at game start: `name=value` pairs, the name with or without the debug.melonade. prefix. Each one it sets replaces the property of that name
 * for the game (setOverrides), and the properties it doesn't set are read as before.
 *
 * On/off switches (0 or 1; the default is the core's, see GPU.h):
 *   hash2d         log a checksum of the 2D output every frame (GPU::Hash2DLogging, default 0)
 *   defercap       hold display capture lines until line 192 with an accelerated 3D renderer, instead of drawing them at HBlank (GPU::DeferCapture2D)
 *   earlyflush     flush the 3D frame (line 215) and its capture readback (line 0) right away while capture lines are held (GPU::EarlyFlushCapture)
 *   shadowfix      classic OpenGL: clear the shadow mask per line like the software renderer (GPU::ShadowMaskLineClear)
 *   maskdepth      classic OpenGL: DS depth margin for depth-equal shadow masks and shadows (GPU::ShadowMaskDepthTest)
 *   equaldepth     classic OpenGL: DS depth margin for the other depth-equal polygons (GPU::EqualDepthMargin)
 *   computedirect  compute: CPU-sized SortWork and Rasterise dispatches instead of GPU-written indirect ones (GPU::ComputeDirectDispatch)
 *   computegrow    compute: the tile memory and X span buffers start small (6 of 16 layers, a 16th of the spans) and grow before a frame whose
 *                  polygons could need more, a quarter over what it asked, logging a COMPGROW line each time. 0 gives them their full size at the
 *                  first frame with polygons, the old behavior, for memory A/B runs (GPU::ComputeGrow, default 1). COMPGROW has the ms the
 *                  allocation calls took
 *   computerooms   compute, with computegrow: start those buffers at the rooms the game (cart game code and header CRC) grew to before at
 *                  the same scale, width, tile size and layout, kept in the app's cache dir (compute_rooms.bin, 512 entries at most, least
 *                  recently used dropped, written when a buffer grows), so a grow that always comes happens at the first frame and not
 *                  mid-game. Debug builds log a COMPROOMS line whenever the targets are set up with the key, whether it was found, the rooms
 *                  they start at and the ms of their allocation. 0 starts small as before and doesn't read or write the file
 *                  (GPU::ComputeRooms, default 1)
 *   computewarm    compute: at the first frame after the targets are set up, write every byte of the work buffers once on the GPU, so the
 *                  driver commits their memory under the black screen and not at a game's first heavy render; a grow writes the grown
 *                  buffers the same way. Every build queues it ahead of the render; debug builds also wait for it and log a COMPWARM line
 *                  with its ms and bytes, and COMPGROW's touch. 0 leaves it out. Whatever it's set to, debug builds log a COMPSLOW line
 *                  (20 a run at most) for a 3D render over 30 ms with its CPU time by phase and how many programs it was the first to
 *                  dispatch (GPU::ComputeWarm, default 1)
 *   shadercache    compute: load the 33 programs of a configuration (scale, WideMelon width, computeopt layout) seen before, this run or
 *                  an earlier one, from their binaries in the app's cache dir (compute_programs.bin, 24 MB at most, least recently used
 *                  dropped) instead of compiling them. Debug builds log a COMPSHADER line per batch of compiles with the hits, misses,
 *                  rejected binaries, ms and bytes loaded and saved. 0 compiles as before and doesn't read or write the file
 *                  (GPU::ShaderCache, default 1)
 *   computetimers  compute: log a COMPTIME line once a second with the GPU time of each pass and of the compositor, from GL_EXT_disjoint_timer_query,
 *                  no effect on emulation (GPU::ComputeTimers, GPU3D_ComputeStats.h, default 0)
 *   computepeaks   compute: log a COMPPEAK line once a second with the most work tiles, spans, variants and polygons a frame used, and the room
 *                  the grown buffers have (see computegrow). It reads a count back from the GPU every frame, which slows it down, so not
 *                  together with computetimers (GPU::ComputePeaks, default 0)
 *   computesync    compute: log a COMPSYNC line once a second with the same passes timed on the CPU between glFinish calls, for phones whose
 *                  COMPTIME passes read 0 (Adreno 640). Much slower than normal, and it turns computetimers and computepeaks off
 *                  (GPU::ComputeSync, default 0)
 *   computewhy     compute: log a COMPWHY line once a second with how many frames were drawn or left out as identical to the last one, and
 *                  what kept the drawn ones from being left out: a new polygon list (and whether it was the same scene again), a changed
 *                  register, a changed texture, written texture VRAM, and so on. Each cause is a/b: renders it applied to, and renders where
 *                  it was the only one. The fields are with ComputeRenderer::LogWhy. No effect on emulation (GPU::ComputeWhy, default 0)
 *   blitflush      compute: flush right after the line-192 Blit in frames that hold capture lines (GPU::BlitFlushCapture); with blitafterread on,
 *                  only when the Blit stays at line 192
 *   lateblit       threaded 2D, OpenGL/compute: move the frame-end 2D barrier and the Blit from line 192 to line 215 (GPU::LateBlit)
 *   blitafterread  OpenGL/compute: in frames that hold capture lines, move the Blit (compositor and filtered 2D) from line 192 to right after the 3D
 *                  render at line 215, and the next frame's capture readback from line 0 to between the two (GPU::BlitAfterReadback);
 *                  default 1 in debug builds, 0 is the old path
 *   bnohold        OpenGL/compute: while capture lines are held, draw engine B's part of each line right away and hold only engine A's (GPU::EngineBNoHold); default 1 in debug builds, 0 is the old path
 *   threaded2d     force threaded 2D rendering on or off over the setting
 *   capreads       log a CAPREADS line once a second on how often games read what display capture wrote, no effect on emulation (GPU_CaptureReads.h, default 0)
 *   gpu2dstats     log a GPU2DSTATS line per engine once a second on what a GPU 2D renderer would have to deal with, no effect on emulation (GPU_2DStats.h, default 0)
 *   widedrops      log a WIDEDROPS line once a second on polygons dropped because the polygon or vertex RAM was full, and with WideMelon on, how many of
 *                  those the native view had room for, no effect on emulation (GPU3D::SetWideDropLogging, default 0)
 *   hash3d         log `HASH3D frame=<n> hash=<hex>` per frame: an FNV-1a of the 3D image at native resolution in GetLine's pixel format, numbered
 *                  like HASH2D (n is the HASH2D frame that shows the render, which line 215 of frame n-1 started). OpenGL hashes its 256x192
 *                  downscale, compute the low res image display capture reads, software its color buffer; all three are read late without
 *                  waiting (OpenGL and compute through a ring of pixel buffers, so a frame the ring had no room for logs `dropped=ringfull`).
 *                  It costs a blit and a 192 KB readback a frame on the GPU renderers (GPU::Hash3DLogging, GPU3D_Hash3D.h, default 0)
 *   computedepth   compute: log a COMPDEPTH line once a second on what DepthBlend does per pixel: polygons walked, colored, failing the depth
 *                  test. DepthBlend is rebuilt with counters (shared-memory atomics, one SSBO add per work group), so it runs slower
 *                  (GPU::ComputeDepthStats, GPU3D_GLStats.h, default 0)
 *   computessbo    compute, Mali diagnostics (docs/shelved/compute-on-mali.md): InterpXSpans reads the setup indices from the same buffer
 *                  bound as a std430 storage buffer instead of through the RGBA16UI texture buffer. Recompiles at the next render
 *                  (GPU::ComputeSSBO, default 0)
 *
 * Others:
 *   shadowlog N    classic OpenGL: log SHADOWSTATS and SHADOWLOG lines for the first N frames (1 means 300)
 *   capspin N      compute: poll the capture readback's fence for up to N µs (0 to 20000) before mapping it; 0 sleeps in the map
 *   computepace N  compute: keep the emulator from queueing 3D renders faster than the GPU finishes them. 0 off (the old behavior), 1 waits
 *                  before a render (100 ms at most, polling for the first capspin µs of it) until at most one earlier render is unfinished,
 *                  2 (the default in every build) skips the render instead while two or more are and keeps the last 3D image, and waits like 1
 *                  when a display capture may read the render, halfcam or Smooth 30 FPS is on and the game swaps every other frame (see
 *                  computeopt 64) or the last render is half a second old. Setting it also logs a COMPPACE line once a second (GPU::ComputePace)
 *   computetile N  compute: 8, 16 or 32 forces that tile size at any scale, for A/B runs; anything else is the default (8 px up to 4x, 16 px
 *                  from 5x). A size that can't work for a target falls back to the default with one log line. It costs a second shader
 *                  compile at the first frame (GPU::ComputeTileSize)
 *   computeopt N   compute: which of the changes that cut GPU work without changing a pixel are on, as a sum of bits, for A/B runs and
 *                  to find the one behind a wrong pixel. 223 (the default in every build) is all but 32 and 256, 95 that with the tile memory's
 *                  old three buffers, 479 the default with one tile buffer, 255 and 511 are 223 and 479 with 32 on, 0 the renderer as it
 *                  was. 1 Rasterise skips
 *                  the perspective divide on linear spans with z-buffering, 2 Rasterise reads only the polygon and span words the coverage
 *                  test needs until a pixel is covered, 4 DepthBlend stores the second layer only where the final pass reads it (its color
 *                  with anti-aliasing, its depth and attributes with fog as well), 8 Rasterise drops a dead branch and a multiply by 1/16,
 *                  16 a frame with the same polygons is no longer drawn again just because texture or palette VRAM was written, only when
 *                  that changed a cached texture or the clear bitmap, 32 a frame isn't drawn again either when the game swapped buffers
 *                  but sent exactly the scene of the swap before, with the same registers (compared value by value, COMPWHY's swapsaved; off by default, it costs frame-time spikes),
 *                  64 with halfcam or Smooth 30 FPS on, computepace 2 can skip a render as it does without them, except around an
 *                  in-between frame and while the game swaps every other frame (off: it always waits, as before), 128
 *                  Rasterise stores a pixel's edge flags and coverage in the free bits of its color, so the tile memory is two buffers of
 *                  4 bytes a pixel and not three, 256 (off by default, it measured no faster; nothing without 128) those two are one buffer
 *                  with color and depth side by side; where that's bigger than GL_MAX_SHADER_STORAGE_BLOCK_SIZE at a scale it stays two.
 *                  Debug builds log a COMPTILES line with the buffer count (3, 2 or 1) whenever the targets are built. Turning any of
 *                  1 to 8 or 128 off, or 256 on where one buffer fits, costs a second shader compile at the first frame (GPU::ComputeOpt)
 *   hirescap V     0 or 1 forces upscaled display capture off or on over the setting; `log` also logs a HIRESCAP line once a second;
 *                  `check` also compares the hi-res copy with the native capture every 30 passes
 *   filter2d N     over the Smooth 2D edges setting: 0 off, 1 on, 2 on but stop at the 2x map at every scale (to measure the second pass),
 *                  3 run every pass but write identity coordinates (output must match 0 byte for byte). 1-3 also log a FILTER2D line once a second
 *   gpu2d V        OpenGL/compute, the GPU 2D shadow renderer (GPU_Shadow2D.h, docs/plan-gpu-2d-shadow.md): 0 off (the default), 1 redraws
 *                  only the lines that gain something, `all` every line it can, `log` is 1 plus a GPU2D line once a second, `check` is
 *                  all plus the log and a 1x readback every 30 frames compared word for word with the CPU's planes
 *   gpu2dwide N    with gpu2d: 1 lets every 2D layer draw past the edge into WideMelon's wings (a test mode, default 0)
 *   vertexid N     1 logs a VERTEXID line once a second on vertex identity between 3D scenes, 2 a line per scene as well, no effect on emulation (GPU3D_VertexId.h)
 *   halfcam N      experiment: in-between 3D scenes from a halfway camera, 1 camera only, 2 plus characters, 3 same as 2 with a HALFCAM cam line per scene and a diag line per second, 4 same as 3 plus billboards following the camera, 5 same as 4 with a HALFCAM pair line per scene; compute renderer only (GPU3D_HalfCamera.h). When set, it replaces the Smooth 30 FPS setting, so 0 forces it off
 *   gltimers N     OpenGL: 1 logs a GLTIME line once a second with the GPU µs of each pass per timed render, capture readback and Blit (upload,
 *                  clear, opaque, clear0, trans, edge, fog, capture, comp, compx, then the compositor's parts comp_hd and cx_*, each with an
 *                  _n of the Blits it ran in), from GL_EXT_disjoint_timer_query read some frames late; it
 *                  logs once if the extension is missing or every query reads 0. 2 times the same passes on the CPU between glFinish calls
 *                  instead: much slower, every pass reads longer than it is, so compare its share= list (GPU::GLTimers, GPU3D_GLStats.h)
 *   scenestats N   1 logs a SCENESTATS line once a second on the scenes line 215 renders, from the CPU's polygons, with any renderer: DISP3DCNT
 *                  bits, W or Z, polygon counts by kind and textured or not, and their summed screen area over the screen's (rasterized
 *                  fragments per displayed 3D pixel, the same at any WideMelon width), no effect on emulation (GPU3D_SceneStats.h). 2 also has the OpenGL renderer count with an atomic per fragment
 *                  what its render passes shade and how many pixels polygons cover (a SCENEFRAGS line), which slows it down; without SSBOs in
 *                  fragment shaders it logs that and runs as 1 (GPU::SceneStats). On OpenGL either one also logs a GLTRANS line once a
 *                  second on need-opaque polygons, their texture alpha classes and what gltrans left out, and a GLOPAQUE line (the R1
 *                  count): Z- and W-buffer frames and, in Z frames, the opaque shader's polygons and area by whether its discard can fire
 *                  (GPU3D_GLStats.h)
 *   gltrans N      OpenGL, experiment T1 (docs/plan-gpu-discovery.md): leave out translucent draws that can't change a pixel, a sum of
 *                  bits, meant to keep hash3d the same. 1 with a clear alpha of 0, pass 3 doesn't redraw a need-opaque polygon's opaque part
 *                  (not depth-equal ones), 2 a need-opaque polygon's opaque draw is left out when its texture has no texel at alpha 31,
 *                  its translucent draw (not a shadow's) when none is at 1-30, from a CPU scan of the texture each frame. 4 (T1c) with a
 *                  clear alpha of 0 and GL_EXT_shader_framebuffer_fetch, no clear0 pass: pass 3's translucent draws read the color target
 *                  and don't blend where its alpha is 0 (without the extension, or desktop, nothing changes). A/B it as 3 against 7.
 *                  3 is the default, in release builds too; 0 draws as before (GPU::GLTransSkip)
 *   gledgeskip N   OpenGL, experiment E2 (docs/plan-gpu-discovery.md): 1 leaves out the final pass's edge-marking draw, and the whole
 *                  pass with its depth detach when fog is off too. Exact while nothing writes the edge flag (edge marking, E1, is
 *                  open), so hash3d stays the same. 1 is the default since 11-12, in release builds too; 0 draws as before (GPU::GLEdgeSkip)
 *   glclear N      OpenGL, experiment D1 (docs/plan-gpu-discovery.md), a sum of bits: 1 the plain clear (not the clear bitmap) is a
 *                  glClearBuffer per target instead of a full-screen quad, same values; 2 the attr and depth/stencil targets are
 *                  invalidated after the frame, as nothing reads them before the next clear. 3 is the default since 11-12, in release
 *                  builds too; 0 draws as before (GPU::GLClearFast)
 *   glnodiscard N  OpenGL, experiment R1 step 1 (docs/plan-gpu-discovery.md): 1 draws the opaque pass's polygons that can't discard a
 *                  fragment (polygon alpha 31, not a shadow, and untextured, decal, or a palette texture without a transparent color 0)
 *                  with a program built without discard, so the GPU can test depth early. Z-buffer frames only. Exact, so hash3d stays
 *                  the same; with scenestats the GLOPAQUE line counts those polygons and the extra draws and program switches.
 *                  1 is the default since 11-13, in release builds too; 0 draws as before (GPU::GLNoDiscard)
 *   compclear N    OpenGL and compute, experiment K2a (docs/plan-gpu-discovery.md): the compositor's main draw starts without the
 *                  output's old contents, so a tiled GPU needn't load them first. 1 clears the whole output to 0, 2 invalidates it and
 *                  clears only the padding rows between the screens. Both keep the output the same. 0, the default, draws as before
 *                  (GPU::CompClear)
 *   comp1x N       OpenGL and compute, experiment K2c (docs/plan-gpu-discovery.md): a screen with no 3D, 3D or held wings or hi-res capture
 *                  lines is composited at 1x into a corner of its half of the frame, which the screen shader shows nearest-upscaled. Only
 *                  with the "None" filter, no deband and no Lua tilt-shift on every surface (otherwise both screens are drawn in full), not
 *                  with filtered 2D or GPU 2D lines. 1 in a wide view only (pixel-exact), 2 the 256 view too (its mediump full draw rounds
 *                  differently on fp16 GPUs), 3 checks: the full draw as usual, and every 30 frames a 1x draw compared with it. With
 *                  scenestats or gltimers on (always in 3) a COMP1X line once a second counts the frames per screen at 1x and why the
 *                  others weren't. 0 draws as before; 1 is the default in every build (GPU::Comp1x). With filtered 2D (K3) such a screen
 *                  is drawn at the filter map's scale instead of 1x (4x at 8x, 12x and 16x; 2x at 4x and 6x with filter2d 2), in either
 *                  view in mode 1 too; at other scales, and in filter2d 3's 256 view, both screens are drawn in full
 *   filter2drows N OpenGL and compute, experiment K3 (docs/plan-gpu-discovery.md): with filtered 2D, a frame whose keys changed on some
 *                  lines only redraws the map rows those lines can reach. 0 redraws the whole map as before, 2 checks: every 30th partial
 *                  redraw is read back and compared with a full one (the FILTER2D line's rowchecks and rowmismatches). 1 is the default in
 *                  every build (GPU::Filter2DRows)
 *   outcap N       OpenGL and compute, experiment K4 (docs/plan-gpu-discovery.md): 1 caps the compositor's output, and so the frame,
 *                  at the scale the screens are shown at, while the 3D keeps rendering at the internal resolution: the output is the
 *                  internal scale over d (d = 2-4 dividing it, so 12x gives 6x, 4x or 3x), and each output pixel is made from the
 *                  d x d 3D pixels under it (see outcap3d), in the 256 view and in a WideMelon one. Hi-res capture's copies are at the
 *                  output's scale too, made from the 3D at the full scale the same way. The need is the largest ceil(screen size /
 *                  native size) over the screens of every surface, applied once it holds for 30 frames. Not with filtered 2D or the
 *                  GPU 2D shadow yet (those keep the full scale). 1 is the default in every build, 0 as before (GPU::OutputCap). A
 *                  need of 0 (unknown, as in the desktop build) is no cap. OUTCAP lines, only on a change: each surface's need and
 *                  the max sent (melonade-debug, DSRenderer and FrameRenderCoordinator), the need pending and accepted after the 30 frames (melonade-debug), and the
 *                  output scale with why it isn't capped (melonade, GLCompositor::LogOutputCap)
 *   outcapneed N   with outcap 1: cap by this need (1-16) instead of the screens' sizes, for A/Bs. 0, the default, uses the screens'
 *                  (GPU::OutputCapNeed)
 *   outcap3d N     with outcap 1 capping: how an output pixel is made from the 3D pixels under it. 0 is the exact supersample, a
 *                  composite per 3D pixel, averaged: exactly a box filter of the full-scale frame. 1 and 2 are cheaper and inexact
 *                  (a few levels off on 3D pixels only), one composite per output pixel: 1 on the plain average of their color and
 *                  alpha from bilinear taps (where the alphas differ, a 3D edge over 2D, transparent pixels' color mixes in), 2 on
 *                  their alpha-weighted color and averaged alpha from a texelFetch each. Both are also off, by more, where a 2D
 *                  layer blends additively over textured 3D (3D on bottom with EVA + EVB above 16): the blend clamps, and clamping
 *                  the average isn't averaging the clamped sub-pixels. 3, the default, reads the alphas first (one textureGather
 *                  at a ratio of 2, four at 3 and 4) and does 1 where they're all the same and 0 where they differ or the blend
 *                  can clamp, so edges and those blends are exact and only same-alpha blocks are off by a color average's
 *                  rounding. Hi-res capture follows the same mode: in 0 each 3D pixel is captured and the colors averaged, with the
 *                  alpha bit set where at least half have it, in 1 and 2 one capture on the averaged 3D, in 3 either by the alphas
 *                  and the blend. The 2D,
 *                  brightness and what's excluded stay as in 0 (GPU::OutputCap3D)
 *   computecheck N compute, Mali diagnostics (docs/shelved/compute-on-mali.md): every Nth render with polygons waits for the GPU, reads
 *                  back binning's buffers, the WorkDescs and the X spans, and the setup indices through the texture buffer (a small
 *                  extra program), and logs a COMPCHECK line with counts of what's inconsistent (work total vs the CPU bound and
 *                  the mask bits, work tiles off screen, past the polygon count or outside the polygon's box, masks vs WorkDescs,
 *                  the sorted list, X spans outside their polygon, texture buffer texels vs the CPU's, and countmismatch: one
 *                  for each of maskbits, realsum and offsetsum that isn't the written total), then up to 3 lines of
 *                  cases for each count over 0. Slow on the frames it checks. 0 is off (the default), a value that isn't a
 *                  number over 0 is 1 (GPU::ComputeCheck, ComputeRenderer::RunCheck). Every Compute start also logs one COMPGL
 *                  line with the GL vendor, renderer, versions and GL_MAX_TEXTURE_BUFFER_SIZE
 *   computebin N   compute, Mali diagnostics: BinCombined experiments. 1 only the first 32 invocations test a polygon for the shared
 *                  coarse mask (no shift of 32 or more), 2 no shared mask or barrier: every invocation tests all 32 polygons of
 *                  its group for its own tile, 3 is 2 with the WorkDescs packed with plain shifts instead of bitfieldInsert. 0 as
 *                  before, the default. Recompiles at the next render (GPU::ComputeBin)
 *   computecalc N  compute: 1 brings back CalcOffsets' old atomic sorted offsets (an atomicAdd on VariantWorkCount[1].w that Mali's
 *                  driver adds to [0].w, doubling the work total), for A/B runs. 0, the default, hands them out as a running sum
 *                  in one invocation. Recompiles at the next render (GPU::ComputeCalc)
 *   computeflat N  compute: 0 declares binning's VariantWorkCount counters as uvec4s again, the Mali control: that driver applies an
 *                  atomic on a uvec4 element's component to element 0, so the per-variant work counts all land in variant 0.
 *                  1, the default in every build, has them as a flat uint array with the same bytes. Recompiles at the next
 *                  render (GPU::ComputeFlat)
 *   cpusemu L      pin the emulator thread (and the core threads it creates) to the CPUs in the comma-separated list L (MelonDSAndroidJNI.cpp)
 *   cpus2d L       pin the core's 2D render thread to the CPUs in L (MelonDSAndroidJNI.cpp)
 */
namespace DebugProperties
{

/**
 * Reads debug.melonade.[name] into [value], which must hold PROP_VALUE_MAX bytes, and returns false if it's unset or empty. A switch the app
 * set (setOverrides) is read instead of the property. Every switch found is remembered for describeActive.
 */
bool read(const char* name, char* value);

/**
 * The app's switches for the next game, as "name=value" lines with the names without the prefix (empty for none). Replaces the last ones and
 * forgets which switches were found.
 */
void setOverrides(const std::string& text);

/**
 * The switches read since the last setOverrides that had a value, as "name=value" separated by spaces, each marked "(prop)" when it came from a
 * property, "(app, over prop X)" when the app's replaced a property that was set too, and then the app's switches nothing read (yet) as "unread:".
 */
std::string describeActive();

/**
 * The frontend switches, -1 when the property isn't set.
 */
struct Overrides
{
    int threaded2d = -1;
    int hiresCapture = -1;
    int filter2d = -1;
    int halfCamera = -1;
};

/**
 * Applies every switch that targets the core to [gpu], logs a summary line and returns the frontend switches.
 */
Overrides apply(melonDS::GPU& gpu);

}

#endif

#endif //DEBUGPROPERTIES_H
