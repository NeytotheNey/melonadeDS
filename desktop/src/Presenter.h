// Draws the screens of a frame from the shared native code's FrameQueue (the frame texture MelonInstance rendered or uploaded into, in the
// layout the app's DSRenderer reads) into the current framebuffer, and reads frames back for screenshots. It draws into whatever window
// is current, so one presenter (one GL context) can draw several windows, each with its own screens and rects. Each window is a surface with
// its own ScreenRenderer (the app's DSRenderer, one per surface), all with the same video filter
#pragma once

#include <optional>
#include <vector>

#include "PlatformOGL.h"
#include "render/PresentFrame.h"
#include "render/ScreenRenderer.h"
#include "types.h"

namespace Frontend
{

// the screens a window shows, and how: both (vertical or horizontal) or one of them
enum class Layout { Vertical, Horizontal, TopOnly, BottomOnly, Count };

// how the screens fill the window: as big as they fit at their aspect (black bars on two sides), the biggest whole multiple of their size
// that fits (bars all around, sharp pixels; the aspect fit when not even 1x fits), or the whole window whatever the aspect
enum class Fit { Aspect, Integer, Stretch, Count };

// where Draw put each screen, in the window's drawable pixels (W 0 for a screen it didn't draw)
struct ScreenRects
{
    Rect Top, Bottom;
};

class Presenter
{
public:
    // the windows that draw (the main one, and the second one when detached)
    static constexpr int SURFACE_COUNT = 2;

    bool Init();
    void Free();

    // the video filter, deband, DS screen colors and screen shadow every window draws with, from the next Draw on; each surface has it at
    // once, so what CanDrawHalvesAt1x answers before that Draw (the first frame of a game, a change) is already the new filter's
    void SetRendererConfig(const RendererConfig& config)
    {
        Config = config;
        for (ScreenRenderer& surface : Surfaces)
            surface.UpdateRendererConfiguration(config);
    }
    const RendererConfig& GetRendererConfig() const { return Config; }
    // the Lua tilt-shift over the top screen (none by default; --tilt-shift for tests until the scripts drive it)
    void SetTiltShift(const std::optional<TiltShift>& tiltShift) { CurrentTiltShift = tiltShift; }
    // the Lua bloom and vignette over the top screen (none by default; --bloom for tests until the scripts drive them)
    void SetBloom(const std::optional<Bloom>& bloom) { CurrentBloom = bloom; }
    // the Lua depth of field over the top screen (none by default; --dof for tests), drawn on frames that have depth
    void SetDepthOfField(const std::optional<DepthOfField>& depthOfField) { CurrentDepthOfField = depthOfField; }
    // the Lua shading over the top screen (none by default; --shade for tests), drawn on frames that have depth
    void SetShade(const std::optional<Shade>& shade) { CurrentShade = shade; }
    // the Lua sun shadows over the top screen (none by default; --sun for tests), drawn on frames that have depth and a depth camera
    void SetSun(const std::optional<Sun>& sun) { CurrentSun = sun; }

    // where [layout]'s screens of [frame] go in a drawable windowW x windowH with [fit], centered. [swap] trades the screens' places: the
    // bottom screen first (above or left), or the other screen for TopOnly and BottomOnly. Only the frame's top screen size is read
    static ScreenRects Place(const FrameView& frame, int windowW, int windowH, Layout layout, Fit fit = Fit::Aspect, bool swap = false);

    // clears the current window's drawable (windowW x windowH) to black and draws [layout]'s screens of [frame] into it where Place puts
    // them, through [surface]'s ScreenRenderer (0: the main window, 1: the second one); the GL state it needs is set first, since the core
    // leaves its own behind
    ScreenRects Draw(const FrameView& frame, int windowW, int windowH, Layout layout, Fit fit = Fit::Aspect, bool swap = false, int surface = 0);

    // over the first [surfaces] windows, as the app's FrameRenderCoordinator asks its surfaces after presenting: whether a screen at 1x can be
    // drawn as it is (GPU::Comp1x), how big the screens are shown (GPU::OutputCap's need) and whether the output is debanded
    bool CanDrawHalvesAt1x(int surfaces) const;
    int OutputScaleNeed(int surfaces) const;
    bool DebandsOutput() const { return Config.Deband; }

    // a frame's texture as RGBA rows, top row first, at the renderer's internal size. a half the compositor drew at 1x (GPU::Comp1x) is
    // upscaled into its rows, as the 1x shader shows it
    bool ReadFrame(const FrameView& frame, std::vector<melonDS::u8>& pixels, int& width, int& height);

    // the touch cursor at x,y (the current window's drawable pixels, y down), [radius] pixels, filled while [pressed]
    void DrawCursor(float x, float y, float radius, bool pressed, int windowW, int windowH);

private:
    RendererConfig Config;
    std::optional<TiltShift> CurrentTiltShift;
    std::optional<Bloom> CurrentBloom;
    std::optional<DepthOfField> CurrentDepthOfField;
    std::optional<Shade> CurrentShade;
    std::optional<Sun> CurrentSun;
    ScreenRenderer Surfaces[SURFACE_COUNT];

    GLuint CursorProgram = 0;
    GLint CursorRectLoc = -1, CursorStateLoc = -1;
    bool CursorFailed = false;
    // the cursor's vertex array (no attributes: its quad comes from gl_VertexID)
    GLuint VertexArray = 0;
    GLuint ReadFB = 0;
};

// writes RGBA rows (top row first) as a 24-bit BMP
bool WriteBMP(const char* path, const std::vector<melonDS::u8>& pixels, int width, int height);

}
