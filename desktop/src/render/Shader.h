// Ports of the app's Shader.kt (a built program, its attributes and the uniforms the filters may declare) and ShaderFactory.kt (building one
// from a ShaderProgramSource, with COLOR_CORRECTION and other defines). UI thread only, with its GL context current
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PlatformOGL.h"
#include "render/ShaderLibrary.h"

namespace Frontend
{

struct TiltShift;
struct Bloom;
struct DepthOfField;

class Shader
{
public:
    Shader(GLuint program, GLenum textureFiltering);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // GL_NEAREST or GL_LINEAR, what the shader samples its input with
    const GLenum TextureFilter;
    GLint AttribUv = -1, AttribPos = -1, AttribAlpha = -1;
    GLint UniformTex = -1;

    // the program, with its vertex attribute arrays enabled on the bound vertex array
    void Use() const;
    // each one only where the shader declares the uniform; call after Use()
    void SetTextureSize(int width, int height) const;
    void SetTopScreenWidth(int width) const;
    // the top screen's half in DS pixels (192 plus the WideMelon camera's margin on both sides), as frameLayout's v coordinates and rows
    void SetTopScreenHeight(int height) const;
    void SetBottomScreenWide(bool isBottomScreenWide) const;
    void SetTiltShift(const TiltShift& tiltShift) const;
    // the bloom's strength, threshold and radius, the vignette's strength and size, and the glow on unit 1
    void SetBloom(const Bloom& bloom) const;
    // the depth of field's radius at its strength, range and falloff, its focus point as [focusU] and [focusV] of the depth texture (read when
    // it has no focus depth), whether this frame ages a held focus ([focusAgeStep], 0 or 1), and the depth texture on unit 1 and the picked
    // focus on unit 2
    void SetDepthOfField(const DepthOfField& depthOfField, float focusU, float focusV, float focusAgeStep) const;
    // GPU::Comp1x: which screens are at 1x, the frame's scale and theirs, and the coordinate textures on units 1 and 2
    void SetHalvesAt1x(int halvesAt1x, int scale, int halfScale) const;

private:
    GLuint Program;
    GLint UniformTexSize = -1, UniformTopScreenWidth = -1, UniformFrameLayout = -1, UniformBottomScreenWide = -1;
    GLint UniformTiltShift = -1, UniformTiltShiftBoost = -1;
    GLint UniformBloom = -1, UniformVignette = -1, UniformGlowTex = -1;
    GLint UniformDepthOfField = -1, UniformDepthFocus = -1, UniformDepthTex = -1, UniformFocusTex = -1;
    GLint UniformHalvesAt1x = -1, UniformCoordXTex = -1, UniformCoordYTex = -1;
};

// ShaderFactory.fragmentShaderSource: [source]'s fragment shader as it's compiled, COLOR_CORRECTION defined with [colorCorrection], then each of
// [extraDefines], on the line after #version when there's one
std::string FragmentShaderSource(const ShaderProgramSource& source, bool colorCorrection, const std::vector<std::string>& extraDefines = {});

// ShaderFactory.createShaderProgramOrNull: [source] compiled and linked, null (logged, nothing left behind) if it doesn't build
std::unique_ptr<Shader> CreateShaderProgramOrNull(const ShaderProgramSource* source, bool colorCorrection = false,
    const std::vector<std::string>& extraDefines = {});

// ShaderFactory.createScreenShaderProgram: the screen shader, or the no-filter one if it doesn't build ([fallback] set then); null only if that
// doesn't build either, where the app would keep a program that draws nothing
std::unique_ptr<Shader> CreateScreenShaderProgram(const ShaderProgramSource* source, bool colorCorrection, bool& fallback);

}
