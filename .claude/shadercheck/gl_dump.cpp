#include <cstdio>
#include <string>
#include "types.h"

#include "GPU3D_OpenGL_shaders.h"
#include "GPU_OpenGL_shaders.h"

using namespace melonDS;
static void w(const char* n, const std::string& s){ FILE* f=fopen(n,"w"); fputs(s.c_str(),f); fclose(f); }
int main(){
  w("ClearVS.vert", kClearVS); w("ClearFS.frag", kClearFS);
  w("ClearBitmapVS.vert", kClearBitmapVS); w("ClearBitmapFS.frag", kClearBitmapFS);
  w("FinalPassVS.vert", kFinalPassVS); w("FinalPassEdgeFS.frag", kFinalPassEdgeFS); w("FinalPassFogFS.frag", kFinalPassFogFS);
  for (int wb=0; wb<2; wb++){ const char* d = wb? "\n#define WBuffer":"";
    w(wb?"RenderW.vert":"RenderZ.vert", std::string(kShaderHeader)+d+kRenderVSCommon+kRenderVS);
    w(wb?"RenderW.frag":"RenderZ.frag", std::string(kShaderHeader)+d+kRenderFSCommon+kRenderFS); }
  w("CompositorVS.vert", kCompositorVS); w("CompositorFS_Nearest.frag", kCompositorFS_Nearest);
  return 0; }
