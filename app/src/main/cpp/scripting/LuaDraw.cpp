#include "LuaDraw.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "LuaFont.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// Shapes are one DS pixel thick
static const int64_t kThickness = LuaOverlay::kScale;

/**
 * round(value / 255) for value in 0..65025
 */
static inline u32 divideBy255(u32 value)
{
    value += 128;
    return (value + (value >> 8)) >> 8;
}

/**
 * Premultiplied [color] over [destination]
 */
static inline u32 blend(u32 destination, u32 color)
{
    u32 inverseAlpha = 255 - (color >> 24);
    if (inverseAlpha == 0)
        return color;

    // Two channels at a time, 16 bits each. Every channel of the result fits in a byte, since the source's channels are at most its alpha
    u32 redBlue = (destination & 0x00FF00FF) * inverseAlpha + 0x00800080;
    u32 alphaGreen = ((destination >> 8) & 0x00FF00FF) * inverseAlpha + 0x00800080;
    redBlue = ((redBlue + ((redBlue >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
    alphaGreen = (alphaGreen + ((alphaGreen >> 8) & 0x00FF00FF)) & 0xFF00FF00;
    return color + (redBlue | alphaGreen);
}

u32 premultiplyColor(u32 color)
{
    u32 alpha = color >> 24;
    if (alpha == 255)
        return color;
    if (alpha == 0)
        return 0;

    u32 red = divideBy255(((color >> 16) & 0xFF) * alpha);
    u32 green = divideBy255(((color >> 8) & 0xFF) * alpha);
    u32 blue = divideBy255((color & 0xFF) * alpha);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

/**
 * Goes through text one character at a time. Each UTF-8 code point is one character
 */
struct TextReader
{
    const unsigned char* position;
    const unsigned char* end;
    int continuationBytesLeft = 0;

    TextReader(const char* text, size_t length) : position((const unsigned char*) text), end((const unsigned char*) text + length) {}

    /**
     * The next character as the font draws it: '\n', ASCII from ' ' to '~', or '?' for anything else. -1 at the end
     */
    int next()
    {
        while (position < end)
        {
            unsigned char byte = *position++;
            if ((byte & 0xC0) == 0x80 && continuationBytesLeft > 0)
            {
                continuationBytesLeft--;
                continue;
            }

            // A continuation byte with no lead byte before it counts as a character of its own
            continuationBytesLeft = byte >= 0xF8 ? 0 : byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
            if (byte == '\n' || (byte >= ' ' && byte <= '~'))
                return byte;
            return '?';
        }
        return -1;
    }
};

static const unsigned short* getGlyph(int character)
{
    return &kLuaFont[(character - 0x10) * 16];
}

/**
 * How far the pen moves for [glyph], like melonDS's OSD: blank glyphs are 6 pixels wide, and the others get a pixel of space on each side
 */
static int getGlyphAdvance(const unsigned short* glyph)
{
    return glyph[0] ? glyph[0] + 2 : 6;
}

LuaOverlay::Rect LuaOverlay::clipToScreen(const Rect& rect)
{
    return Rect {
        std::max<int64_t>(rect.x0, 0),
        std::max<int64_t>(rect.y0, 0),
        std::min<int64_t>(rect.x1, kScreenWidth),
        std::min<int64_t>(rect.y1, kScreenHeight),
    };
}

void LuaOverlay::getOutlineSides(const Rect& rect, Rect sides[4])
{
    // Top and bottom take the full width, left and right the rows between them
    Rect top = { rect.x0, rect.y0, rect.x1, std::min(rect.y0 + kThickness, rect.y1) };
    Rect bottom = { rect.x0, std::max(rect.y1 - kThickness, top.y1), rect.x1, rect.y1 };
    Rect left = { rect.x0, top.y1, std::min(rect.x0 + kThickness, rect.x1), bottom.y0 };
    Rect right = { std::max(rect.x1 - kThickness, left.x1), top.y1, rect.x1, bottom.y0 };
    sides[0] = top;
    sides[1] = bottom;
    sides[2] = left;
    sides[3] = right;
}

bool LuaOverlay::clipLine(int64_t& x0, int64_t& y0, int64_t& x1, int64_t& y1)
{
    // A point at (x, y) covers [x, x + 2) x [y, y + 2), so points from -1 can still be seen. Liang-Barsky, in doubles: the ends are at most a
    // few trillion pixels out, which doubles hold exactly
    const double minX = -1.0, maxX = kScreenWidth - 1.0;
    const double minY = -1.0, maxY = kScreenHeight - 1.0;
    double startX = (double) x0, startY = (double) y0;
    double dx = (double) (x1 - x0), dy = (double) (y1 - y0);
    double tStart = 0.0, tEnd = 1.0;

    // Keeps the part of the line where p * t <= q
    auto clipSide = [&](double p, double q) {
        if (p == 0.0)
            return q >= 0.0;
        double t = q / p;
        if (p < 0.0)
        {
            if (t > tEnd)
                return false;
            tStart = std::max(tStart, t);
        }
        else
        {
            if (t < tStart)
                return false;
            tEnd = std::min(tEnd, t);
        }
        return true;
    };

    if (!clipSide(-dx, startX - minX) || !clipSide(dx, maxX - startX) || !clipSide(-dy, startY - minY) || !clipSide(dy, maxY - startY))
        return false;

    x0 = std::clamp<int64_t>(std::llround(startX + tStart * dx), (int64_t) minX, (int64_t) maxX);
    y0 = std::clamp<int64_t>(std::llround(startY + tStart * dy), (int64_t) minY, (int64_t) maxY);
    x1 = std::clamp<int64_t>(std::llround(startX + tEnd * dx), (int64_t) minX, (int64_t) maxX);
    y1 = std::clamp<int64_t>(std::llround(startY + tEnd * dy), (int64_t) minY, (int64_t) maxY);
    return true;
}

int64_t LuaOverlay::linePixelCount(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    // See drawLine()
    return (std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + kThickness) * kThickness;
}

LuaOverlay::TextSize LuaOverlay::measureText(const char* text, size_t length)
{
    TextReader reader(text, length);
    TextSize size = { 0, 1 };
    int64_t lineWidth = 0;
    for (int character = reader.next(); character >= 0; character = reader.next())
    {
        if (character == '\n')
        {
            size.lineCount++;
            lineWidth = 0;
            continue;
        }

        lineWidth += getGlyphAdvance(getGlyph(character));
        size.width = std::max(size.width, lineWidth);
    }
    return size;
}

void LuaOverlay::clear()
{
    for (int screen = Top; screen <= Bottom; screen++)
    {
        if (rowStart[screen] < rowEnd[screen])
            memset(&pixels[(size_t) rowStart[screen] * kWidth], 0, (size_t) (rowEnd[screen] - rowStart[screen]) * kWidth * sizeof(u32));
        rowStart[screen] = 0;
        rowEnd[screen] = 0;
    }
}

u32* LuaOverlay::screenRow(Screen screen, int y)
{
    return &pixels[(size_t) ((screen == Bottom ? kBottomScreenRow : 0) + y) * kWidth];
}

void LuaOverlay::addRows(Screen screen, int y0, int y1)
{
    // Allocated on the first drawing, so a session whose scripts never draw doesn't pay for it
    if (pixels.empty())
        pixels.assign((size_t) kWidth * kHeight, 0);

    int offset = screen == Bottom ? kBottomScreenRow : 0;
    if (rowStart[screen] >= rowEnd[screen])
    {
        rowStart[screen] = offset + y0;
        rowEnd[screen] = offset + y1;
    }
    else
    {
        rowStart[screen] = std::min(rowStart[screen], offset + y0);
        rowEnd[screen] = std::max(rowEnd[screen], offset + y1);
    }
}

void LuaOverlay::blendPixel(Screen screen, int x, int y, u32 color)
{
    if (x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight)
        return;

    u32* pixel = screenRow(screen, y) + x;
    *pixel = blend(*pixel, color);
}

void LuaOverlay::fillRect(Screen screen, const Rect& rect, u32 color)
{
    Rect clipped = clipToScreen(rect);
    if (clipped.isEmpty() || (color >> 24) == 0)
        return;

    int x0 = (int) clipped.x0, x1 = (int) clipped.x1;
    addRows(screen, (int) clipped.y0, (int) clipped.y1);
    for (int y = (int) clipped.y0; y < (int) clipped.y1; y++)
    {
        u32* row = screenRow(screen, y);
        if ((color >> 24) == 255)
        {
            std::fill(row + x0, row + x1, color);
        }
        else
        {
            for (int x = x0; x < x1; x++)
                row[x] = blend(row[x], color);
        }
    }
}

void LuaOverlay::drawLine(Screen screen, int x0, int y0, int x1, int y1, u32 color)
{
    if ((color >> 24) == 0)
        return;

    int rowsStart = std::clamp(std::min(y0, y1), 0, kScreenHeight);
    int rowsEnd = std::clamp(std::max(y0, y1) + (int) kThickness, 0, kScreenHeight);
    if (rowsStart >= rowsEnd)
        return;
    addRows(screen, rowsStart, rowsEnd);

    // Bresenham along the longer axis, with a span 2 pixels thick across it. Every pixel is drawn once, so a translucent line looks even. The
    // line goes one pixel past its end, so that it covers both ends' DS pixels
    bool isXMajor = std::abs(x1 - x0) >= std::abs(y1 - y0);
    if (isXMajor ? x0 > x1 : y0 > y1)
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    int major0 = isXMajor ? x0 : y0, major1 = isXMajor ? x1 : y1;
    int minor = isXMajor ? y0 : x0, minorEnd = isXMajor ? y1 : x1;
    int majorLength = major1 - major0;
    int minorLength = std::abs(minorEnd - minor);
    int minorStep = minorEnd >= minor ? 1 : -1;
    int error = majorLength / 2;
    for (int major = major0; major <= major1 + 1; major++)
    {
        for (int i = 0; i < kThickness; i++)
        {
            if (isXMajor)
                blendPixel(screen, major, minor + i, color);
            else
                blendPixel(screen, minor + i, major, color);
        }

        if (major < major1)
        {
            error -= minorLength;
            if (error < 0)
            {
                minor += minorStep;
                error += majorLength;
            }
        }
    }
}

void LuaOverlay::drawText(Screen screen, int x, int y, const char* text, size_t length, u32 color, u32 background)
{
    TextSize size = measureText(text, length);
    Rect box = { x, y, x + size.width, y + size.lineCount * kLineHeight };
    fillRect(screen, box, background);

    Rect clipped = clipToScreen(box);
    if (clipped.isEmpty() || (color >> 24) == 0)
        return;
    addRows(screen, (int) clipped.y0, (int) clipped.y1);

    TextReader reader(text, length);
    int64_t penX = x;
    int64_t lineY = y;
    for (int character = reader.next(); character >= 0; character = reader.next())
    {
        if (character == '\n')
        {
            penX = x;
            lineY += kLineHeight;
            // Lines only go down
            if (lineY >= kScreenHeight)
                break;
            continue;
        }

        const unsigned short* glyph = getGlyph(character);
        int advance = getGlyphAdvance(glyph);
        if (glyph[0] && lineY + kLineHeight > 0 && penX < kScreenWidth && penX + advance > 0)
        {
            int glyphX = (int) penX + 1;
            int glyphY = (int) lineY + 1;
            for (int row = 0; row < 12; row++)
            {
                unsigned short bits = glyph[4 + row];
                for (int column = 0; bits; column++, bits >>= 1)
                {
                    if (bits & 1)
                        blendPixel(screen, glyphX + column, glyphY + row, color);
                }
            }
        }
        penX += advance;
    }
}

}
