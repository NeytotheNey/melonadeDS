// replicates ComputeRenderer::CompileShader's source assembly (GPU3D_Compute.cpp) for scale 1
#include <cstdio>
#include <string>
#include "GPU3D_Compute_shaders.h"
using namespace melonDS;
static void w(const char* n, const std::string& s){ FILE* f=fopen(n,"w"); fputs(s.c_str(),f); fclose(f); }
static std::string assemble(const std::string& source, std::initializer_list<const char*> defines){
  std::string s;
  s += "#version 320 es\n";
  s += "precision highp float;\n";
  s += "precision highp int;\n";
  s += "precision highp usampler2DArray;\n";
  s += "precision highp usampler2D;\n";
  s += "precision highp image2D;\n";
  s += "precision highp uimage2D;\n";
  for (const char* d : defines) { s += "#define "; s += d; s += '\n'; }
  s += "#define ScreenWidth 256";
  s += "\n#define ScreenHeight 192";
  s += "\n#define MaxWorkTiles 12288";
  s += "\n#define TileSize 8";
  s += "\nconst int CoarseTileCountY = 4;";
  s += "\n#define CoarseTileArea 32";
  s += "\n#define ClearCoarseBinMaskLocalSize 64";
  s += ComputeRendererShaders::Common;
  s += source;
  return s;
}
int main(){
  w("DepthBlendZ.comp", assemble(ComputeRendererShaders::DepthBlend, {"DepthBlend", "ZBuffer"}));
  w("DepthBlendW.comp", assemble(ComputeRendererShaders::DepthBlend, {"DepthBlend", "WBuffer"}));
  w("FinalPass.comp", assemble(ComputeRendererShaders::FinalPass, {"FinalPass", "EdgeMarking", "Fog"}));
  w("InterpSpansZ.comp", assemble(ComputeRendererShaders::InterpSpans, {"InterpSpans", "ZBuffer"}));
  w("BinCombined.comp", assemble(ComputeRendererShaders::BinCombined, {"BinCombined"}));
  w("RasteriseTexToon.comp", assemble(ComputeRendererShaders::Rasterise, {"Rasterise", "ZBuffer", "UseTexture", "Toon"}));
  w("RasteriseShadowMask.comp", assemble(ComputeRendererShaders::Rasterise, {"Rasterise", "WBuffer", "ShadowMask"}));
  w("ClearCoarseBinMask.comp", assemble(ComputeRendererShaders::ClearCoarseBinMask, {"ClearCoarseBinMask"}));
  w("ClearIndirectWorkCount.comp", assemble(ComputeRendererShaders::ClearIndirectWorkCount, {"ClearIndirectWorkCount"}));
  w("CalcOffsets.comp", assemble(ComputeRendererShaders::CalcOffsets, {"CalculateWorkOffsets"}));
  w("SortWork.comp", assemble(ComputeRendererShaders::SortWork, {"SortWork"}));
  w("FinalPassAA.comp", assemble(ComputeRendererShaders::FinalPass, {"FinalPass", "AntiAliasing"}));
  return 0; }
