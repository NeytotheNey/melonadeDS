// Port of the screen drawing in the app's DSRenderer.kt, one per window as the app has one per surface: the screen quads (with the frame's
// columns and rows), the screen filter (VideoFilterPipeline, rebuilt when the filter, deband or the DS screen colors change), the screen
// shadows, GPU::Comp1x's 1x shader, deband in the screen shader where it's cheaper (DebandFold.kt) and the output cap's need. The background
// (phase 3 step B) and the Lua drawings (phase 4) aren't drawn yet; DrawFrame says where they go. UI thread only, with its GL context current
#pragma once

#include <array>
#include <memory>
#include <optional>

#include "render/PresentFrame.h"
#include "render/ShaderLibrary.h"
#include "render/VideoFilterPipeline.h"

namespace Frontend
{

// where Draw put a screen, in the window's drawable pixels (W 0 for a screen it didn't draw)
struct Rect
{
    float X = 0, Y = 0, W = 0, H = 0;

    bool operator==(const Rect&) const = default;
};

// RuntimeRendererConfiguration.kt without the internal resolution: the video filter (video_filtering), deband (video_deband), the DS screen
// colors (video_ds_screen_colors, Pokefan531's color profile) and the screen shadow (video_screen_shadow)
struct RendererConfig
{
    VideoFiltering Filtering = VideoFiltering::None;
    bool Deband = false;
    bool DsScreenColors = false;
    bool ScreenShadow = false;

    bool operator==(const RendererConfig&) const = default;
};

// DebandFold.kt's shouldFoldDeband: deband inside the screen shader, once per drawn pixel, when the switch is on, the filter takes it, no
// Lua tilt-shift, bloom or vignette runs and the screens draw fewer pixels than the frame has texels
bool ShouldFoldDeband(bool isSwitchOn, bool canFilterFold, bool isLuaEffectActive, long long drawnPixels, long long frameTexels);

class ScreenRenderer
{
public:
    ScreenRenderer() = default;
    ScreenRenderer(const ScreenRenderer&) = delete;
    ScreenRenderer& operator=(const ScreenRenderer&) = delete;

    // the GL objects deleted (the filter too), for before the context goes; made again on the next draw
    void Release();

    void UpdateRendererConfiguration(const RendererConfig& config);
    // where the screens go in a [surfaceW] x [surfaceH] drawable (a W of 0 leaves one out); the quads are built again only when it changes
    void UpdateScreenAreas(const Rect& top, const Rect& bottom, int surfaceW, int surfaceH);
    // runs the filter's pre-passes, clears the bound framebuffer (the window, at its viewport) and draws the screens; the caller sets up the
    // state the core left behind first
    void DrawFrame(const FrameView& frame, const std::optional<TiltShift>& tiltShift, const std::optional<Bloom>& bloom,
        const std::optional<DepthOfField>& depthOfField, const std::optional<Shade>& shade, const std::optional<Sun>& sun);

    // GPU::Comp1x: whether a frame with a screen at 1x can be drawn as it is (the no-filter shader without deband or a Lua tilt-shift, bloom
    // or vignette)
    bool CanDrawHalvesAt1x(bool isLuaEffectOn) const;
    // GPU::OutputCap: how many times their native size the screens are shown at, at most (0 before any were placed)
    int OutputScaleNeed() const { return ScreensOutputScaleNeed; }
    // whether the filter debands, as the output cap's any-ratio rule asks (GPU::OutputCapDeband)
    bool DebandsOutput() const { return Config.Deband; }

private:
    void ApplyRendererConfiguration();
    void UpdateScreenCoordinates();
    void UpdateShadowCoordinates(const Rect* top, const Rect* bottom);
    void UpdateShader();
    void EnsureGlObjects();
    void PutQuad(const Rect& rect, float leftU, float rightU, float topV, float bottomV, float alpha);
    int PutShadow(int offset, const Rect& rect, float alpha);
    void UseScreenQuadShader(const Shader& shader, GLuint textureId);
    void UseScreenShader(const Shader& shader, GLuint textureId, int textureWidth, int textureHeight, const FrameView& frame);
    void SetScreenVertexAttributes(const Shader& shader);
    void RenderScreenShadows();

    RendererConfig Config;
    bool MustUpdateConfiguration = true;
    bool MustRebuildScreenFilter = true;

    std::unique_ptr<VideoFilterPipeline> ScreenFilter;
    // what ScreenFilter was built with, so it's only built again when one changes
    VideoFiltering ScreenFilterFiltering = VideoFiltering::None;
    bool ScreenFilterDsScreenColors = false;
    bool ScreenFilterDeband = false;
    std::unique_ptr<Shader> ShadowShader;
    bool IsShadowShaderTried = false;

    GLuint ScreensVbo = 0, ScreensVao = 0;
    GLuint ShadowsVbo = 0, ShadowsVao = 0;
    int ScreenIndices = 0;
    int ShadowIndices = 0;
    // the screen quads: both screens and, later, their Lua drawings (DSRenderer's MAX_SCREEN_VERTEX_FLOATS), and the shadows' 8 cells a screen
    std::array<float, 4 * 6 * 5> ScreenVertices {};
    int ScreenVertexFloats = 0;
    std::array<float, 2 * 8 * 6 * 5> ShadowVertices {};
    bool IsScreensVboAllocated = false;

    int ScreensOutputScaleNeed = 0;
    long long ScreensDrawnPixels = 0;
    // debug.melonade.debandfold, the app's default (1); the desktop has no switch for it yet
    bool IsDebandFoldOn = true;
    bool WasDebandFolded = false;

    // the frame shape the quads were built for (the last valid frame's), and the screens' places
    int FrameTopScreenWidth = 256;
    int FrameTopScreenMargin = 0;
    Rect TopScreenRect, BottomScreenRect;
    float Width = 0, Height = 0;
    // whether the bottom screen is the one drawn wide; the desktop's swap keeps the top one wide, so never
    bool IsBottomScreenWideInQuads = false;
};

}
