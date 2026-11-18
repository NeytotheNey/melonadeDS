// Writes the core's GL and compute shaders to files, assembled the way the renderers do at scale 1
#include <cstdio>
#include <initializer_list>
#include <string>
#include "types.h"
#include "WideMelon.h"

#include "GPU3D_OpenGL_shaders.h"
#include "GPU_OpenGL_shaders.h"
#include "GPU3D_Compute_shaders.h"

using namespace melonDS;
static void w(const std::string& n, const std::string& s){ FILE* f=fopen(n.c_str(),"w"); fputs(s.c_str(),f); fclose(f); }
// GLCompositor::PrepareWide: WIDE defined after the #version line
static std::string define(std::string s, const char* defines){ s.insert(s.find('\n') + 1, defines); return s; }
static std::string wide(std::string s){ return define(std::move(s), "#define WIDE\n"); }

static void dumpGL(){
  w("ClearVS.vert", kClearVS); w("ClearFS.frag", kClearFS);
  w("ClearBitmapVS.vert", kClearBitmapVS); w("ClearBitmapFS.frag", kClearBitmapFS);
  w("FinalPassVS.vert", kFinalPassVS); w("FinalPassEdgeFS.frag", kFinalPassEdgeFS); w("FinalPassFogFS.frag", kFinalPassFogFS);
  for (int wb=0; wb<2; wb++){ const char* d = wb? "\n#define WBuffer":"";
    w(wb?"RenderW.vert":"RenderZ.vert", std::string(kShaderHeader)+d+kRenderVSCommon+kRenderVS);
    w(wb?"RenderW.frag":"RenderZ.frag", std::string(kShaderHeader)+d+kRenderFSCommon+kRenderFS); }
  // GLCompositor::CompositorFS and HiresCaptureFS: the head, the shared composite, the rest
  std::string compFS = std::string(kCompositorFS_Nearest_Head) + kCompositePixelFS + kCompositorFS_Nearest_Main + kCompositorFS_Nearest_Entry;
  w("CompositorVS.vert", kCompositorVS); w("CompositorFS_Nearest.frag", compFS);
  w("CompositorWideVS.vert", wide(kCompositorVS)); w("CompositorWideFS_Nearest.frag", wide(compFS));
  w("HiresCaptureVS.vert", kHiresCaptureVS);
  w("HiresCaptureFS.frag", std::string(kHiresCaptureFS_Head) + kCompositePixelFS + kHiresCaptureFS_Main);
  // GLCompositor::PrepareFilter2DShaders: the pre-pass programs, and the compositor with FILTER2D
  w("Filter2DKeyFS.frag", kFilter2DKeyFS); w("Filter2DMmpxFS.frag", kFilter2DMmpxFS);
  w("CompositorFilter2DFS.frag", define(compFS, "#define FILTER2D\n"));
  w("CompositorWideFilter2DFS.frag", define(compFS, "#define WIDE\n#define FILTER2D\n"));
  // GLCompositor::Shadow2DFS and Shadow2DObjFS: the GPU 2D shadow programs (the composite in the middle and wide, the planes pass in the 256 view and wide, the affine composite in the middle and wide) and the OBJ pass
  std::string shadowFS = std::string(kCompositorFS_Nearest_Head) + kCompositePixelFS + kCompositorFS_Nearest_Main + kShadow2DMemFS + kShadow2DFS_Main;
  w("Shadow2DVS.vert", kShadow2DVS);
  w("Shadow2DFS.frag", define(shadowFS, "#define SHADOW2D\n"));
  w("Shadow2DWideFS.frag", define(shadowFS, "#define WIDE\n#define SHADOW2D\n"));
  w("Shadow2DPlanesFS.frag", define(shadowFS, "#define SHADOW2D\n#define SHADOW2D_PLANES\n"));
  w("Shadow2DPlanesWideFS.frag", define(shadowFS, "#define WIDE\n#define SHADOW2D\n#define SHADOW2D_PLANES\n"));
  w("Shadow2DAffineFS.frag", define(shadowFS, "#define SHADOW2D\n#define SHADOW2D_AFFINE\n"));
  w("Shadow2DAffineWideFS.frag", define(shadowFS, "#define WIDE\n#define SHADOW2D\n#define SHADOW2D_AFFINE\n"));
  w("Shadow2DObjVS.vert", kShadow2DObjVS);
  w("Shadow2DObjFS.frag", std::string(kShadow2DObjFS_Head) + kShadow2DMemFS + kShadow2DObjFS_Main);
}

// ComputeRenderer::CompileShader's assembly (compute_assembly.inc, cut from GPU3D_Compute.cpp by check.sh) with the scale 1 settings
static void dumpCompute(const std::string& source, const std::initializer_list<const char*>& defines){
  const int ScreenWidth = 256, ScreenHeight = 192, MaxWorkTiles = 12288, TileSize = 8;
  const int CoarseTileCountY = 4, CoarseTileArea = 32, ClearCoarseBinMaskLocalSize = 64;
  const int TargetWidth = WideMelon::NativeWidth, ScaleFactor = 1;
  std::string shaderFileName, shaderName, shaderSource;
  int i = 0;
#include "compute_assembly.inc"
  w(shaderFileName + ".comp", shaderSource);
}

int main(){
  dumpGL();
  // Every CompileShader call of ComputeRenderer::ShaderCompileStep (compute_shaders.inc, cut by check.sh)
#define COMPILE(source, ...) dumpCompute(source, __VA_ARGS__);
#include "compute_shaders.inc"
#undef COMPILE
  return 0; }
