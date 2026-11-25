#include "Presenter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "OpenGLSupport.h"
#include "renderer/FrameQueue.h"
#include "scripting/LuaDraw.h"

using namespace melonDS;

namespace Frontend
{

namespace
{

// the frame textures hold the top screen in rows 0-191 and the bottom one in 194-385 (times the scale), with two rows of padding between
// them, as the app's DSRenderer reads them. with a vertical margin M (Frame::topScreenMargin) the top half is 192 + 2M rows, all of it shown
// here but the sun's guard band (Frame::topScreenGuard). the screens are drawn with the app's shaders (render/ScreenRenderer.cpp)

// the touch cursor: a ring with a dark edge (a filled dot while it touches), on a quad around it; uRect is the quad in NDC
const char* kCursorVS = R"(#version 320 es
uniform vec4 uRect;
out vec2 fLocal;
void main()
{
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    fLocal = corner * 2.0 - 1.0;
    gl_Position = vec4(uRect.xy + corner * uRect.zw, 0.0, 1.0);
}
)";

const char* kCursorFS = R"(#version 320 es
precision mediump float;
// x: 1 while it touches, y: the radius in pixels, for a one-pixel soft edge
uniform vec2 uState;
in vec2 fLocal;
out vec4 oColor;
void main()
{
    float d = length(fLocal);
    float px = 1.0 / uState.y;
    float outer = 1.0 - smoothstep(1.0 - px, 1.0, d);
    // a white ring 3 pixels wide inside a dark one, so it shows on light and dark games; filled while touching
    float ringIn = uState.x > 0.5 ? 0.0 : 1.0 - 5.0 * px;
    float white = smoothstep(ringIn - px, ringIn, d) * (1.0 - smoothstep(1.0 - 3.0 * px, 1.0 - 2.0 * px, d));
    float alpha = outer * smoothstep(ringIn - 3.0 * px, ringIn - 2.0 * px, d);
    vec3 color = mix(vec3(0.1), vec3(1.0), white);
    oColor = vec4(color * alpha * 0.9, alpha * 0.9);
}
)";

// a frame's Lua drawings (FrameView::OverlayTexture) as RGBA rows, top row first, through [readFramebuffer]; false if it isn't the overlay's size
bool ReadDrawings(GLuint readFramebuffer, GLuint texture, std::vector<u8>& drawings)
{
    using MelonDSAndroid::LuaOverlay;
    GLint width = 0, height = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width != LuaOverlay::kWidth || height != LuaOverlay::kHeight)
    {
        fprintf(stderr, "frame readback: the Lua drawings' texture %u is %dx%d\n", texture, width, height);
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    drawings.resize((size_t)width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, drawings.data());
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // the frame texture's byte order (BGRA), swapped as ReadFrame swaps the frame's
    for (size_t i = 0; i < drawings.size(); i += 4)
        std::swap(drawings[i], drawings[i + 2]);
    return true;
}

// [drawings]' DS [screen] (0 top, 1 bottom) blended over that native screen of [frame] read back at [scale] into [pixels] ([width] wide),
// premultiplied and keeping the frame's alpha, as ScreenshotRenderer::renderOverlay: nearest from the 2x drawings, or at 1x the average of
// each pixel's 2 x 2 of them, as the app's 1x screenshot takes them
void BlendDrawings(const std::vector<u8>& drawings, int screen, const FrameView& frame, int scale, std::vector<u8>& pixels, int width)
{
    using MelonDSAndroid::LuaOverlay;
    int left = frame.NativeLeft() * scale;
    int top = (screen == 0 ? frame.NativeTop() : frame.TopScreenHeight() + 2) * scale;
    int drawingsTop = screen == 0 ? 0 : LuaOverlay::kBottomScreenRow;
    for (int y = 0; y < 192 * scale; y++)
    {
        u8* row = &pixels[((size_t)(top + y) * width + left) * 4];
        for (int x = 0; x < 256 * scale; x++)
        {
            int source[4] = {};
            if (scale == 1)
            {
                for (int i = 0; i < 4; i++)
                {
                    const u8* texel = &drawings[((size_t)(drawingsTop + 2 * y + i / 2) * LuaOverlay::kWidth + 2 * x + i % 2) * 4];
                    for (int c = 0; c < 4; c++)
                        source[c] += texel[c];
                }
                for (int c = 0; c < 4; c++)
                    source[c] = (source[c] + 2) / 4;
            }
            else
            {
                // the drawings' texel under this pixel's center
                const u8* texel = &drawings[((size_t)(drawingsTop + (2 * y + 1) / scale) * LuaOverlay::kWidth + (2 * x + 1) / scale) * 4];
                for (int c = 0; c < 4; c++)
                    source[c] = texel[c];
            }
            if (source[3] == 0 && source[0] == 0 && source[1] == 0 && source[2] == 0)
                continue;
            u8* pixel = &row[x * 4];
            for (int c = 0; c < 3; c++)
                pixel[c] = (u8)std::min(255, source[c] + (pixel[c] * (255 - source[3]) + 127) / 255);
        }
    }
}

}

void Presenter::DrawCursor(float x, float y, float radius, bool pressed, int windowW, int windowH)
{
    if (!CursorProgram && !CursorFailed)
    {
        if (!OpenGL::CompileVertexFragmentProgram(CursorProgram, kCursorVS, kCursorFS, "PCCursor", {}, {{"oColor", 0}}))
        {
            CursorFailed = true;
            CursorProgram = 0;
        }
        else
        {
            CursorRectLoc = glGetUniformLocation(CursorProgram, "uRect");
            CursorStateLoc = glGetUniformLocation(CursorProgram, "uState");
        }
    }
    if (!CursorProgram || windowW <= 0 || windowH <= 0)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW, windowH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    // premultiplied
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(CursorProgram);
    // window pixels (y down) to NDC (y up)
    float x0 = (x - radius) / windowW * 2 - 1, y0 = 1 - (y + radius) / windowH * 2;
    glUniform4f(CursorRectLoc, x0, y0, 2 * radius / windowW * 2, 2 * radius / windowH * 2);
    glUniform2f(CursorStateLoc, pressed ? 1.f : 0.f, radius);
    glBindVertexArray(VertexArray);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

bool Presenter::Init()
{
    // the app's shaders and filter table, embedded from desktop/shaders: without them nothing could be drawn
    if (!ShaderLibrary::Get())
        return false;
    glGenVertexArrays(1, &VertexArray);
    glGenFramebuffers(1, &ReadFB);
    return true;
}

void Presenter::Free()
{
    for (ScreenRenderer& surface : Surfaces)
        surface.Release();
    glDeleteProgram(CursorProgram);
    glDeleteVertexArrays(1, &VertexArray);
    glDeleteFramebuffers(1, &ReadFB);
}

FrameView FrameView::Of(const Frame& frame)
{
    FrameView view;
    view.Texture = frame.frameTexture;
    view.Width = (int)frame.width;
    view.Height = (int)frame.height;
    view.TopWidth = (int)frame.topScreenWidth;
    view.TopMargin = (int)frame.topScreenMargin;
    // MelonInstance keeps it within the frame's width and margin already
    view.TopGuard = frame.topScreenGuard;
    view.HalvesAt1x = frame.halvesAt1x;
    view.Scale = frame.scaleAt1x;
    view.HalfScale = frame.halfScaleAt1x;
    view.Sequence = frame.sequence;
    view.DepthTexture = frame.hasDepth ? frame.depthTexture : 0;
    view.DepthProjection = frame.depthProjection;
    view.DepthViewport = frame.depthViewport;
    view.DepthCameraValid = frame.depthCameraValid;
    view.DepthFocus = frame.hasDepth ? frame.depthFocus : -1.f;
    view.TopWingLines = frame.topWingLines;
    view.OverlayTexture = frame.overlayTexture;
    view.OverlayScreens = frame.overlayTexture ? (frame.overlayRows[0].isEmpty() ? 0 : 1) | (frame.overlayRows[1].isEmpty() ? 0 : 2) : 0;
    return view;
}

ScreenRects Presenter::Place(const FrameView& frame, int windowW, int windowH, Layout layout, Fit fit, bool swap)
{
    if (swap && layout == Layout::TopOnly)
        layout = Layout::BottomOnly;
    else if (swap && layout == Layout::BottomOnly)
        layout = Layout::TopOnly;
    int margin = frame.ShownTopMargin();
    // the frame in DS pixels as shown: the top screen is its shown width wide (and 192 + 2 x margin tall), the bottom one 256; with the
    // WideMelon camera's window, the window's show width and 192 rows, whatever the frame's render width and margin are
    float topW = (float)frame.ShownTopWidth(), botW = 256.f, h = 192.f, topH = 192.f + 2.f * margin;
    if (const WideWindow* window = UsableWideWindow(frame.Window ? &*frame.Window : nullptr, frame.ShownTopWidth()))
    {
        topW = (float)window->ShowWidth;
        topH = h;
    }
    float boxW, boxH;
    switch (layout)
    {
    case Layout::Horizontal: boxW = topW + botW; boxH = topH; break;
    case Layout::TopOnly: boxW = topW; boxH = topH; break;
    case Layout::BottomOnly: boxW = botW; boxH = h; break;
    default: boxW = std::max(topW, botW); boxH = topH + h; break;
    }
    // the box's scale across (sx) and down (sy), which only the stretch fit makes differ
    float sx = std::min(windowW / boxW, windowH / boxH), sy = sx;
    float x0, y0;
    if (fit == Fit::Stretch)
    {
        sx = windowW / boxW;
        sy = windowH / boxH;
        x0 = y0 = 0;
    }
    else if (fit == Fit::Integer && sx >= 1)
    {
        sx = sy = std::floor(sx);
        // on whole pixels, so every DS pixel is the same size
        x0 = std::floor((windowW - boxW * sx) / 2);
        y0 = std::floor((windowH - boxH * sy) / 2);
    }
    else
    {
        x0 = (windowW - boxW * sx) / 2;
        y0 = (windowH - boxH * sy) / 2;
    }

    Rect Top, Bottom;
    switch (layout)
    {
    case Layout::Horizontal:
        if (swap)
        {
            Bottom = {x0, y0 + (topH - h) * sy / 2, botW * sx, h * sy};
            Top = {x0 + botW * sx, y0, topW * sx, topH * sy};
        }
        else
        {
            Top = {x0, y0, topW * sx, topH * sy};
            Bottom = {x0 + topW * sx, y0 + (topH - h) * sy / 2, botW * sx, h * sy};
        }
        break;
    case Layout::TopOnly:
        Top = {x0, y0, topW * sx, topH * sy};
        break;
    case Layout::BottomOnly:
        Bottom = {x0, y0, botW * sx, h * sy};
        break;
    default:
        if (swap)
        {
            Bottom = {x0 + (boxW - botW) * sx / 2, y0, botW * sx, h * sy};
            Top = {x0 + (boxW - topW) * sx / 2, y0 + h * sy, topW * sx, topH * sy};
        }
        else
        {
            Top = {x0 + (boxW - topW) * sx / 2, y0, topW * sx, topH * sy};
            Bottom = {x0 + (boxW - botW) * sx / 2, y0 + topH * sy, botW * sx, h * sy};
        }
        break;
    }
    return {Top, Bottom};
}

ScreenRects Presenter::Draw(const FrameView& frame, int windowW, int windowH, const SurfaceScreens& screens, const SurfaceBackground& background,
    int surface, bool waitForBackground)
{
    // the core leaves its own state behind, so all of what's drawn here is set first
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    // draw buffer 0 only, as the app's frontend leaves the others: the core's passes must set their own
    // masks (the GPU 2D shadow's StackTex is draw buffer 1, which the OpenGL 3D renderer leaves masked)
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // the screen shaders set their textures' filtering, as the app does, so no sampler object may stand in front of it (the 1x shader's
    // coordinates are on units 1 and 2)
    for (GLuint unit = 0; unit < 3; unit++)
        glBindSampler(unit, 0);
    glViewport(0, 0, windowW, windowH);
    glClearColor(0, 0, 0, 1);

    ScreenRenderer& renderer = Surfaces[std::clamp(surface, 0, SURFACE_COUNT - 1)];
    renderer.UpdateRendererConfiguration(Config);
    renderer.SetBackground(background);
    renderer.SetBackgroundWithoutFrame(screens.ShowBackgroundWithoutFrame);
    ScreenRects rects;
    if (frame.IsValid() || screens.ShowBackgroundWithoutFrame)
        renderer.UpdateScreenAreas(screens, windowW, windowH);
    if (frame.IsValid())
        rects = RectsFor(screens, frame.TopWidth, frame.TopMargin, frame.TopGuard, (float)windowW, frame.Window ? &*frame.Window : nullptr);
    renderer.DrawFrame(frame, CurrentTiltShift, CurrentBloom, CurrentDepthOfField, CurrentShade, CurrentSun, waitForBackground);
    return rects;
}

bool Presenter::CanDrawHalvesAt1x(int surfaces) const
{
    bool can = true;
    for (int i = 0; i < std::min(surfaces, SURFACE_COUNT); i++)
        can = can && Surfaces[i].CanDrawHalvesAt1x(CurrentTiltShift.has_value() || CurrentBloom.has_value() || CurrentDepthOfField.has_value()
            || CurrentShade.has_value() || CurrentSun.has_value());
    return can;
}

int Presenter::OutputScaleNeed(int surfaces) const
{
    int need = 0;
    for (int i = 0; i < std::min(surfaces, SURFACE_COUNT); i++)
        need = std::max(need, Surfaces[i].OutputScaleNeed());
    return need;
}

bool Presenter::ReadFrame(const FrameView& frame, std::vector<u8>& pixels, int& width, int& height, bool withDrawings)
{
    GLuint texture = frame.Texture;
    if (!texture)
        return false;
    // errors the core left behind aren't the readback's
    while (glGetError() != GL_NO_ERROR) {}
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width <= 0 || height <= 0)
    {
        fprintf(stderr, "frame readback: texture %u is %dx%d\n", texture, width, height);
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFB);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
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
        if (!(frame.HalvesAt1x & (1 << half)))
            continue;
        int scale = frame.Scale, halfScale = std::max(frame.HalfScale, 1), y0 = half * 194 * scale;
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

    if (withDrawings && frame.OverlayTexture && frame.OverlayScreens)
    {
        // the native screens' place at the frame's scale, which a frame of an unexpected size can't give
        int drawingsScale = height / frame.FrameHeight();
        std::vector<u8> drawings;
        bool fits = drawingsScale >= 1 && drawingsScale * frame.FrameHeight() == height
            && (frame.NativeLeft() + 256) * drawingsScale <= width;
        if (!fits)
            fprintf(stderr, "frame readback: no Lua drawings on a %dx%d frame\n", width, height);
        else if (ReadDrawings(ReadFB, frame.OverlayTexture, drawings))
        {
            for (int screen = 0; screen < 2; screen++)
            {
                if (frame.OverlayScreens & (1 << screen))
                    BlendDrawings(drawings, screen, frame, drawingsScale, pixels, width);
            }
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
