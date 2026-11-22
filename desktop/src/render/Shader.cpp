#include "render/Shader.h"

#include <cstdio>

#include "render/PresentFrame.h"

namespace Frontend
{

namespace
{

// the app's SCREEN_GAP and SCREEN_HEIGHT (domain/model/NDS.kt)
constexpr int SCREEN_GAP = 2;
constexpr int SCREEN_HEIGHT = 192;

GLuint CompileShader(GLenum type, const std::string& code, const std::string& name)
{
    GLuint shader = glCreateShader(type);
    if (!shader)
        return 0;
    const char* text = code.c_str();
    GLint length = (GLint)code.size();
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        char log[2048] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "shader %s (%s) didn't compile: %s\n", name.c_str(), type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint LinkProgram(GLuint vertex, GLuint fragment, const std::string& name)
{
    GLuint program = glCreateProgram();
    if (!program)
        return 0;
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        char log[2048] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        fprintf(stderr, "shader %s didn't link: %s\n", name.c_str(), log);
        glDeleteProgram(program);
        return 0;
    }
    // the program keeps them; they go with it
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    return program;
}

std::string AddDefines(const std::string& code, const std::vector<std::string>& names)
{
    if (names.empty())
        return code;
    std::string defines;
    for (const std::string& name : names)
        defines += "#define " + name + "\n";
    // Nothing but comments and whitespace can come before #version, so the defines go on the line after it
    if (code.rfind("#version", 0) == 0)
    {
        size_t versionLineEnd = code.find('\n');
        if (versionLineEnd != std::string::npos)
            return code.substr(0, versionLineEnd + 1) + defines + code.substr(versionLineEnd + 1);
    }
    return defines + code;
}

// The one change the app's shaders get here. ShaderProgramSource's PRECISION_HEADER picks highp with #ifdef GL_FRAGMENT_PRECISION_HIGH, which
// phones define in GLSL ES 1.00 fragment shaders; the PC's ES driver (NVIDIA's, at least) doesn't, so those shaders ran at mediump, which it
// runs as 16-bit float (2xBR's 65536-weighted color compares overflowed and it never blended). Every GLES 3 fragment shader has highp, so
// the block becomes the highp line the phones take. Matched as exported; a header that changed in the app is left alone and logged
std::string ForDesktop(std::string code, const std::string& name)
{
    static const std::string PRECISION_HEADER = "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
                                                "precision highp float;\n"
                                                "#else\n"
                                                "precision mediump float;\n"
                                                "#endif\n";
    size_t at = code.find(PRECISION_HEADER);
    if (at != std::string::npos)
        code.replace(at, PRECISION_HEADER.size(), "precision highp float;\n");
    else if (code.find("GL_FRAGMENT_PRECISION_HIGH") != std::string::npos)
        fprintf(stderr, "shader %s: its precision header isn't the one the desktop turns into highp (render/Shader.cpp)\n", name.c_str());
    return code;
}

}

Shader::Shader(GLuint program, GLenum textureFiltering)
    : TextureFilter(textureFiltering), Program(program)
{
    glUseProgram(Program);
    AttribUv = glGetAttribLocation(Program, "vUV");
    AttribPos = glGetAttribLocation(Program, "vPos");
    AttribAlpha = glGetAttribLocation(Program, "vAlpha");
    UniformTex = glGetUniformLocation(Program, "tex");
    UniformTexSize = glGetUniformLocation(Program, "texSize");
    UniformTopScreenWidth = glGetUniformLocation(Program, "topScreenWidth");
    UniformFrameLayout = glGetUniformLocation(Program, "frameLayout");
    UniformBottomScreenWide = glGetUniformLocation(Program, "bottomScreenWide");
    UniformTiltShift = glGetUniformLocation(Program, "tiltShift");
    UniformTiltShiftBoost = glGetUniformLocation(Program, "tiltShiftBoost");
    UniformBloom = glGetUniformLocation(Program, "bloom");
    UniformVignette = glGetUniformLocation(Program, "vignette");
    UniformGlowTex = glGetUniformLocation(Program, "glowTex");
    UniformDepthOfField = glGetUniformLocation(Program, "depthOfField");
    UniformDepthFocus = glGetUniformLocation(Program, "depthFocus");
    UniformDepthTex = glGetUniformLocation(Program, "depthTex");
    UniformHalvesAt1x = glGetUniformLocation(Program, "halvesAt1x");
    UniformCoordXTex = glGetUniformLocation(Program, "coordXTex");
    UniformCoordYTex = glGetUniformLocation(Program, "coordYTex");
    glUseProgram(0);
}

Shader::~Shader()
{
    glDeleteProgram(Program);
}

void Shader::Use() const
{
    glUseProgram(Program);
    // only the attributes the program has: -1 is GL_INVALID_VALUE here, where the app enables it anyway
    if (AttribUv >= 0)
        glEnableVertexAttribArray(AttribUv);
    if (AttribPos >= 0)
        glEnableVertexAttribArray(AttribPos);
    if (AttribAlpha >= 0)
        glEnableVertexAttribArray(AttribAlpha);
}

void Shader::SetTextureSize(int width, int height) const
{
    if (UniformTexSize >= 0)
        glUniform2f(UniformTexSize, (float)width, (float)height);
}

void Shader::SetTopScreenWidth(int width) const
{
    if (UniformTopScreenWidth >= 0)
        glUniform1f(UniformTopScreenWidth, (float)width);
}

void Shader::SetTopScreenHeight(int height) const
{
    if (UniformFrameLayout >= 0)
    {
        // As v coordinates: the top screen's half's end, the bottom screen's start and the middle of the gap between, then the frame's rows.
        // Worked out in float like the constants they replace (192 / 386, 194 / 386, 0.5 and 386 without the camera's margin)
        float frameRows = (float)(height + SCREEN_GAP + SCREEN_HEIGHT);
        glUniform4f(UniformFrameLayout, height / frameRows, (height + SCREEN_GAP) / frameRows, (height + SCREEN_GAP / 2) / frameRows, frameRows);
    }
}

void Shader::SetBottomScreenWide(bool isBottomScreenWide) const
{
    if (UniformBottomScreenWide >= 0)
        glUniform1f(UniformBottomScreenWide, isBottomScreenWide ? 1.f : 0.f);
}

void Shader::SetTiltShift(const TiltShift& tiltShift) const
{
    if (UniformTiltShift >= 0)
        glUniform4f(UniformTiltShift, tiltShift.BandTop(), tiltShift.BandBottom(), tiltShift.TopRadius(), tiltShift.BottomRadius());
    if (UniformTiltShiftBoost >= 0)
        glUniform2f(UniformTiltShiftBoost, tiltShift.AppliedSaturation(), tiltShift.AppliedContrast());
}

void Shader::SetBloom(const Bloom& bloom) const
{
    if (UniformBloom >= 0)
        glUniform3f(UniformBloom, bloom.Strength, bloom.Threshold, bloom.Radius);
    if (UniformVignette >= 0)
        glUniform2f(UniformVignette, bloom.Vignette, bloom.VignetteSize);
    if (UniformGlowTex >= 0)
        glUniform1i(UniformGlowTex, 1);
}

void Shader::SetDepthOfField(const DepthOfField& depthOfField, float focusU, float focusV) const
{
    if (UniformDepthOfField >= 0)
        glUniform3f(UniformDepthOfField, depthOfField.AppliedRadius(), depthOfField.Range, depthOfField.Falloff);
    if (UniformDepthFocus >= 0)
        glUniform3f(UniformDepthFocus, depthOfField.FocusDepth, focusU, focusV);
    if (UniformDepthTex >= 0)
        glUniform1i(UniformDepthTex, 1);
}

void Shader::SetHalvesAt1x(int halvesAt1x, int scale, int halfScale) const
{
    if (UniformHalvesAt1x >= 0)
        glUniform3i(UniformHalvesAt1x, halvesAt1x, scale, halfScale);
    if (UniformCoordXTex >= 0)
        glUniform1i(UniformCoordXTex, 1);
    if (UniformCoordYTex >= 0)
        glUniform1i(UniformCoordYTex, 2);
}

std::string FragmentShaderSource(const ShaderProgramSource& source, bool colorCorrection, const std::vector<std::string>& extraDefines)
{
    std::vector<std::string> names;
    if (colorCorrection)
        names.push_back("COLOR_CORRECTION");
    names.insert(names.end(), extraDefines.begin(), extraDefines.end());
    return AddDefines(source.FragmentShaderSource, names);
}

std::unique_ptr<Shader> CreateShaderProgramOrNull(const ShaderProgramSource* source, bool colorCorrection, const std::vector<std::string>& extraDefines)
{
    if (!source)
        return nullptr;
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, source->VertexShaderSource, source->Name);
    GLuint fragment = vertex ? CompileShader(GL_FRAGMENT_SHADER, ForDesktop(FragmentShaderSource(*source, colorCorrection, extraDefines), source->Name),
        source->Name) : 0;
    GLuint program = vertex && fragment ? LinkProgram(vertex, fragment, source->Name) : 0;
    // deleting 0 does nothing; a linked program keeps its own
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!program)
        return nullptr;
    return std::make_unique<Shader>(program, source->Filtering == TextureFiltering::Linear ? GL_LINEAR : GL_NEAREST);
}

std::unique_ptr<Shader> CreateScreenShaderProgram(const ShaderProgramSource* source, bool colorCorrection, bool& fallback)
{
    fallback = false;
    const ShaderLibrary* library = ShaderLibrary::Get();
    const ShaderProgramSource* noFilter = library ? library->Find("NoFilterShader") : nullptr;
    if (source == noFilter)
        return CreateShaderProgramOrNull(source, colorCorrection);

    if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(source, colorCorrection))
        return shader;
    fprintf(stderr, "screen shader %s failed to build, falling back to no filter\n", source ? source->Name.c_str() : "(none)");
    fallback = true;
    return CreateShaderProgramOrNull(noFilter, colorCorrection);
}

}
