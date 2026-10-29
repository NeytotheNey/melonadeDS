#ifndef LUADRAW_H
#define LUADRAW_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include "types.h"

namespace MelonDSAndroid
{

/**
 * What melon.draw draws into: both DS screens at twice their resolution, in the frame texture's layout (top screen in rows 0..383, bottom screen
 * from row 388), so the app draws it with the screens' own quads. Pixels are 0xAARRGGBB with premultiplied alpha. Everything outside the
 * drawings is kept at 0, so any row range can be uploaded as it is.
 *
 * Coordinates are overlay pixels of one screen (half DS pixels), and every drawing function clips. The geometry functions are static, so that the
 * engine can clip and count pixels when a draw call is recorded, before anything is drawn. Emulator thread only.
 */
class LuaOverlay
{
public:
    static constexpr int kScale = 2;
    static constexpr int kScreenWidth = 256 * kScale;
    static constexpr int kScreenHeight = 192 * kScale;
    static constexpr int kWidth = kScreenWidth;
    static constexpr int kHeight = (192 * 2 + 2) * kScale;
    static constexpr int kBottomScreenRow = (192 + 2) * kScale;
    // Lines of text are 12 rows of glyph with a row of space above and below (at text size 1)
    static constexpr int kLineHeight = 14;
    static constexpr int kMaxTextSize = 4;
    // Circles bigger than this are capped, so that their maths fits in 64 bits. It's over a million screens across
    static constexpr int64_t kMaxCircleRadius = 1ll << 29;
    // Overlay pixels per image pixel go from 1 (scale 0.5) to this (scale 8)
    static constexpr int kMaxImageScale = 8 * kScale;

    enum Screen
    {
        Top = 0,
        Bottom = 1,
    };

    // [x0, x1) x [y0, y1)
    struct Rect
    {
        int64_t x0;
        int64_t y0;
        int64_t x1;
        int64_t y1;

        bool isEmpty() const { return x0 >= x1 || y0 >= y1; }
    };

    struct TextSize
    {
        int64_t width;
        int64_t lineCount;
    };

    static Rect clipToScreen(const Rect& rect);
    static int64_t clippedArea(const Rect& rect) { Rect clipped = clipToScreen(rect); return clipped.isEmpty() ? 0 : (clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0); }
    /**
     * The 4 sides of a rectangle outline, one DS pixel thick and inside [rect], without overlaps. Sides that don't fit come out empty
     */
    static void getOutlineSides(const Rect& rect, Rect sides[4]);
    /**
     * Clips the line from (x0, y0) to (x1, y1) to the part whose pixels can touch the screen. Returns false if none can. The clipped ends fit in
     * an int
     */
    static bool clipLine(int64_t& x0, int64_t& y0, int64_t& x1, int64_t& y1);
    // Pixels drawn by a clipped line
    static int64_t linePixelCount(int64_t x0, int64_t y0, int64_t x1, int64_t y1);
    /**
     * Size of [text] in overlay pixels: the widest line, and how many lines there are. Each UTF-8 code point is one character, and the ones
     * the font doesn't have are drawn as '?'
     */
    static TextSize measureText(const char* text, size_t length);
    /**
     * The spans of row [y] (of one screen) that a circle covers, clipped to the screen: the circle's middle is at the corner ([middleX],
     * [middleY]), and it covers the pixels whose centre is at most [radius] from it, or for an outline, also more than one DS pixel less than
     * [radius]. [radius] is at most kMaxCircleRadius, and the middle at most that far outside the screen. Returns how many spans there are (up to 2)
     */
    static int getCircleRowSpans(int64_t middleX, int64_t middleY, int64_t radius, bool isFilled, int y, Rect spans[2]);
    // Pixels drawn by a circle, see getCircleRowSpans()
    static int64_t circlePixelCount(int64_t middleX, int64_t middleY, int64_t radius, bool isFilled);

    /**
     * Erases every drawing. Only goes through the rows that have any
     */
    void clear();
    // Colours are premultiplied
    void fillRect(Screen screen, const Rect& rect, melonDS::u32 color);
    // Draws a line clipped by [clipLine], one DS pixel thick
    void drawLine(Screen screen, int x0, int y0, int x1, int y1, melonDS::u32 color);
    // (x, y) is the top left of the text's box, which [background] fills. Each pixel of a glyph is [size] x [size] pixels (size 1 to
    // kMaxTextSize), and lines are kLineHeight * [size] apart
    void drawText(Screen screen, int x, int y, const char* text, size_t length, melonDS::u32 color, melonDS::u32 background, int size);
    // See getCircleRowSpans()
    void drawCircle(Screen screen, int64_t middleX, int64_t middleY, int64_t radius, bool isFilled, melonDS::u32 color);
    /**
     * Draws [width] x [height] image pixels from [pixels] (premultiplied, [stride] pixels from one row to the next), each one [scale] x [scale]
     * overlay pixels (1 to kMaxImageScale, nearest neighbour), with the top left at (x, y). [alpha] (0 to 255) multiplies the image's own
     */
    void drawImage(Screen screen, int x, int y, const melonDS::u32* pixels, int stride, int width, int height, int scale, melonDS::u32 alpha);

    bool hasDrawings() const { return rowStart[Top] < rowEnd[Top] || rowStart[Bottom] < rowEnd[Bottom]; }
    // Rows of the whole overlay (bottom screen included) that may have drawings on [screen]: [start, end), empty if start >= end
    int getRowStart(Screen screen) const { return rowStart[screen]; }
    int getRowEnd(Screen screen) const { return rowEnd[screen]; }
    // Null until something is drawn
    const melonDS::u32* getPixels() const { return pixels.empty() ? nullptr : pixels.data(); }

private:
    melonDS::u32* screenRow(Screen screen, int y);
    void addRows(Screen screen, int y0, int y1);
    void blendPixel(Screen screen, int x, int y, melonDS::u32 color);

    std::vector<melonDS::u32> pixels;
    int rowStart[2] = { 0, 0 };
    int rowEnd[2] = { 0, 0 };
};

/**
 * [color] (0xAARRGGBB) with premultiplied alpha
 */
melonDS::u32 premultiplyColor(melonDS::u32 color);

}

#endif //LUADRAW_H
