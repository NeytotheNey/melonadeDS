#include <cstdio>
#include <string>
#include "types.h"

#include "GPU3D_OpenGL_shaders.h"
#include "GPU_OpenGL_shaders.h"

using namespace melonDS;
static void w(const char* n, const std::string& s){ FILE* f=fopen(n,"w"); fputs(s.c_str(),f); fclose(f); }
// GLCompositor::PrepareWide: WIDE defined after the #version line
static std::string wide(std::string s){ s.insert(s.find('\n') + 1, "#define WIDE\n"); return s; }
int main(){
  w("ClearVS.vert", kClearVS); w("ClearFS.frag", kClearFS);
  w("ClearBitmapVS.vert", kClearBitmapVS); w("ClearBitmapFS.frag", kClearBitmapFS);
  w("FinalPassVS.vert", kFinalPassVS); w("FinalPassEdgeFS.frag", kFinalPassEdgeFS); w("FinalPassFogFS.frag", kFinalPassFogFS);
  for (int wb=0; wb<2; wb++){ const char* d = wb? "\n#define WBuffer":"";
    w(wb?"RenderW.vert":"RenderZ.vert", std::string(kShaderHeader)+d+kRenderVSCommon+kRenderVS);
    w(wb?"RenderW.frag":"RenderZ.frag", std::string(kShaderHeader)+d+kRenderFSCommon+kRenderFS); }
  // GLCompositor::CompositorFS and HiresCaptureFS: the head, the shared composite, the rest
  std::string compFS = std::string(kCompositorFS_Nearest_Head) + kCompositePixelFS + kCompositorFS_Nearest_Main;
  w("CompositorVS.vert", kCompositorVS); w("CompositorFS_Nearest.frag", compFS);
  w("CompositorWideVS.vert", wide(kCompositorVS)); w("CompositorWideFS_Nearest.frag", wide(compFS));
  w("HiresCaptureVS.vert", kHiresCaptureVS);
  w("HiresCaptureFS.frag", std::string(kHiresCaptureFS_Head) + kCompositePixelFS + kHiresCaptureFS_Main);
  return 0; }
