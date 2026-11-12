// Draws a frame's two screens into the window, from the renderer's own output texture (OpenGL,
// Compute) or the software framebuffer, and reads frames back for screenshots
#pragma once

#include <vector>

#include "PlatformOGL.h"
#include "GPU3D.h"
#include "types.h"

namespace melonDS { class NDS; }

namespace Frontend
{

enum class Layout { Vertical, Horizontal, TopOnly, BottomOnly, Count };

struct Rect
{
    float X = 0, Y = 0, W = 0, H = 0;
};

class Presenter
{
public:
    bool Init();
    void Free();

    // topWidth is the top screen's width in DS pixels (wider than 256 with WideMelon)
    void Draw(melonDS::NDS& nds, int windowW, int windowH, int topWidth, Layout layout, bool linear);

    // GPU::Comp1x: forgets the output layouts, when a new renderer (with textures of its own, which it
    // draws into before they're shown) replaces the old one
    void ResetLayouts();

    // where the bottom screen was last drawn, in window pixels, for touch
    const Rect& BottomRect() const { return Bottom; }

    // the last drawn frame as RGBA rows, top row first, at the renderer's internal size. a half the
    // compositor drew at 1x (GPU::Comp1x) is upscaled into its rows, as Draw shows it
    bool ReadFrame(std::vector<melonDS::u8>& pixels, int& width, int& height);

private:
    GLuint BindFrame(melonDS::NDS& nds);

    // GPU::Comp1x: what the renderer last drew into each of its output buffers (OutputLayout), kept
    // until it draws into that one again (or ResetLayouts), and the drawn frame's
    melonDS::OutputLayout Layouts[2] {};
    melonDS::OutputLayout LastLayout {};
    // draws a frame with a half at 1x without linear filtering, and expands one into the full frame
    // (ExpandTexture, the frame's size) for it. made the first time such a frame is drawn
    bool PrepareHalvesAt1x();
    GLuint ExpandHalvesAt1x(GLuint texture);
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
    GLuint SoftTexture = 0;
    GLuint ReadFB = 0;
    GLuint LastTexture = 0;
    GLint TexLoc = -1;
    Rect Top, Bottom;
};

// writes RGBA rows (top row first) as a 24-bit BMP
bool WriteBMP(const char* path, const std::vector<melonDS::u8>& pixels, int width, int height);

}
