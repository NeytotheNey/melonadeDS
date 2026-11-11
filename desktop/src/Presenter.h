// Draws a frame's two screens into the window, from the renderer's own output texture (OpenGL,
// Compute) or the software framebuffer, and reads frames back for screenshots
#pragma once

#include <vector>

#include "PlatformOGL.h"
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

    // where the bottom screen was last drawn, in window pixels, for touch
    const Rect& BottomRect() const { return Bottom; }

    // the last drawn frame as RGBA rows, top row first, at the renderer's internal size
    bool ReadFrame(std::vector<melonDS::u8>& pixels, int& width, int& height);

private:
    GLuint BindFrame(melonDS::NDS& nds);

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
