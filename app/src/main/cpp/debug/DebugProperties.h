#ifndef DEBUGPROPERTIES_H
#define DEBUGPROPERTIES_H

#ifdef MELONADE_DEBUG_TOOLS

namespace melonDS { class GPU; }

/**
 * Debug builds only: `adb shell setprop debug.melonade.<switch> <value>` before a game starts changes one of the switches below, and an unset or
 * empty value (`adb shell "setprop debug.melonade.<switch> ''"`) means the default.
 *
 * On/off switches (0 or 1; the default is the core's, see GPU.h):
 *   hash2d         log a checksum of the 2D output every frame (GPU::Hash2DLogging, default 0)
 *   defercap       hold display capture lines until line 192 with an accelerated 3D renderer, instead of drawing them at HBlank (GPU::DeferCapture2D)
 *   earlyflush     flush the 3D frame (line 215) and its capture readback (line 0) right away while capture lines are held (GPU::EarlyFlushCapture)
 *   shadowfix      classic OpenGL: clear the shadow mask per line like the software renderer (GPU::ShadowMaskLineClear)
 *   maskdepth      classic OpenGL: DS depth margin for depth-equal shadow masks and shadows (GPU::ShadowMaskDepthTest)
 *   equaldepth     classic OpenGL: DS depth margin for the other depth-equal polygons (GPU::EqualDepthMargin)
 *   computedirect  compute: CPU-sized SortWork and Rasterise dispatches instead of GPU-written indirect ones (GPU::ComputeDirectDispatch)
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
 *
 * Others:
 *   shadowlog N    classic OpenGL: log SHADOWSTATS and SHADOWLOG lines for the first N frames (1 means 300)
 *   capspin N      compute: poll the capture readback's fence for up to N µs (0 to 20000) before mapping it; 0 sleeps in the map
 *   hirescap V     0 or 1 forces upscaled display capture off or on over the setting; `log` also logs a HIRESCAP line once a second;
 *                  `check` also compares the hi-res copy with the native capture every 30 passes
 *   filter2d N     over the Smooth 2D edges setting: 0 off, 1 on, 2 on but stop at the 2x map at every scale (to measure the second pass),
 *                  3 run every pass but write identity coordinates (output must match 0 byte for byte). 1-3 also log a FILTER2D line once a second
 *   vertexid N     1 logs a VERTEXID line once a second on vertex identity between 3D scenes, 2 a line per scene as well, no effect on emulation (GPU3D_VertexId.h)
 *   halfcam N      experiment: in-between 3D scenes from a halfway camera, 1 camera only, 2 plus characters, 3 same as 2 with a HALFCAM cam line per scene and a diag line per second, 4 same as 3 plus billboards following the camera, 5 same as 4 with a HALFCAM pair line per scene; compute renderer only (GPU3D_HalfCamera.h). When set, it replaces the Smooth 30 FPS setting, so 0 forces it off
 *   cpusemu L      pin the emulator thread (and the core threads it creates) to the CPUs in the comma-separated list L (MelonDSAndroidJNI.cpp)
 *   cpus2d L       pin the core's 2D render thread to the CPUs in L (MelonDSAndroidJNI.cpp)
 */
namespace DebugProperties
{

/**
 * Reads debug.melonade.[name] into [value], which must hold PROP_VALUE_MAX bytes, and returns false if it's unset or empty.
 */
bool read(const char* name, char* value);

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
