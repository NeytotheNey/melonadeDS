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
struct Shade;
struct Sun;

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
    // the sun shadows' guard band in DS pixels (FrameView::TopGuard): the vignette goes over the top screen as shown, without it
    void SetTopScreenGuard(int guard) const;
    void SetBottomScreenWide(bool isBottomScreenWide) const;
    void SetTiltShift(const TiltShift& tiltShift) const;
    // the bloom's strength, threshold and radius, the vignette's strength and size, the haze's strength, start, falloff and color, the lights'
    // threshold and strength, the glow on unit 1, and the depth texture on unit 1 for the 3D-only and lights bright passes and on unit 2 for
    // the haze
    void SetBloom(const Bloom& bloom) const;
    // the depth of field's radius at its strength, range and falloff, its focus point as [focusU] and [focusV] of the depth texture (read when
    // it has no focus depth), whether this frame ages a held focus ([focusAgeStep], 0 or 1), and the depth texture on unit 1 and the picked
    // focus on unit 2
    void SetDepthOfField(const DepthOfField& depthOfField, float focusU, float focusV, float focusAgeStep) const;
    // the shading's strength, radius, range and tint, and for its last pass the frame's depth texture on unit 1 and the blurred depth on
    // unit 2
    void SetShade(const Shade& shade) const;
    // the sun shadows' values and the depth camera they're marched with: [projection] and [inverse] (its inverse), 16 floats each
    // column-major, and [viewport] (left, top, width, height in DS pixels of the depth texture's top half); and the frame's depth texture on
    // unit 1 and the shadow on unit 2. The direction is normalized here
    void SetSun(const Sun& sun, const float* projection, const float* inverse, const float* viewport) const;
    // GPU::Comp1x: which screens are at 1x, the frame's scale and theirs, and the coordinate textures on units 1 and 2
    void SetHalvesAt1x(int halvesAt1x, int scale, int halfScale) const;

private:
    GLuint Program;
    GLint UniformTexSize = -1, UniformTopScreenWidth = -1, UniformFrameLayout = -1, UniformBottomScreenWide = -1;
    GLint UniformTopScreenGuard = -1;
    GLint UniformTiltShift = -1, UniformTiltShiftBoost = -1;
    GLint UniformBloom = -1, UniformVignette = -1, UniformGlowTex = -1;
    GLint UniformHaze = -1, UniformHazeColor = -1, UniformHazeDepthTex = -1, UniformBloomLights = -1;
    GLint UniformDepthOfField = -1, UniformDepthFocus = -1, UniformDepthTex = -1, UniformFocusTex = -1;
    GLint UniformShade = -1, UniformShadeColor = -1, UniformShadeDepthTex = -1, UniformShadeBlurTex = -1;
    GLint UniformSun = -1, UniformSunDirection = -1, UniformSunSoftness = -1, UniformSunColor = -1, UniformSunProjection = -1;
    GLint UniformSunInverse = -1, UniformSunViewport = -1, UniformSunDepthTex = -1, UniformSunMaskTex = -1, UniformSunNear = -1;
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
