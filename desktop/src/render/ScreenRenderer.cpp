#include "render/ScreenRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Frontend.h"

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

// Screen and shadow vertices: position (2), UVs or distance outside the screen (2) and alpha (1)
constexpr int FLOATS_PER_VERTEX = 5;
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

}

bool ShouldFoldDeband(bool isSwitchOn, bool canFilterFold, bool isLuaEffectActive, long long drawnPixels, long long frameTexels)
{
    return isSwitchOn && canFilterFold && !isLuaEffectActive && drawnPixels >= 1 && drawnPixels < frameTexels;
}

void ScreenRenderer::Release()
{
    ScreenFilter.reset();
    ShadowShader.reset();
    IsShadowShaderTried = false;
    if (ScreensVbo)
    {
        GLuint vbos[] = {ScreensVbo, ShadowsVbo};
        GLuint vaos[] = {ScreensVao, ShadowsVao};
        glDeleteBuffers(2, vbos);
        glDeleteVertexArrays(2, vaos);
        ScreensVbo = ScreensVao = ShadowsVbo = ShadowsVao = 0;
    }
    IsScreensVboAllocated = false;
    MustRebuildScreenFilter = true;
    MustUpdateConfiguration = true;
}

void ScreenRenderer::EnsureGlObjects()
{
    if (ScreensVbo)
        return;
    GLuint vbos[2], vaos[2];
    glGenBuffers(2, vbos);
    glGenVertexArrays(2, vaos);
    ScreensVbo = vbos[0];
    ScreensVao = vaos[0];
    ShadowsVbo = vbos[1];
    ShadowsVao = vaos[1];
    IsScreensVboAllocated = false;
}

void ScreenRenderer::UpdateRendererConfiguration(const RendererConfig& config)
{
    if (config == Config)
        return;
    Config = config;
    MustUpdateConfiguration = true;
}

void ScreenRenderer::UpdateScreenAreas(const Rect& top, const Rect& bottom, int surfaceW, int surfaceH)
{
    if (top == TopScreenRect && bottom == BottomScreenRect && (float)surfaceW == Width && (float)surfaceH == Height)
        return;
    TopScreenRect = top;
    BottomScreenRect = bottom;
    Width = (float)surfaceW;
    Height = (float)surfaceH;
    MustUpdateConfiguration = true;
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
    const Rect* topScreen = TopScreenRect.W > 0 && TopScreenRect.H > 0 ? &TopScreenRect : nullptr;
    const Rect* bottomScreen = BottomScreenRect.W > 0 && BottomScreenRect.H > 0 ? &BottomScreenRect : nullptr;

    // The output cap (GPU::OutputCap): each screen's middle 256 columns and 192 rows are shown at that part of its rect, which the frame has
    // to be at least as big as. The desktop shows the top screen's whole half (the camera's margin too), where the app shows its middle rows
    int frameWidth = std::max(FrameTopScreenWidth, SCREEN_WIDTH);
    // The sun's guard band is rendered and filtered but cut off here (Frame::topScreenGuard, which MelonInstance keeps within the frame)
    int guard = FrameTopScreenGuard;
    int shownWidth = frameWidth - 2 * guard;
    int shownRows = layout.TopScreenRows - 2 * guard;
    int outputScaleNeed = 0;
    if (topScreen)
        outputScaleNeed = GetOutputScaleNeed(topScreen->W * SCREEN_WIDTH / shownWidth, topScreen->H * SCREEN_HEIGHT / shownRows);
    if (bottomScreen)
        outputScaleNeed = std::max(outputScaleNeed, GetOutputScaleNeed(bottomScreen->W, bottomScreen->H));
    auto visibleArea = [&](const Rect& rect) -> long long
    {
        float visibleWidth = std::min(rect.X + rect.W, Width) - std::max(rect.X, 0.f);
        float visibleHeight = std::min(rect.Y + rect.H, Height) - std::max(rect.Y, 0.f);
        return visibleWidth > 0 && visibleHeight > 0 ? (long long)std::lround(visibleWidth) * std::lround(visibleHeight) : 0;
    };
    ScreensDrawnPixels = (topScreen ? visibleArea(*topScreen) : 0) + (bottomScreen ? visibleArea(*bottomScreen) : 0);
    if (outputScaleNeed != ScreensOutputScaleNeed)
    {
        if (VerboseLog)
            printf("OUTCAP: surface %dx%d need %d (was %d), drawn %lld px, frame width %d\n", (int)Width, (int)Height, outputScaleNeed,
                ScreensOutputScaleNeed, ScreensDrawnPixels, FrameTopScreenWidth);
        ScreensOutputScaleNeed = outputScaleNeed;
    }

    // Under-screen first, then the one drawn over it (the desktop's screens never overlap, and the app's default has the bottom one first)
    ScreenVertexFloats = 0;
    if (bottomScreen)
    {
        // the bottom screen's 256 columns in the middle of a wider frame, between black bars
        float leftU = (frameWidth - SCREEN_WIDTH) / (2.f * frameWidth);
        PutQuad(*bottomScreen, leftU, 1.f - leftU, layout.BottomScreenV(0), layout.BottomScreenV(SCREEN_HEIGHT), 1.f);
    }
    if (topScreen)
    {
        float guardU = guard / (float)frameWidth;
        PutQuad(*topScreen, guardU, 1.f - guardU, layout.TopScreenV((float)guard), layout.TopScreenV((float)(layout.TopScreenRows - guard)), 1.f);
    }
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

    UpdateShadowCoordinates(topScreen, bottomScreen);
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

void ScreenRenderer::UpdateShadowCoordinates(const Rect* top, const Rect* bottom)
{
    // Empty when the setting is off, so nothing is drawn
    int floats = 0;
    if (Config.ScreenShadow)
    {
        if (top)
            floats = PutShadow(floats, *top, 1.f);
        if (bottom)
            floats = PutShadow(floats, *bottom, 1.f);
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

bool ScreenRenderer::CanDrawHalvesAt1x(bool isLuaEffectOn) const
{
    if (isLuaEffectOn)
        return false;
    // before the filter is built for the configuration (the first frame of a game, or a change, asks before the next draw), what it will be
    bool isFilterCurrent = ScreenFilter && Config.Filtering == ScreenFilterFiltering && Config.Deband == ScreenFilterDeband
        && Config.DsScreenColors == ScreenFilterDsScreenColors;
    if (!isFilterCurrent)
        return Config.Filtering == VideoFiltering::None && !Config.Deband;
    return ScreenFilter->CanDrawHalvesAt1x();
}

void ScreenRenderer::DrawFrame(const FrameView& frame, const std::optional<TiltShift>& tiltShift, const std::optional<Bloom>& bloom,
    const std::optional<DepthOfField>& depthOfField, const std::optional<Shade>& shade, const std::optional<Sun>& sun)
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
    if (frame.IsValid() && frame.TopGuard != FrameTopScreenGuard)
    {
        FrameTopScreenGuard = frame.TopGuard;
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
    if (frame.IsValid() && frame.HalvesAt1x != 0 && !tiltShift && !bloom && !depthOfField && !shade && !sun && filter
        && filter->CanDrawHalvesAt1x())
        halvesAt1xShader = filter->GetHalvesAt1xShader();

    // debug.melonade.debandfold: deband inside the screen shader, once per drawn pixel, instead of the pre-pass over every frame texel, when
    // ShouldFoldDeband allows it for this frame and layout; the pre-pass otherwise, or if the deband screen shader doesn't build
    long long frameTexels = (long long)frame.Width * frame.Height;
    // Only a Lua effect that adds passes (a shading, sun shadows, depth of field, tilt-shift, bloom or vignette, see
    // VideoFilterPipeline::RenderPrePasses) keeps deband in the pre-pass
    bool isLuaEffectActive = (tiltShift && (tiltShift->HasBlur() || tiltShift->HasColorBoost())) || (bloom && !bloom->IsOff())
        || (depthOfField && !depthOfField->IsOff()) || (shade && !shade->IsOff()) || (sun && !sun->IsOff());
    const Shader* debandScreenShader = nullptr;
    if (frame.IsValid() && !halvesAt1xShader && filter
        && ShouldFoldDeband(IsDebandFoldOn, filter->CanFoldDeband(), isLuaEffectActive, ScreensDrawnPixels, frameTexels))
        debandScreenShader = filter->GetDebandScreenShader();
    if (frame.IsValid() && (debandScreenShader != nullptr) != WasDebandFolded)
    {
        WasDebandFolded = debandScreenShader != nullptr;
        if (VerboseLog)
            printf("DEBANDFOLD: %s (filter %d, Lua effect %d, drawn %lld px, frame %lld texels)\n",
                WasDebandFolded ? "screen shader" : "pre-pass or none", filter && filter->CanFoldDeband(), isLuaEffectActive, ScreensDrawnPixels,
                frameTexels);
    }

    // Pre-passes must run before anything is drawn to the window; also called for no frame, to clear the cache
    const FilterOutput* screenTexture = nullptr;
    if (!halvesAt1xShader && filter)
        screenTexture = &filter->RenderPrePasses(frame, IsBottomScreenWideInQuads, tiltShift, bloom, depthOfField, shade, sun,
            debandScreenShader != nullptr);

    glClear(GL_COLOR_BUFFER_BIT);
    if (!frame.IsValid())
        return;

    // (the background goes here, under the shadows: phase 3 step B)

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

}
