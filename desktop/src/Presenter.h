// Draws the screens of a frame from the shared native code's FrameQueue (the frame texture MelonInstance rendered or uploaded into, in the
// layout the app's DSRenderer reads) into the current framebuffer, and reads frames back for screenshots. It draws into whatever window
// is current, so one presenter (one GL context) can draw several windows, each with its own screens and rects
#pragma once

#include <vector>

#include "PlatformOGL.h"
#include "types.h"

struct Frame;

namespace Frontend
{

// the screens a window shows, and how: both (vertical or horizontal) or one of them
enum class Layout { Vertical, Horizontal, TopOnly, BottomOnly, Count };

struct Rect
{
    float X = 0, Y = 0, W = 0, H = 0;
};

// where Draw put each screen, in the window's drawable pixels (W 0 for a screen it didn't draw)
struct ScreenRects
{
    Rect Top, Bottom;
};

// what Draw and ReadFrame need of a Frame (renderer/FrameQueue.h)
struct FrameView
{
    GLuint Texture = 0;
    int Width = 0, Height = 0;
    // the top screen's width in DS pixels (wider than 256 with WideMelon), and its margin rows above and below (WideMelon camera stage 2)
    int TopWidth = 256, TopMargin = 0;
    // GPU::Comp1x: the halves the compositor drew at 1x (bit 0 the top one), the frame's scale and the scale they were drawn at
    int HalvesAt1x = 0, Scale = 1, HalfScale = 1;

    static FrameView Of(const Frame& frame);
};

class Presenter
{
public:
    bool Init();
    void Free();

    // clears the current window's drawable (windowW x windowH) to black and draws [layout]'s screens of [frame] into it, fitted and
    // centered; the GL state it needs is set first, since the core leaves its own behind
    ScreenRects Draw(const FrameView& frame, int windowW, int windowH, Layout layout, bool linear);

    // a frame's texture as RGBA rows, top row first, at the renderer's internal size. a half the compositor drew at 1x (GPU::Comp1x) is
    // upscaled into its rows, as Draw shows it
    bool ReadFrame(const FrameView& frame, std::vector<melonDS::u8>& pixels, int& width, int& height);

private:
    // draws a frame with a half at 1x without linear filtering, and expands one into the full frame
    // (ExpandTexture, the frame's size) for it. made the first time such a frame is drawn
    bool PrepareHalvesAt1x();
    GLuint ExpandHalvesAt1x(const FrameView& frame);
    GLuint HalvesAt1xProgram = 0;
    GLint HalvesAt1xTexLoc = -1;
    GLint HalvesAt1xLoc = -1;
    GLuint ExpandProgram = 0;
    GLint ExpandTexLoc = -1;
    GLint ExpandHalvesLoc = -1;
    GLuint ExpandTexture = 0;
    GLuint ExpandFB = 0;
    int ExpandW = 0, ExpandH = 0;
    bool HalvesAt1xFailed = false;
    // the 1x program's coordinate textures (see kHalvesAt1xFS) on units 1 and 2, for [texture]'s size,
    // made again when it changes; leaves [texture] bound on unit 0, which is active
    void BindCoordTextures(GLuint texture);
    GLuint CoordTex[2] {};
    int CoordW = 0, CoordH = 0;

    GLuint Program = 0;
    GLuint VertexArray = 0;
    GLuint VertexBuffer = 0;
    GLuint Sampler[2] = {};
    GLuint ReadFB = 0;
    GLint TexLoc = -1;
};

// writes RGBA rows (top row first) as a 24-bit BMP
bool WriteBMP(const char* path, const std::vector<melonDS::u8>& pixels, int width, int height);

}
