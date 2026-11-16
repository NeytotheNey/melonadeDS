#include "Presenter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "NDS.h"
#include "GPU.h"
#include "OpenGLSupport.h"

using namespace melonDS;

namespace Frontend
{

namespace
{

// the output textures hold the top screen in rows 0-191 and the bottom one in 194-385 (times the
// scale), with two rows of padding between them, as the app's DSRenderer reads them. with a
// vertical margin M (OutputLayout::TopMargin) the top half is 192 + 2M rows, all of it shown here

const char* kVS = R"(#version 320 es
layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec2 vTexcoord;
out vec2 fTexcoord;
void main()
{
    fTexcoord = vTexcoord;
    gl_Position = vec4(vPosition, 0.0, 1.0);
}
)";

const char* kFS = R"(#version 320 es
precision mediump float;
uniform sampler2D ScreenTex;
in vec2 fTexcoord;
out vec4 oColor;
void main()
{
    // the core's frames are BGRA in memory, which the app's and upstream's screen shaders swap too
    oColor = vec4(texture(ScreenTex, fTexcoord).bgr, 1.0);
}
)";

// GPU::Comp1x: kFS for a frame with a half at 1x (OutputLayout). a pixel takes the texel kFS's
// nearest sample picks: the sampler's own pick, from CoordTex (a texture the frame's width x 1 whose
// texel x holds x) and CoordYTex (1 x its height, y), sampled with the same varying as kFS declares
// it, as it is. working it out in the shader isn't exact: where a sample lands on the edge between
// two texels (a 2:1 window does it at every pixel), floor() and the sampler's fixed point can choose
// differently, and with filtered 2D the two can differ. a texel in the rows of a half at 1x reads its
// native pixel in the 1x image at the start of those rows instead, and any other one (the other
// half, the padding) is sampled as kFS does. a half drawn with filtered 2D at the filter map's scale
// (OutputLayout::HalfScale) has an image that many times bigger, upscaled by Scale / HalfScale
const char* kHalvesAt1xFS = R"(#version 320 es
precision highp float;
precision highp int;
uniform sampler2D ScreenTex;
uniform highp usampler2D CoordXTex;
uniform highp usampler2D CoordYTex;
// x: the halves at 1x (bit 0 the top one), y: the frame's scale, z: the scale they were drawn at
uniform ivec3 uHalvesAt1x;
// as kFS has it
in mediump vec2 fTexcoord;
out vec4 oColor;
void main()
{
    ivec2 size = textureSize(ScreenTex, 0);
    ivec2 texel = ivec2(int(texture(CoordXTex, fTexcoord).r), int(texture(CoordYTex, fTexcoord).r));
    int scale = uHalvesAt1x.y;
    int up = scale / max(uHalvesAt1x.z, 1);
    int screenHalf = texel.y >= 194 * scale ? 1 : 0;
    int row = texel.y - screenHalf * 194 * scale;
    vec4 color;
    if (row < 192 * scale && (uHalvesAt1x.x & (1 << screenHalf)) != 0)
        color = texelFetch(ScreenTex, ivec2(min(texel.x / up, size.x / up - 1), screenHalf * 194 * scale + row / up), 0);
    else
        color = texture(ScreenTex, fTexcoord);
    oColor = vec4(color.bgr, 1.0);
}
)";

// GPU::Comp1x: turns a frame with a half at 1x into the full frame, texel for texel, for linear
// filtering. one triangle over a target the frame's size, so gl_FragCoord is each texel's center
const char* kExpandVS = R"(#version 320 es
void main()
{
    gl_Position = vec4(float((gl_VertexID & 1) * 4 - 1), float((gl_VertexID >> 1) * 4 - 1), 0.0, 1.0);
}
)";

const char* kExpandFS = R"(#version 320 es
precision highp float;
precision highp int;
uniform sampler2D ScreenTex;
uniform ivec3 uHalvesAt1x;
out vec4 oColor;
void main()
{
    ivec2 size = textureSize(ScreenTex, 0);
    ivec2 texel = ivec2(gl_FragCoord.xy);
    int scale = uHalvesAt1x.y;
    int up = scale / max(uHalvesAt1x.z, 1);
    int screenHalf = texel.y >= 194 * scale ? 1 : 0;
    int row = texel.y - screenHalf * 194 * scale;
    if (row < 192 * scale && (uHalvesAt1x.x & (1 << screenHalf)) != 0)
        texel = ivec2(min(texel.x / up, size.x / up - 1), screenHalf * 194 * scale + row / up);
    oColor = texelFetch(ScreenTex, texel, 0);
}
)";

}

bool Presenter::Init()
{
    if (!OpenGL::CompileVertexFragmentProgram(Program, kVS, kFS, "PCScreen", {{"vPosition", 0}, {"vTexcoord", 1}}, {{"oColor", 0}}))
        return false;
    TexLoc = glGetUniformLocation(Program, "ScreenTex");

    glGenVertexArrays(1, &VertexArray);
    glGenBuffers(1, &VertexBuffer);
    glBindVertexArray(VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, VertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, 12 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // samplers, so the filter doesn't touch the renderer's own texture parameters
    glGenSamplers(2, Sampler);
    for (int i = 0; i < 2; i++)
    {
        GLint filter = i ? GL_LINEAR : GL_NEAREST;
        glSamplerParameteri(Sampler[i], GL_TEXTURE_MIN_FILTER, filter);
        glSamplerParameteri(Sampler[i], GL_TEXTURE_MAG_FILTER, filter);
        glSamplerParameteri(Sampler[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(Sampler[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // the software renderer's frames, laid out like the GL output
    std::vector<u8> zero(256 * 386 * 4, 0);
    glGenTextures(1, &SoftTexture);
    glBindTexture(GL_TEXTURE_2D, SoftTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 386, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &ReadFB);
    return true;
}

void Presenter::Free()
{
    glDeleteProgram(Program);
    glDeleteProgram(HalvesAt1xProgram);
    glDeleteProgram(ExpandProgram);
    glDeleteTextures(1, &ExpandTexture);
    glDeleteTextures(2, CoordTex);
    glDeleteFramebuffers(1, &ExpandFB);
    glDeleteVertexArrays(1, &VertexArray);
    glDeleteBuffers(1, &VertexBuffer);
    glDeleteSamplers(2, Sampler);
    glDeleteTextures(1, &SoftTexture);
    glDeleteFramebuffers(1, &ReadFB);
}

bool Presenter::PrepareHalvesAt1x()
{
    if (HalvesAt1xProgram || HalvesAt1xFailed)
        return HalvesAt1xProgram != 0;

    if (!OpenGL::CompileVertexFragmentProgram(HalvesAt1xProgram, kVS, kHalvesAt1xFS, "PCScreenHalvesAt1x",
            {{"vPosition", 0}, {"vTexcoord", 1}}, {{"oColor", 0}})
        || !OpenGL::CompileVertexFragmentProgram(ExpandProgram, kExpandVS, kExpandFS, "PCExpandHalvesAt1x", {}, {{"oColor", 0}}))
    {
        fprintf(stderr, "the 1x screen programs didn't build\n");
        glDeleteProgram(HalvesAt1xProgram);
        HalvesAt1xProgram = 0;
        HalvesAt1xFailed = true;
        return false;
    }
    HalvesAt1xTexLoc = glGetUniformLocation(HalvesAt1xProgram, "ScreenTex");
    HalvesAt1xLoc = glGetUniformLocation(HalvesAt1xProgram, "uHalvesAt1x");
    glUseProgram(HalvesAt1xProgram);
    glUniform1i(glGetUniformLocation(HalvesAt1xProgram, "CoordXTex"), 1);
    glUniform1i(glGetUniformLocation(HalvesAt1xProgram, "CoordYTex"), 2);
    glGenTextures(2, CoordTex);
    ExpandTexLoc = glGetUniformLocation(ExpandProgram, "ScreenTex");
    ExpandHalvesLoc = glGetUniformLocation(ExpandProgram, "uHalvesAt1x");
    glGenFramebuffers(1, &ExpandFB);
    return true;
}

GLuint Presenter::ExpandHalvesAt1x(GLuint texture)
{
    GLint width = 0, height = 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width <= 0 || height <= 0)
        return texture;

    if (width != ExpandW || height != ExpandH)
    {
        if (!ExpandTexture)
            glGenTextures(1, &ExpandTexture);
        glBindTexture(GL_TEXTURE_2D, ExpandTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, ExpandFB);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ExpandTexture, 0);
        ExpandW = width;
        ExpandH = height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, ExpandFB);
    glViewport(0, 0, width, height);
    glUseProgram(ExpandProgram);
    glUniform1i(ExpandTexLoc, 0);
    glUniform3i(ExpandHalvesLoc, LastLayout.HalvesAt1x, LastLayout.Scale, LastLayout.HalfScale);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(0, Sampler[0]);
    glBindVertexArray(VertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ExpandTexture;
}

void Presenter::BindCoordTextures(GLuint texture)
{
    GLint width = 0, height = 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width != CoordW || height != CoordH)
    {
        std::vector<u16> coords((size_t)std::max(width, height));
        for (size_t i = 0; i < coords.size(); i++)
            coords[i] = (u16)i;
        for (int axis = 0; axis < 2; axis++)
        {
            glBindTexture(GL_TEXTURE_2D, CoordTex[axis]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, axis == 0 ? width : 1, axis == 0 ? 1 : height, 0, GL_RED_INTEGER,
                         GL_UNSIGNED_SHORT, coords.data());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
        CoordW = width;
        CoordH = height;
    }
    for (int axis = 0; axis < 2; axis++)
    {
        glActiveTexture(GL_TEXTURE1 + axis);
        glBindSampler(1 + axis, 0);
        glBindTexture(GL_TEXTURE_2D, CoordTex[axis]);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
}

void Presenter::ResetLayouts()
{
    Layouts[0] = Layouts[1] = LastLayout = OutputLayout();
}

GLuint Presenter::BindFrame(NDS& nds)
{
    int frontbuf = nds.GPU.FrontBuffer;
    glActiveTexture(GL_TEXTURE0);
    Renderer3D& renderer = nds.GPU.GetRenderer3D();
    if (renderer.Accelerated)
    {
        OutputLayout layout;
        if (renderer.TakeOutputLayout(frontbuf, layout))
            Layouts[frontbuf] = layout;
        LastLayout = Layouts[frontbuf];

        renderer.BindOutputTexture(frontbuf);
        GLint texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        return (GLuint)texture;
    }

    LastLayout = OutputLayout();
    glBindTexture(GL_TEXTURE_2D, SoftTexture);
    if (nds.GPU.Framebuffer[frontbuf][0] && nds.GPU.Framebuffer[frontbuf][1])
    {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds.GPU.Framebuffer[frontbuf][0].get());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 194, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds.GPU.Framebuffer[frontbuf][1].get());
    }
    return SoftTexture;
}

void Presenter::Draw(NDS& nds, int windowW, int windowH, int topWidth, Layout layout, bool linear)
{
    // the core leaves its own state behind, so all of what's drawn here is set first
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, windowW, windowH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    LastTexture = BindFrame(nds);
    // camera stage 2: the top half's margin rows, shown above and below it
    int margin = nds.GPU.GetRenderer3D().Accelerated ? LastLayout.TopMargin : 0;
    float rows = 386.f + 2.f * margin;
    float topBottomV = (192.f + 2.f * margin) / rows, bottomTopV = (194.f + 2.f * margin) / rows;

    // the frame in DS pixels: the top screen is topWidth wide (and 192 + 2 x margin tall), the bottom one 256
    float topW = (float)topWidth, botW = 256.f, h = 192.f, topH = 192.f + 2.f * margin;
    float boxW, boxH;
    switch (layout)
    {
    case Layout::Horizontal: boxW = topW + botW; boxH = topH; break;
    case Layout::TopOnly: boxW = topW; boxH = topH; break;
    case Layout::BottomOnly: boxW = botW; boxH = h; break;
    default: boxW = std::max(topW, botW); boxH = topH + h; break;
    }
    float scale = std::min(windowW / boxW, windowH / boxH);
    float x0 = (windowW - boxW * scale) / 2, y0 = (windowH - boxH * scale) / 2;

    Top = Bottom = Rect();
    switch (layout)
    {
    case Layout::Horizontal:
        Top = {x0, y0, topW * scale, topH * scale};
        Bottom = {x0 + topW * scale, y0 + (topH - h) * scale / 2, botW * scale, h * scale};
        break;
    case Layout::TopOnly:
        Top = {x0, y0, topW * scale, topH * scale};
        break;
    case Layout::BottomOnly:
        Bottom = {x0, y0, botW * scale, h * scale};
        break;
    default:
        Top = {x0 + (boxW - topW) * scale / 2, y0, topW * scale, topH * scale};
        Bottom = {x0 + (boxW - botW) * scale / 2, y0 + topH * scale, botW * scale, h * scale};
        break;
    }

    // the bottom screen sits in the middle 256 columns of a wider output, between black bars
    float botU0 = 0.5f - 0.5f * botW / topW, botU1 = 0.5f + 0.5f * botW / topW;
    float verts[12 * 4];
    int count = 0;
    auto quad = [&](const Rect& r, float u0, float u1, float v0, float v1)
    {
        if (r.W <= 0)
            return;
        // window pixels (y down) to clip space (y up)
        float l = r.X / windowW * 2 - 1, rr = (r.X + r.W) / windowW * 2 - 1;
        float t = 1 - r.Y / windowH * 2, b = 1 - (r.Y + r.H) / windowH * 2;
        float q[6][4] = {
            {l, t, u0, v0}, {rr, t, u1, v0}, {l, b, u0, v1},
            {rr, t, u1, v0}, {rr, b, u1, v1}, {l, b, u0, v1},
        };
        memcpy(&verts[count * 4], q, sizeof(q));
        count += 6;
    };
    quad(Top, 0, 1, 0, topBottomV);
    quad(Bottom, botU0, botU1, bottomTopV, 1);
    // GPU::Comp1x: a frame with a half at 1x is drawn nearest straight from the 1x image, and expanded
    // into the full frame first for linear filtering (the core only draws such frames without it, but
    // the last one can be drawn again after F8)
    bool halvesAt1x = LastLayout.HalvesAt1x != 0 && PrepareHalvesAt1x();
    if (halvesAt1x && !linear)
    {
        BindCoordTextures(LastTexture);
        glBindSampler(0, Sampler[0]);
        glUseProgram(HalvesAt1xProgram);
        glUniform1i(HalvesAt1xTexLoc, 0);
        glUniform3i(HalvesAt1xLoc, LastLayout.HalvesAt1x, LastLayout.Scale, LastLayout.HalfScale);
    }
    else
    {
        if (halvesAt1x)
        {
            GLuint expanded = ExpandHalvesAt1x(LastTexture);
            glViewport(0, 0, windowW, windowH);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, expanded);
        }
        glBindSampler(0, Sampler[linear ? 1 : 0]);
        glUseProgram(Program);
        glUniform1i(TexLoc, 0);
    }
    glBindVertexArray(VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, VertexBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * 4 * sizeof(float), verts);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glBindVertexArray(0);
    glBindSampler(0, 0);
}

bool Presenter::ReadFrame(std::vector<u8>& pixels, int& width, int& height)
{
    if (!LastTexture)
        return false;
    // errors the core left behind aren't the readback's
    while (glGetError() != GL_NO_ERROR) {}
    glBindTexture(GL_TEXTURE_2D, LastTexture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width <= 0 || height <= 0)
    {
        fprintf(stderr, "frame readback: texture %u is %dx%d\n", LastTexture, width, height);
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFB);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, LastTexture, 0);
    pixels.resize((size_t)width * height * 4);
    // the core's capture readback leaves its pixel buffer bound
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // the texture holds BGRA, as the screen shader reads it
    for (size_t i = 0; i < pixels.size(); i += 4)
        std::swap(pixels[i], pixels[i + 2]);

    // GPU::Comp1x: a half at 1x (or at the filter map's scale), upscaled over its rows from the image at
    // their start, as Draw shows it
    for (int half = 0; half < 2; half++)
    {
        if (!(LastLayout.HalvesAt1x & (1 << half)))
            continue;
        int scale = LastLayout.Scale, halfScale = std::max(LastLayout.HalfScale, 1), y0 = half * 194 * scale;
        if (scale < 1 || scale % halfScale != 0 || y0 + 192 * scale > height)
            continue;
        int up = scale / halfScale, smallW = width / up, smallH = 192 * halfScale;
        std::vector<u8> small((size_t)smallW * smallH * 4);
        for (int y = 0; y < smallH; y++)
            memcpy(&small[(size_t)y * smallW * 4], &pixels[(size_t)(y0 + y) * width * 4], (size_t)smallW * 4);
        for (int y = 0; y < 192 * scale; y++)
        {
            u8* row = &pixels[(size_t)(y0 + y) * width * 4];
            const u8* smallRow = &small[(size_t)(y / up) * smallW * 4];
            for (int x = 0; x < width; x++)
                memcpy(&row[x * 4], &smallRow[std::min(x / up, smallW - 1) * 4], 4);
        }
    }
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        fprintf(stderr, "frame readback: GL error 0x%x\n", error);
    return error == GL_NO_ERROR;
}

bool WriteBMP(const char* path, const std::vector<u8>& pixels, int width, int height)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    int rowSize = (width * 3 + 3) & ~3;
    u32 dataSize = (u32)rowSize * height;
    u8 header[54] = {'B', 'M'};
    auto put32 = [&](int at, u32 v) { for (int i = 0; i < 4; i++) header[at + i] = (u8)(v >> (i * 8)); };
    put32(2, 54 + dataSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, (u32)width);
    put32(22, (u32)-height); // negative: rows top first
    header[26] = 1;
    header[28] = 24;
    put32(34, dataSize);
    fwrite(header, 1, sizeof(header), f);

    std::vector<u8> row(rowSize, 0);
    for (int y = 0; y < height; y++)
    {
        const u8* src = &pixels[(size_t)y * width * 4];
        for (int x = 0; x < width; x++)
        {
            row[x * 3 + 0] = src[x * 4 + 2];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        fwrite(row.data(), 1, rowSize, f);
    }
    fclose(f);
    return true;
}

}
