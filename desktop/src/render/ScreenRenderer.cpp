#include "render/ScreenRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "Frontend.h"
#include "render/Backgrounds.h"
#include "render/Shader.h"

namespace Frontend
{

namespace
{

// the app's SCREEN_WIDTH, SCREEN_HEIGHT and SCREEN_GAP (domain/model/NDS.kt)
constexpr int SCREEN_WIDTH = 256;
constexpr int SCREEN_HEIGHT = 192;
constexpr int SCREEN_GAP = 2;

// Screen shadow: radius as a share of the screen's shorter side with a minimum in window pixels, and alpha at the screen's edge
constexpr float SHADOW_RADIUS_FACTOR = 0.06f;
constexpr int SHADOW_MIN_RADIUS = 8;
constexpr float SHADOW_STRENGTH = 0.6f;

// Screen and shadow vertices: position (2), UVs or distance outside the screen (2) and alpha (1); background vertices have no alpha
constexpr int FLOATS_PER_VERTEX = 5;
constexpr int BACKGROUND_FLOATS_PER_VERTEX = 4;
constexpr int VERTICES_PER_SCREEN = 6;

// FrameTextureLayout.kt: the v coordinates of the screens' rows in a frame whose top screen's half is 192 + 2 * margin rows
struct FrameTextureLayout
{
    explicit FrameTextureLayout(int topScreenMargin)
        : TopScreenRows(SCREEN_HEIGHT + 2 * std::max(topScreenMargin, 0)),
          FrameRows((float)(TopScreenRows + SCREEN_GAP + SCREEN_HEIGHT)),
          BottomScreenStartRow(TopScreenRows + SCREEN_GAP)
    {
        // The gap's 2 rows are one on each side of the split, which is the middle (0.5) without a margin
        float lineRelativeSize = 1.f / FrameRows;
        float split = (TopScreenRows + SCREEN_GAP / 2) / FrameRows;
        TopScreenBottomV = split - lineRelativeSize;
        BottomScreenTopV = split + lineRelativeSize;
    }

    // the v of [row] of the top screen's half (0 to TopScreenRows); its edges are exactly 0 and TopScreenBottomV
    float TopScreenV(float row) const
    {
        return row <= 0.f ? 0.f : row >= TopScreenRows ? TopScreenBottomV : row / FrameRows;
    }

    // the v of [row] of the bottom screen's half (0 to SCREEN_HEIGHT); its edges are exactly BottomScreenTopV and 1
    float BottomScreenV(float row) const
    {
        return row <= 0.f ? BottomScreenTopV : row >= SCREEN_HEIGHT ? 1.f : (BottomScreenStartRow + row) / FrameRows;
    }

    int TopScreenRows;
    float FrameRows;
    int BottomScreenStartRow;
    float TopScreenBottomV = 0, BottomScreenTopV = 0;
};

// the output cap's need for a screen whose middle columns are shown at [width] x [height]
int GetOutputScaleNeed(float width, float height)
{
    return std::max((int)std::ceil(std::max(width / (double)SCREEN_WIDTH, height / (double)SCREEN_HEIGHT) - 1e-3), 1);
}

// DSRenderer.getBackgroundCoords: the background's quad in NDC (6 vertices, x and y) for [mode], an image of [backgroundWidth] x
// [backgroundHeight] on a [width] x [height] surface
void GetBackgroundCoords(BackgroundMode mode, int backgroundWidth, int backgroundHeight, float width, float height, float coords[12])
{
    float backgroundAspectRatio = backgroundWidth / (float)backgroundHeight;
    float screenAspectRatio = width / height;
    // a quad from (left, bottom) to (right, top), in the app's vertex order
    auto quad = [&](float left, float bottom, float right, float top)
    {
        const float c[12] = {left, bottom, left, top, right, top, left, bottom, right, top, right, bottom};
        std::copy(c, c + 12, coords);
    };
    if (mode == BackgroundMode::Stretch)
    {
        quad(-1, -1, 1, 1);
        return;
    }
    if (screenAspectRatio > backgroundAspectRatio)
    {
        // as tall as the surface, centered across it (or at its left or right edge); the top and bottom modes center it too
        float scaleFactor = width / backgroundWidth;
        float relativeBackgroundWidth = height / (backgroundHeight * scaleFactor) * 2.f;
        if (mode == BackgroundMode::FitLeft)
            quad(-1, -1, -1 + relativeBackgroundWidth, 1);
        else if (mode == BackgroundMode::FitRight)
            quad(1.f - relativeBackgroundWidth, -1, 1, 1);
        else
            quad(-(relativeBackgroundWidth / 2.f), -1, relativeBackgroundWidth / 2.f, 1);
        return;
    }
    // as wide as the surface, centered down it (or at its top or bottom edge); the left and right modes center it too
    float scaleFactor = height / backgroundHeight;
    float relativeBackgroundHeight = width / (backgroundWidth * scaleFactor) * 2.f;
    if (mode == BackgroundMode::FitTop)
        quad(-1, 1.f - relativeBackgroundHeight, 1, 1);
    else if (mode == BackgroundMode::FitBottom)
        quad(-1, -1, 1, -1.f + relativeBackgroundHeight);
    else
        quad(-1, -(relativeBackgroundHeight / 2.f), 1, relativeBackgroundHeight / 2.f);
}

}

ScreensDrawing DrawingFor(const SurfaceScreens& screens, int frameTopWidth, int frameMargin, float surfaceWidth)
{
    const IntRect* top = screens.Top ? &screens.Top->Rect : nullptr;
    const IntRect* bottom = screens.Bottom ? &screens.Bottom->Rect : nullptr;
    return GetScreensDrawing(top, bottom, frameTopWidth, screens.IsWideLayout, screens.AreScreensSwapped, surfaceWidth, frameMargin);
}

ScreenRects RectsFor(const SurfaceScreens& screens, int frameTopWidth, int frameMargin, float surfaceWidth)
{
    if (screens.AsPlaced)
        return {screens.Top ? screens.PlacedTop : Rect(), screens.Bottom ? screens.PlacedBottom : Rect()};
    ScreensDrawing drawing = DrawingFor(screens, frameTopWidth, frameMargin, surfaceWidth);
    ScreenRects rects;
    if (drawing.Top)
        rects.Top = Rect::Of(drawing.Top->DrawRect);
    if (drawing.Bottom)
        rects.Bottom = Rect::Of(drawing.Bottom->NativeRect);
    return rects;
}

bool ShouldFoldDeband(bool isSwitchOn, bool canFilterFold, bool isTiltShiftActive, long long drawnPixels, long long frameTexels)
{
    return isSwitchOn && canFilterFold && !isTiltShiftActive && drawnPixels >= 1 && drawnPixels < frameTexels;
}

ScreenRenderer::~ScreenRenderer()
{
    DropBackgroundLoad();
}

void ScreenRenderer::DropBackgroundLoad()
{
    if (Loading)
        Loading->Job->Cancel.store(true, std::memory_order_relaxed);
    Loading.reset();
}

void ScreenRenderer::Release()
{
    ScreenFilter.reset();
    ShadowShader.reset();
    IsShadowShaderTried = false;
    // a decode still running is told to stop and dropped
    DropBackgroundLoad();
    BackgroundFailed = false;
    BackgroundShader.reset();
    IsBackgroundShaderTried = false;
    if (ScreensVbo)
    {
        GLuint vbos[] = {ScreensVbo, ShadowsVbo, BackgroundVbo};
        GLuint vaos[] = {ScreensVao, ShadowsVao, BackgroundVao};
        glDeleteBuffers(3, vbos);
        glDeleteVertexArrays(3, vaos);
        ScreensVbo = ScreensVao = ShadowsVbo = ShadowsVao = BackgroundVbo = BackgroundVao = 0;
    }
    if (BackgroundTexture)
    {
        glDeleteTextures(1, &BackgroundTexture);
        BackgroundTexture = 0;
    }
    // the new texture is empty, so the background is loaded into it again
    IsBackgroundLoaded = false;
    IsLoadedSvg = false;
    IsBackgroundPositionDirty = true;
    MustLoadBackground = !Background.Key.empty();
    IsScreensVboAllocated = false;
    MustRebuildScreenFilter = true;
    MustUpdateConfiguration = true;
}

void ScreenRenderer::EnsureGlObjects()
{
    if (ScreensVbo)
        return;
    GLuint vbos[3], vaos[3];
    glGenBuffers(3, vbos);
    glGenVertexArrays(3, vaos);
    ScreensVbo = vbos[0];
    ScreensVao = vaos[0];
    ShadowsVbo = vbos[1];
    ShadowsVao = vaos[1];
    BackgroundVbo = vbos[2];
    BackgroundVao = vaos[2];
    IsScreensVboAllocated = false;

    glGenTextures(1, &BackgroundTexture);
    glBindTexture(GL_TEXTURE_2D, BackgroundTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ScreenRenderer::UpdateRendererConfiguration(const RendererConfig& config)
{
    if (config == Config)
        return;
    Config = config;
    MustUpdateConfiguration = true;
}

void ScreenRenderer::UpdateScreenAreas(const SurfaceScreens& screens, int surfaceW, int surfaceH)
{
    if (screens == Screens && (float)surfaceW == Width && (float)surfaceH == Height)
        return;
    // onSurfaceChanged: the background is placed again, and an SVG rasterized again for the new size
    if ((float)surfaceW != Width || (float)surfaceH != Height)
    {
        IsBackgroundPositionDirty = true;
        if (IsLoadedSvg && (surfaceW != LoadedForWidth || surfaceH != LoadedForHeight))
            MustLoadBackground = true;
        // one that couldn't load before the surface had a size (not one that failed)
        if (!IsBackgroundLoaded && !Loading && !BackgroundFailed && !Background.Key.empty())
            MustLoadBackground = true;
    }
    Screens = screens;
    Width = (float)surfaceW;
    Height = (float)surfaceH;
    MustUpdateConfiguration = true;
}

void ScreenRenderer::SetBackground(const SurfaceBackground& background)
{
    if (background == Background)
        return;
    // the one before isn't shown while the new one loads (as the app's setBackground); a decode of another one is given up
    DropBackgroundLoad();
    BackgroundFailed = false;
    Background = background;
    MustLoadBackground = true;
    IsBackgroundPositionDirty = true;
    IsBackgroundLoaded = false;
    IsLoadedSvg = false;
}

void ScreenRenderer::ApplyRendererConfiguration()
{
    EnsureGlObjects();
    UpdateScreenCoordinates();
    UpdateShader();
}

void ScreenRenderer::UpdateScreenCoordinates()
{
    // The texture has 2 empty lines between the screens (SCREEN_GAP), and the top screen's half is taller by the camera's margin (stage 2)
    FrameTextureLayout layout(FrameTopScreenMargin);
    int frameWidth = std::max(FrameTopScreenWidth, SCREEN_WIDTH);

    // Each screen's quad: where it's drawn, the columns and rows of its half it shows, and the size its middle 256 columns and 192 rows are
    // shown at (the output cap's need). The app's layouts go through getScreensDrawing; the desktop's own placements are drawn where they
    // are, on fractional pixels as the test frontend drew them, the top screen's whole half (the camera's margin too) where the app shows its
    // middle rows
    struct ScreenQuad
    {
        Rect Draw;
        float LeftU = 0, RightU = 1, TopRow = 0, BottomRow = SCREEN_HEIGHT;
        float NativeW = 0, NativeH = 0;
    };
    std::optional<ScreenQuad> top, bottom;
    if (Screens.AsPlaced)
    {
        if (Screens.Top && Screens.PlacedTop.W > 0 && Screens.PlacedTop.H > 0)
        {
            const Rect& r = Screens.PlacedTop;
            top = ScreenQuad {r, 0.f, 1.f, 0.f, (float)layout.TopScreenRows, r.W * SCREEN_WIDTH / frameWidth, r.H * SCREEN_HEIGHT / layout.TopScreenRows};
        }
        if (Screens.Bottom && Screens.PlacedBottom.W > 0 && Screens.PlacedBottom.H > 0)
        {
            const Rect& r = Screens.PlacedBottom;
            float leftU = (frameWidth - SCREEN_WIDTH) / (2.f * frameWidth);
            bottom = ScreenQuad {r, leftU, 1.f - leftU, 0.f, (float)SCREEN_HEIGHT, r.W, r.H};
        }
    }
    else
    {
        // Lay out both screens for the current frame width, swap and wide-layout flags (see getScreensDrawing)
        ScreensDrawing drawing = DrawingFor(Screens, FrameTopScreenWidth, FrameTopScreenMargin, Width);
        auto quad = [](const ScreenDrawing& d)
        {
            return ScreenQuad {Rect::Of(d.DrawRect), d.LeftU, d.RightU, d.TopRow, d.BottomRow, (float)d.NativeRect.W, (float)d.NativeRect.H};
        };
        if (drawing.Top && drawing.Top->DrawRect.W > 0 && drawing.Top->DrawRect.H > 0)
            top = quad(*drawing.Top);
        if (drawing.Bottom && drawing.Bottom->DrawRect.W > 0 && drawing.Bottom->DrawRect.H > 0)
            bottom = quad(*drawing.Bottom);
    }
    IsBottomScreenWideInQuads = !Screens.AsPlaced && Screens.AreScreensSwapped;
    float topAlpha = Screens.Top ? Screens.Top->Alpha : 1.f, bottomAlpha = Screens.Bottom ? Screens.Bottom->Alpha : 1.f;

    // The output cap (GPU::OutputCap): each screen's middle columns and rows are shown at its native size, which the frame has to be at least
    // as big as
    int outputScaleNeed = 0;
    if (top)
        outputScaleNeed = GetOutputScaleNeed(top->NativeW, top->NativeH);
    if (bottom)
        outputScaleNeed = std::max(outputScaleNeed, GetOutputScaleNeed(bottom->NativeW, bottom->NativeH));
    // the part of a rect inside the surface, in pixels
    auto visibleArea = [&](const Rect& rect) -> long long
    {
        float visibleWidth = std::min(rect.X + rect.W, Width) - std::max(rect.X, 0.f);
        float visibleHeight = std::min(rect.Y + rect.H, Height) - std::max(rect.Y, 0.f);
        return visibleWidth > 0 && visibleHeight > 0 ? (long long)std::lround(visibleWidth) * std::lround(visibleHeight) : 0;
    };
    ScreensDrawnPixels = (top ? visibleArea(top->Draw) : 0) + (bottom ? visibleArea(bottom->Draw) : 0);
    if (outputScaleNeed != ScreensOutputScaleNeed)
    {
        if (VerboseLog)
            printf("OUTCAP: surface %dx%d need %d (was %d), drawn %lld px, frame width %d\n", (int)Width, (int)Height, outputScaleNeed,
                ScreensOutputScaleNeed, ScreensDrawnPixels, FrameTopScreenWidth);
        ScreensOutputScaleNeed = outputScaleNeed;
    }

    // Under-screen first, then the one drawn over it: the top screen first only when the layout puts the bottom one on top
    ScreenVertexFloats = 0;
    auto putTop = [&]
    {
        PutQuad(top->Draw, top->LeftU, top->RightU, layout.TopScreenV(top->TopRow), layout.TopScreenV(top->BottomRow), topAlpha);
    };
    auto putBottom = [&]
    {
        PutQuad(bottom->Draw, bottom->LeftU, bottom->RightU, layout.BottomScreenV(bottom->TopRow), layout.BottomScreenV(bottom->BottomRow),
            bottomAlpha);
    };
    bool topFirst = Screens.Bottom && Screens.Bottom->OnTop;
    if (topFirst && top)
        putTop();
    if (bottom)
        putBottom();
    if (!topFirst && top)
        putTop();
    // (the screens' Lua drawings go after both, in the same order: phase 4)

    glBindVertexArray(ScreensVao);
    glBindBuffer(GL_ARRAY_BUFFER, ScreensVbo);
    // The buffer's storage is made once at its largest size, and only filled after that
    if (!IsScreensVboAllocated)
    {
        glBufferData(GL_ARRAY_BUFFER, sizeof(ScreenVertices), nullptr, GL_DYNAMIC_DRAW);
        IsScreensVboAllocated = true;
    }
    if (ScreenVertexFloats > 0)
        glBufferSubData(GL_ARRAY_BUFFER, 0, ScreenVertexFloats * sizeof(float), ScreenVertices.data());
    ScreenIndices = ScreenVertexFloats / FLOATS_PER_VERTEX;

    UpdateShadowCoordinates(top ? &top->Draw : nullptr, bottom ? &bottom->Draw : nullptr, topAlpha, bottomAlpha);
}

void ScreenRenderer::PutQuad(const Rect& rect, float leftU, float rightU, float topV, float bottomV, float alpha)
{
    float left = (rect.X / Width) * 2.f - 1.f;
    float right = ((rect.X + rect.W) / Width) * 2.f - 1.f;
    float top = 1.f - rect.Y / Height * 2.f;
    float bottom = 1.f - (rect.Y + rect.H) / Height * 2.f;

    // Position, UVs and alpha: two triangles, (left bottom, left top, right top) and (left bottom, right top, right bottom)
    const float quad[VERTICES_PER_SCREEN][FLOATS_PER_VERTEX] = {
        {left, bottom, leftU, bottomV, alpha},
        {left, top, leftU, topV, alpha},
        {right, top, rightU, topV, alpha},
        {left, bottom, leftU, bottomV, alpha},
        {right, top, rightU, topV, alpha},
        {right, bottom, rightU, bottomV, alpha},
    };
    if (ScreenVertexFloats + VERTICES_PER_SCREEN * FLOATS_PER_VERTEX > (int)ScreenVertices.size())
        return;
    std::copy(&quad[0][0], &quad[0][0] + VERTICES_PER_SCREEN * FLOATS_PER_VERTEX, ScreenVertices.begin() + ScreenVertexFloats);
    ScreenVertexFloats += VERTICES_PER_SCREEN * FLOATS_PER_VERTEX;
}

void ScreenRenderer::UpdateShadowCoordinates(const Rect* top, const Rect* bottom, float topAlpha, float bottomAlpha)
{
    // Empty when the setting is off, so nothing is drawn
    int floats = 0;
    if (Config.ScreenShadow)
    {
        if (top)
            floats = PutShadow(floats, *top, topAlpha);
        if (bottom)
            floats = PutShadow(floats, *bottom, bottomAlpha);
    }
    glBindVertexArray(ShadowsVao);
    glBindBuffer(GL_ARRAY_BUFFER, ShadowsVbo);
    glBufferData(GL_ARRAY_BUFFER, floats * sizeof(float), floats ? ShadowVertices.data() : nullptr, GL_STATIC_DRAW);
    ShadowIndices = floats / FLOATS_PER_VERTEX;
}

int ScreenRenderer::PutShadow(int offset, const Rect& rect, float alpha)
{
    int radius = std::max((int)std::lround(std::min(rect.W, rect.H) * SHADOW_RADIUS_FACTOR), SHADOW_MIN_RADIUS);
    float shadowAlpha = alpha * SHADOW_STRENGTH;

    // 3x3 grid of cells around the screen minus the center; each vertex carries its distance outside the screen in radii
    const float xs[] = {rect.X - radius, rect.X, rect.X + rect.W, rect.X + rect.W + radius};
    const float ys[] = {rect.Y - radius, rect.Y, rect.Y + rect.H, rect.Y + rect.H + radius};
    const float distances[] = {1.f, 0.f, 0.f, 1.f};
    for (int row = 0; row < 3; row++)
    {
        for (int column = 0; column < 3; column++)
        {
            if (row == 1 && column == 1)
                continue;
            if (offset + VERTICES_PER_SCREEN * FLOATS_PER_VERTEX > (int)ShadowVertices.size())
                return offset;
            float left = (xs[column] / Width) * 2.f - 1.f;
            float right = (xs[column + 1] / Width) * 2.f - 1.f;
            float topY = 1.f - ys[row] / Height * 2.f;
            float bottomY = 1.f - ys[row + 1] / Height * 2.f;
            float leftDistance = distances[column], rightDistance = distances[column + 1];
            float topDistance = distances[row], bottomDistance = distances[row + 1];
            // Same vertex order as the screens
            const float cell[VERTICES_PER_SCREEN][FLOATS_PER_VERTEX] = {
                {left, bottomY, leftDistance, bottomDistance, shadowAlpha},
                {left, topY, leftDistance, topDistance, shadowAlpha},
                {right, topY, rightDistance, topDistance, shadowAlpha},
                {left, bottomY, leftDistance, bottomDistance, shadowAlpha},
                {right, topY, rightDistance, topDistance, shadowAlpha},
                {right, bottomY, rightDistance, bottomDistance, shadowAlpha},
            };
            std::copy(&cell[0][0], &cell[0][0] + VERTICES_PER_SCREEN * FLOATS_PER_VERTEX, ShadowVertices.begin() + offset);
            offset += VERTICES_PER_SCREEN * FLOATS_PER_VERTEX;
        }
    }
    return offset;
}

void ScreenRenderer::UpdateShader()
{
    // Building the filter compiles its shaders, so it's only done when the filter changes
    if (!MustRebuildScreenFilter && Config.Filtering == ScreenFilterFiltering && Config.DsScreenColors == ScreenFilterDsScreenColors
        && Config.Deband == ScreenFilterDeband)
        return;

    ScreenFilter.reset();
    const ShaderLibrary* library = ShaderLibrary::Get();
    if (library)
        ScreenFilter = std::make_unique<VideoFilterPipeline>(library->GetFilter(Config.Filtering, Config.Deband), Config.DsScreenColors);
    ScreenFilterFiltering = Config.Filtering;
    ScreenFilterDsScreenColors = Config.DsScreenColors;
    ScreenFilterDeband = Config.Deband;
    MustRebuildScreenFilter = false;
}

bool ScreenRenderer::CanDrawHalvesAt1x(bool isTiltShiftOn) const
{
    if (isTiltShiftOn)
        return false;
    // before the filter is built for the configuration (the first frame of a game, or a change, asks before the next draw), what it will be
    bool isFilterCurrent = ScreenFilter && Config.Filtering == ScreenFilterFiltering && Config.Deband == ScreenFilterDeband
        && Config.DsScreenColors == ScreenFilterDsScreenColors;
    if (!isFilterCurrent)
        return Config.Filtering == VideoFiltering::None && !Config.Deband;
    return ScreenFilter->CanDrawHalvesAt1x();
}

void ScreenRenderer::DrawFrame(const FrameView& frame, const std::optional<TiltShift>& tiltShift, bool waitForBackground)
{
    // The screens' UVs follow the width of the frame being presented, and the rows of its layout, which the camera's margin makes taller
    if (frame.IsValid() && frame.TopWidth != FrameTopScreenWidth)
    {
        FrameTopScreenWidth = frame.TopWidth;
        MustUpdateConfiguration = true;
    }
    if (frame.IsValid() && frame.TopMargin != FrameTopScreenMargin)
    {
        FrameTopScreenMargin = frame.TopMargin;
        MustUpdateConfiguration = true;
    }
    if (MustUpdateConfiguration)
    {
        ApplyRendererConfiguration();
        MustUpdateConfiguration = false;
    }

    // GPU::Comp1x: a frame with screens the compositor drew at 1x goes through the 1x shader when the full frame would only go through the
    // no-filter shader. Any other way, RenderPrePasses expands it into the full frame first
    VideoFilterPipeline* filter = ScreenFilter.get();
    const Shader* halvesAt1xShader = nullptr;
    if (frame.IsValid() && frame.HalvesAt1x != 0 && !tiltShift && filter && filter->CanDrawHalvesAt1x())
        halvesAt1xShader = filter->GetHalvesAt1xShader();

    // debug.melonade.debandfold: deband inside the screen shader, once per drawn pixel, instead of the pre-pass over every frame texel, when
    // ShouldFoldDeband allows it for this frame and layout; the pre-pass otherwise, or if the deband screen shader doesn't build
    long long frameTexels = (long long)frame.Width * frame.Height;
    // Only a tilt-shift that adds passes (VideoFilterPipeline::RenderPrePasses) keeps deband in the pre-pass
    bool isTiltShiftActive = tiltShift && (tiltShift->HasBlur() || tiltShift->HasColorBoost());
    const Shader* debandScreenShader = nullptr;
    if (frame.IsValid() && !halvesAt1xShader && filter
        && ShouldFoldDeband(IsDebandFoldOn, filter->CanFoldDeband(), isTiltShiftActive, ScreensDrawnPixels, frameTexels))
        debandScreenShader = filter->GetDebandScreenShader();
    if (frame.IsValid() && (debandScreenShader != nullptr) != WasDebandFolded)
    {
        WasDebandFolded = debandScreenShader != nullptr;
        if (VerboseLog)
            printf("DEBANDFOLD: %s (filter %d, tilt-shift %d, drawn %lld px, frame %lld texels)\n",
                WasDebandFolded ? "screen shader" : "pre-pass or none", filter && filter->CanFoldDeband(), isTiltShiftActive, ScreensDrawnPixels,
                frameTexels);
    }

    // Pre-passes must run before anything is drawn to the window; also called for no frame, to clear the cache
    const FilterOutput* screenTexture = nullptr;
    if (!halvesAt1xShader && filter)
        screenTexture = &filter->RenderPrePasses(frame, IsBottomScreenWideInQuads, tiltShift, debandScreenShader != nullptr);

    glClear(GL_COLOR_BUFFER_BIT);
    if (!frame.IsValid())
    {
        // the layout editor with no game: its background under the editor's stand-ins for the screens
        if (BackgroundWithoutFrame)
        {
            LoadBackgroundIfNeeded(waitForBackground);
            RenderBackground();
        }
        return;
    }

    LoadBackgroundIfNeeded(waitForBackground);
    RenderBackground();

    if (ShadowIndices > 0)
        RenderScreenShadows();

    // The 1x shader reads which texel the frame's sample picks from these (VideoFilterPipeline::BindHalvesAt1xCoordinates)
    if (halvesAt1xShader)
        filter->BindHalvesAt1xCoordinates(frame.Width, frame.Height);

    const Shader* shader = halvesAt1xShader ? halvesAt1xShader : debandScreenShader ? debandScreenShader : filter ? filter->ScreenShader() : nullptr;
    if (!shader || ScreenIndices == 0)
        return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(ScreensVao);
    glBindBuffer(GL_ARRAY_BUFFER, ScreensVbo);

    GLuint screenTextureId = screenTexture ? screenTexture->TextureId : frame.Texture;
    // A pre-pass with a scale makes the texture bigger than the frame
    int screenTextureWidth = screenTexture ? screenTexture->Width : frame.Width;
    int screenTextureHeight = screenTexture ? screenTexture->Height : frame.Height;
    // (with Lua drawings, each screen and then its drawings instead: phase 4)
    UseScreenShader(*shader, screenTextureId, screenTextureWidth, screenTextureHeight, frame);
    glDrawArrays(GL_TRIANGLES, 0, ScreenIndices);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ScreenRenderer::UseScreenShader(const Shader& shader, GLuint textureId, int textureWidth, int textureHeight, const FrameView& frame)
{
    UseScreenQuadShader(shader, textureId);
    shader.SetTextureSize(textureWidth, textureHeight);
    shader.SetTopScreenWidth(frame.TopWidth);
    shader.SetTopScreenHeight(frame.TopScreenHeight());
    shader.SetBottomScreenWide(IsBottomScreenWideInQuads);
    shader.SetHalvesAt1x(frame.HalvesAt1x, frame.Scale, frame.HalfScale);
}

// [shader] draws the screen quads with [textureId] bound to its tex uniform, filtered the way the shader asks
void ScreenRenderer::UseScreenQuadShader(const Shader& shader, GLuint textureId)
{
    shader.Use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, shader.TextureFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, shader.TextureFilter);

    SetScreenVertexAttributes(shader);
    glUniform1i(shader.UniformTex, 0);
}

void ScreenRenderer::SetScreenVertexAttributes(const Shader& shader)
{
    const GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
    if (shader.AttribPos >= 0)
        glVertexAttribPointer(shader.AttribPos, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    if (shader.AttribUv >= 0)
        glVertexAttribPointer(shader.AttribUv, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    if (shader.AttribAlpha >= 0)
        glVertexAttribPointer(shader.AttribAlpha, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
}

void ScreenRenderer::RenderScreenShadows()
{
    if (!IsShadowShaderTried)
    {
        IsShadowShaderTried = true;
        const ShaderLibrary* library = ShaderLibrary::Get();
        ShadowShader = CreateShaderProgramOrNull(library ? library->Find("ScreenShadowShader") : nullptr);
        if (!ShadowShader)
            fprintf(stderr, "the screen shadow shader failed to build\n");
    }
    if (!ShadowShader)
        return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(ShadowsVao);
    glBindBuffer(GL_ARRAY_BUFFER, ShadowsVbo);

    ShadowShader->Use();
    SetScreenVertexAttributes(*ShadowShader);

    glDrawArrays(GL_TRIANGLES, 0, ShadowIndices);
}

void ScreenRenderer::LoadBackgroundIfNeeded(bool wait)
{
    // a decode that finished (or, for a window shot, the one running, waited for): its image shown if it's still the background asked for
    if (Loading && (wait || Loading->Job->Done.load(std::memory_order_acquire)))
        FinishBackgroundLoad();
    if (MustLoadBackground && !Loading)
        StartBackgroundLoad();
    if (wait && Loading)
        FinishBackgroundLoad();
}

void ScreenRenderer::StartBackgroundLoad()
{
    MustLoadBackground = false;
    if (Background.Key.empty() || !Store || !BackgroundTexture || Width <= 0 || Height <= 0)
    {
        IsBackgroundLoaded = false;
        return;
    }
    if (MaxTextureSize <= 0)
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &MaxTextureSize);
    BackgroundSource source;
    std::string error;
    if (!Store->Source(Background.Key, source, error))
    {
        Store->Report(Background.Key, error);
        IsBackgroundLoaded = false;
        BackgroundFailed = true;
        return;
    }
    // a load, not a frame's work: when the background or its mode changes, or an SVG's surface does. The app does it on its GL thread; an
    // SVG takes 0.1 to 0.3 s at 1080p and up to a second at 4K, and a user's file is read there too, so it all runs on a worker and the
    // frames go on meanwhile
    BackgroundLoad load;
    load.For = Background;
    load.Width = (int)std::lround(Width);
    load.Height = (int)std::lround(Height);
    load.Job = StartBackgroundDecode(std::move(source), load.Width, load.Height, Background.Mode, MaxTextureSize);
    Loading = std::move(load);
}

void ScreenRenderer::FinishBackgroundLoad()
{
    BackgroundLoad load = std::move(*Loading);
    Loading.reset();
    // a window shot's wait: the worker ends at its next step at the latest (the decode is budgeted)
    while (!load.Job->Done.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    BackgroundJob& job = *load.Job;
    // given up, or another background was asked for meanwhile: MustLoadBackground is set for the one wanted now
    if (job.Cancelled || !(load.For == Background))
        return;
    if (job.SvgBytes)
        Store->CacheSvg(load.For.Key, job.SvgBytes);
    if (!job.Ok || job.Image.Width <= 0 || job.Image.Height <= 0)
    {
        Store->Report(load.For.Key, job.Error.empty() ? "it came out empty" : job.Error);
        IsBackgroundLoaded = false;
        BackgroundFailed = true;
        return;
    }
    // what an earlier draw or the core left behind isn't this upload's (a few at most: each call takes one flag off)
    for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; i++) {}
    // the core leaves its own unpack buffer bound
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, BackgroundTexture);
    const BackgroundImage& image = job.Image;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.Width, image.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.Rgba.data());
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        fprintf(stderr, "background %s: the texture upload failed (GL error 0x%x)\n", Store->NameOf(load.For.Key).c_str(), error);
        IsBackgroundLoaded = false;
        BackgroundFailed = true;
        return;
    }
    BackgroundWidth = image.Width;
    BackgroundHeight = image.Height;
    IsLoadedSvg = image.IsSvg;
    LoadedForWidth = load.Width;
    LoadedForHeight = load.Height;
    IsBackgroundLoaded = true;
    IsBackgroundPositionDirty = true;
    // an SVG made for a size the window has left meanwhile (even one it went to and came back from): made again for the size it has now
    if (image.IsSvg && (load.Width != (int)std::lround(Width) || load.Height != (int)std::lround(Height)))
        MustLoadBackground = true;
    printf("background: %s (%s), %dx%d for a %dx%d surface\n", Store->NameOf(load.For.Key).c_str(), BackgroundModeName(load.For.Mode),
        image.Width, image.Height, load.Width, load.Height);
}

void ScreenRenderer::UpdateBackgroundPosition()
{
    float coords[12];
    GetBackgroundCoords(Background.Mode, BackgroundWidth, BackgroundHeight, Width, Height, coords);
    // position and UVs: the texture's first row is the image's top, at v 0
    const float uvs[12] = {0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1};
    float vertices[6 * BACKGROUND_FLOATS_PER_VERTEX];
    for (int i = 0; i < 6; i++)
    {
        vertices[i * 4 + 0] = coords[i * 2];
        vertices[i * 4 + 1] = coords[i * 2 + 1];
        vertices[i * 4 + 2] = uvs[i * 2];
        vertices[i * 4 + 3] = uvs[i * 2 + 1];
    }
    glBindVertexArray(BackgroundVao);
    glBindBuffer(GL_ARRAY_BUFFER, BackgroundVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    IsBackgroundPositionDirty = false;
}

void ScreenRenderer::RenderBackground()
{
    if (!IsBackgroundLoaded)
        return;
    if (!IsBackgroundShaderTried)
    {
        IsBackgroundShaderTried = true;
        const ShaderLibrary* library = ShaderLibrary::Get();
        BackgroundShader = CreateShaderProgramOrNull(library ? library->Find("BackgroundShader") : nullptr);
        if (!BackgroundShader)
            fprintf(stderr, "the background shader failed to build\n");
    }
    if (!BackgroundShader)
        return;
    if (IsBackgroundPositionDirty)
        UpdateBackgroundPosition();

    glBindVertexArray(BackgroundVao);
    glBindBuffer(GL_ARRAY_BUFFER, BackgroundVbo);
    BackgroundShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, BackgroundTexture);
    // linear, as the app's background texture is made; a screen shader set its own filtering on the frame's texture, not this one
    const GLsizei stride = BACKGROUND_FLOATS_PER_VERTEX * sizeof(float);
    if (BackgroundShader->AttribPos >= 0)
        glVertexAttribPointer(BackgroundShader->AttribPos, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    if (BackgroundShader->AttribUv >= 0)
        glVertexAttribPointer(BackgroundShader->AttribUv, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glUniform1i(BackgroundShader->UniformTex, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

}
