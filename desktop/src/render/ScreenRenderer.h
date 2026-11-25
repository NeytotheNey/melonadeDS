// Port of the screen drawing in the app's DSRenderer.kt, one per window as the app has one per surface: the background (the layout's, an
// SVG rasterized for the surface or an image), the screen quads where the layout puts them (getScreensDrawing, with each screen's opacity
// and the one on top drawn last), the screen filter (VideoFilterPipeline, rebuilt when the filter, deband or the DS screen colors change), the
// screen shadows, GPU::Comp1x's 1x shader, deband in the screen shader where it's cheaper (DebandFold.kt), each screen's Lua drawings after it
// (unfiltered, premultiplied) and the output cap's need. UI thread only, with its GL context current
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "layout/LayoutGeometry.h"
#include "render/Backgrounds.h"
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

    static Rect Of(const IntRect& r) { return {(float)r.X, (float)r.Y, (float)r.W, (float)r.H}; }
};

// a DS screen's place in a layout: its rect in the surface's pixels, its opacity and whether the layout puts it on top
struct ScreenPlace
{
    IntRect Rect;
    float Alpha = 1;
    bool OnTop = false;

    bool operator==(const ScreenPlace&) const = default;
};

// what DSRenderer.updateScreenAreas gets: where the DS top and bottom screens go (the layout's rects, already traded when the screens are
// swapped; none for a screen the layout doesn't show), whether the layout is a wide one and whether the screens are swapped
struct SurfaceScreens
{
    std::optional<ScreenPlace> Top, Bottom;
    bool IsWideLayout = false;
    bool AreScreensSwapped = false;
    // the desktop's own placements (its four built-in layouts, and a detached window's fit): PlacedTop and PlacedBottom are where the screens
    // are drawn, on fractional pixels and already shaped for the frame as shown (WideMelon's width), and the top screen shows its whole half,
    // the WideMelon camera's margin rows too, but the sun's guard band (Top and Bottom say which are shown)
    bool AsPlaced = false;
    Rect PlacedTop, PlacedBottom;
    // the background drawn while there's no frame too (the layout editor with no game running)
    bool ShowBackgroundWithoutFrame = false;

    bool operator==(const SurfaceScreens&) const = default;
};

// the background a surface shows: Backgrounds' key (an id, a file; empty for none) and how it's placed
struct SurfaceBackground
{
    std::string Key;
    BackgroundMode Mode = BackgroundMode::FitCenter;

    bool operator==(const SurfaceBackground&) const = default;
};

class Shader;

// where a layout's [screens] (not AsPlaced) are drawn for a frame [frameTopWidth] DS pixels wide with [frameMargin] rows above and below its
// top screen, [frameGuard] of each of them the sun's guard band that isn't shown, on a surface [surfaceWidth] wide, with the WideMelon
// camera's [wideWindow] (getScreensDrawing)
ScreensDrawing DrawingFor(const SurfaceScreens& screens, int frameTopWidth, int frameMargin, const MelonDSAndroid::FrameGuard& frameGuard,
    float surfaceWidth, const WideWindow* wideWindow = nullptr);

// where the screens end up, in the surface's pixels (W 0 for one not drawn): the top screen's picture, and the bottom screen's middle 256
// columns (where touches go); the places as they are for AsPlaced, but a camera window's picture inside the top screen's
struct ScreenRects
{
    Rect Top, Bottom;
};
ScreenRects RectsFor(const SurfaceScreens& screens, int frameTopWidth, int frameMargin, const MelonDSAndroid::FrameGuard& frameGuard,
    float surfaceWidth, const WideWindow* wideWindow = nullptr);
// a desktop placement's [rect] on whole pixels (its edges rounded), for GetWindowedScreenDrawing
IntRect RoundedRect(const Rect& rect);

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
    // a background decode still running is told to stop; nothing waits for it
    ~ScreenRenderer();
    ScreenRenderer(const ScreenRenderer&) = delete;
    ScreenRenderer& operator=(const ScreenRenderer&) = delete;

    // the GL objects deleted (the filter too), for before the context goes; made again on the next draw
    void Release();

    void UpdateRendererConfiguration(const RendererConfig& config);
    // where the screens go in a [surfaceW] x [surfaceH] drawable; the quads are built again only when it changes
    void UpdateScreenAreas(const SurfaceScreens& screens, int surfaceW, int surfaceH);
    // the background drawn while there's no frame (SurfaceScreens::ShowBackgroundWithoutFrame), set for each draw
    void SetBackgroundWithoutFrame(bool on) { BackgroundWithoutFrame = on; }
    // the backgrounds to read from, and the one to show (loaded on the next frame that needs it, as DSRenderer.setBackground)
    void SetBackgroundStore(Backgrounds* store) { Store = store; }
    void SetBackground(const SurfaceBackground& background);
    // runs the filter's pre-passes (the Lua effects among them, on the frame's top screen half before it's placed), clears the bound
    // framebuffer (the window, at its viewport) and draws the background and the screens; the caller sets up the state the core left behind
    // first. A background is decoded on a worker and shows once it's ready (the one before it, placed for the new size, until then);
    // [waitForBackground] waits for it instead, so a window shot always has it
    void DrawFrame(const FrameView& frame, const std::optional<TiltShift>& tiltShift, const std::optional<Bloom>& bloom,
        const std::optional<DepthOfField>& depthOfField, const std::optional<Shade>& shade, const std::optional<Sun>& sun,
        bool waitForBackground = false);

    // GPU::Comp1x: whether a frame with a screen at 1x can be drawn as it is (the no-filter shader without deband or a Lua tilt-shift, bloom
    // or vignette)
    bool CanDrawHalvesAt1x(bool isLuaEffectOn) const;
    // GPU::OutputCap: how many times their native size the screens are shown at, at most (0 before any were placed)
    int OutputScaleNeed() const { return ScreensOutputScaleNeed; }
    // whether the filter debands, as the output cap's any-ratio rule asks (GPU::OutputCapDeband)
    bool DebandsOutput() const { return Config.Deband; }

private:
    void ApplyRendererConfiguration();
    // [isWindowOnly]: only the camera's window moved, so the shadows are filled again only if a screen's drawn rect changed
    void UpdateScreenCoordinates(bool isWindowOnly = false);
    void UpdateShadowCoordinates(const Rect* top, const Rect* bottom, float topAlpha, float bottomAlpha);
    void UpdateShader();
    void EnsureGlObjects();
    void PutQuad(const Rect& rect, float leftU, float rightU, float topV, float bottomV, float alpha);
    void PutOverlayQuad(const OverlayDrawing& overlay, float topV, float bottomV, float alpha);
    const Shader* GetOverlayShader();
    int PutShadow(int offset, const Rect& rect, float alpha);
    void UseScreenQuadShader(const Shader& shader, GLuint textureId);
    void UseScreenShader(const Shader& shader, GLuint textureId, int textureWidth, int textureHeight, const FrameView& frame);
    void SetScreenVertexAttributes(const Shader& shader);
    void RenderScreenShadows();
    void LoadBackgroundIfNeeded(bool wait);
    void StartBackgroundLoad();
    void FinishBackgroundLoad();
    void RenderBackground();
    void UpdateBackgroundPosition();

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
    std::unique_ptr<Shader> OverlayShader;
    bool IsOverlayShaderTried = false;

    GLuint ScreensVbo = 0, ScreensVao = 0;
    GLuint ShadowsVbo = 0, ShadowsVao = 0;
    int ScreenIndices = 0;
    int ShadowIndices = 0;
    // the DS screen (bit 0 the top one, bit 1 the bottom one, as FrameView::OverlayScreens) of each screen quad in draw order; ScreenIndices
    // vertices of screens are followed by their Lua drawings' quads in the same order
    std::array<int, 2> ScreenQuadScreens {};
    int ScreenQuadCount = 0;
    // the screen quads: both screens and their Lua drawings (DSRenderer's MAX_SCREEN_VERTEX_FLOATS), and the shadows' 8 cells a screen
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
    MelonDSAndroid::FrameGuard FrameTopScreenGuard {};
    // the camera's window the quads were built for (none without the camera), and the drawn rects the shadows were built for
    std::optional<WideWindow> FrameWideWindow;
    std::optional<Rect> ShadowTopRect, ShadowBottomRect;
    SurfaceScreens Screens;
    float Width = 0, Height = 0;
    // AreScreensSwapped as it was when the quads were built, given to the filters so they keep the same screen's sides (the desktop's own
    // placements keep the top screen the wide one)
    bool IsBottomScreenWideInQuads = false;

    // the background: what's asked, what the texture holds (and for what surface size and mode), and whether loading it failed for that
    Backgrounds* Store = nullptr;
    SurfaceBackground Background;
    bool MustLoadBackground = false;
    bool IsBackgroundLoaded = false;
    bool IsBackgroundPositionDirty = true;
    bool IsLoadedSvg = false;
    int LoadedForWidth = 0, LoadedForHeight = 0;
    int BackgroundWidth = 0, BackgroundHeight = 0;
    GLuint BackgroundTexture = 0, BackgroundVbo = 0, BackgroundVao = 0;
    // a background being decoded on a worker thread of its own (StartBackgroundDecode): what it's for, and the job it hands its result back
    // through. Dropping it (the background changed, the GL objects or the renderer go) sets the job's Cancel and never waits; UI thread
    struct BackgroundLoad
    {
        SurfaceBackground For;
        int Width = 0, Height = 0;
        std::shared_ptr<BackgroundJob> Job;
    };
    std::optional<BackgroundLoad> Loading;
    void DropBackgroundLoad();
    // the background asked for couldn't be shown: not tried again for a new size, only when it's asked for again
    bool BackgroundFailed = false;
    bool BackgroundWithoutFrame = false;
    std::unique_ptr<Shader> BackgroundShader;
    bool IsBackgroundShaderTried = false;
    GLint MaxTextureSize = 0;
};

}
