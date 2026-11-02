#include <algorithm>
#include <cmath>
#include "LuaEngineInternal.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// melon.draw: each recorded call costs this much work, plus a step per pixel it draws (after clipping) and, for text, per byte
static const lua_Integer kDrawCallCost = 32;
// An image pixel is read from the image, and often scaled by alpha and blended, so it costs more than a filled rectangle's
static const lua_Integer kImageStepsPerPixel = 2;
static const size_t kMaxDrawCommands = 4096;
static const char* const kDrawLimitError = "drew too much in one frame (over 4096 draw calls)";
// Text a script can draw in one frame, capped on its own since it's kept outside the Lua memory limit
static const size_t kMaxDrawTextBytes = 64 * 1024;
static const char* const kDrawTextLimitError = "drew too much text in one frame (over 64 KB)";
// Coordinates are capped this far out, in overlay pixels, so that clipping and pixel counts can't overflow
static const double kMaxCoordinate = (double) (1ll << 40);
// melon.draw.image: src rectangles are capped this far out (in image pixels) so that their math can't overflow, which only changes
// rectangles that couldn't be seen anyway
static const lua_Integer kMaxImageOffset = 1 << 30;

void LuaDrawApi::install(lua_State* L, LuaScript& script)
{
    lua_newtable(L);
    script.drawnImagesRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

void LuaDrawApi::clearCommands(LuaScript& script)
{
    script.drawCommands.clear();
    script.drawText.clear();

    // Lets this frame's images be collected again, which can't raise an error since setting existing fields to nil never allocates
    if (script.L && script.drawnImageCount > 0)
    {
        lua_rawgeti(script.L, LUA_REGISTRYINDEX, script.drawnImagesRef);
        for (int i = 1; i <= script.drawnImageCount; i++)
        {
            lua_pushnil(script.L);
            lua_rawseti(script.L, -2, i);
        }
        lua_pop(script.L, 1);
    }
    script.drawnImageCount = 0;
}

void LuaDrawApi::rasterize(const LuaScript& script, LuaOverlay& overlay)
{
    for (const LuaDrawCommand& command : script.drawCommands)
    {
        switch (command.type)
        {
            case LuaDrawCommand::Type::Rect:
                overlay.fillRect(command.screen, { command.x0, command.y0, command.x1, command.y1 }, command.color);
                break;
            case LuaDrawCommand::Type::Outline:
            {
                LuaOverlay::Rect sides[4];
                LuaOverlay::getOutlineSides({ command.x0, command.y0, command.x1, command.y1 }, sides);
                for (const LuaOverlay::Rect& side : sides)
                    overlay.fillRect(command.screen, side, command.color);
                break;
            }
            case LuaDrawCommand::Type::Line:
                overlay.drawLine(command.screen, command.x0, command.y0, command.x1, command.y1, command.color);
                break;
            case LuaDrawCommand::Type::Text:
                overlay.drawText(command.screen, command.x0, command.y0, script.drawText.data() + command.textOffset, command.textLength, command.color, command.background, command.x1);
                break;
            case LuaDrawCommand::Type::Circle:
                overlay.drawCircle(command.screen, command.x0, command.y0, command.x1, command.y1 != 0, command.color);
                break;
            case LuaDrawCommand::Type::Image:
                overlay.drawImage(command.screen, command.x0, command.y0, command.imagePixels, command.imageStride, command.imageWidth, command.imageHeight, command.x1, command.color);
                break;
        }
    }
}

/**
 * A coordinate in DS pixels as overlay pixels, rounded to the nearest half DS pixel, with errors pointing at [argument]
 */
static int64_t toOverlayCoordinate(lua_State* L, lua_Number value, int argument)
{
    if (std::isnan(value))
        luaL_argerror(L, argument, "number expected, got NaN");
    return (int64_t) std::floor(std::clamp<lua_Number>(value * LuaOverlay::kScale, -kMaxCoordinate, kMaxCoordinate) + 0.5);
}

static int64_t checkCoordinate(lua_State* L, int argument)
{
    return toOverlayCoordinate(L, luaL_checknumber(L, argument), argument);
}

static LuaOverlay::Screen checkScreen(lua_State* L, int argument)
{
    static const char* const screenNames[] = { "top", "bottom", nullptr };
    return luaL_checkoption(L, argument, nullptr, screenNames) == 0 ? LuaOverlay::Top : LuaOverlay::Bottom;
}

/**
 * 0xAARRGGBB, masked to 32 bits (so -1 is opaque white) and premultiplied, with no default alpha (0xFF0000 is transparent)
 */
static u32 checkColor(lua_State* L, int argument)
{
    return premultiplyColor((u32) luaL_checkinteger(L, argument));
}

/**
 * Records a draw command of [pixelCount] pixels for the running script and charges its work, or returns null outside on_frame() and
 * on_frame_end() or when [pixelCount] is 0; (x0, y0, x1, y1) go in the command's geometry fields and must fit in an int
 */
LuaDrawCommand* LuaDrawApi::addDrawCommand(lua_State* L, LuaDrawCommand::Type type, LuaOverlay::Screen screen, int64_t x0, int64_t y0, int64_t x1, int64_t y1, u32 color, int64_t pixelCount, size_t textLength)
{
    LuaScript& script = getScript(L);
    if (!script.engine->isDrawingAllowed || pixelCount == 0)
        return nullptr;

    if (script.drawCommands.size() >= kMaxDrawCommands)
        LuaSandbox::raiseLimitError(L, script, kDrawLimitError);
    if (textLength > kMaxDrawTextBytes - script.drawText.size())
        LuaSandbox::raiseLimitError(L, script, kDrawTextLimitError);

    melonade_charge_work(L, kDrawCallCost + pixelCount);
    LuaDrawCommand& command = script.drawCommands.emplace_back();
    command.type = type;
    command.screen = screen;
    command.x0 = (int) x0;
    command.y0 = (int) y0;
    command.x1 = (int) x1;
    command.y1 = (int) y1;
    command.color = color;
    return &command;
}

int LuaDrawApi::pixel(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t x = checkCoordinate(L, 2);
    int64_t y = checkCoordinate(L, 3);
    u32 color = checkColor(L, 4);

    LuaOverlay::Rect rect = LuaOverlay::clipToScreen({ x, y, x + LuaOverlay::kScale, y + LuaOverlay::kScale });
    if (color == 0 || rect.isEmpty())
        return 0;

    addDrawCommand(L, LuaDrawCommand::Type::Rect, screen, rect.x0, rect.y0, rect.x1, rect.y1, color, (rect.x1 - rect.x0) * (rect.y1 - rect.y0));
    return 0;
}

int LuaDrawApi::line(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t x0 = checkCoordinate(L, 2);
    int64_t y0 = checkCoordinate(L, 3);
    int64_t x1 = checkCoordinate(L, 4);
    int64_t y1 = checkCoordinate(L, 5);
    u32 color = checkColor(L, 6);

    if (color == 0 || !LuaOverlay::clipLine(x0, y0, x1, y1))
        return 0;

    addDrawCommand(L, LuaDrawCommand::Type::Line, screen, x0, y0, x1, y1, color, LuaOverlay::linePixelCount(x0, y0, x1, y1));
    return 0;
}

int LuaDrawApi::rect(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number width = luaL_checknumber(L, 4);
    lua_Number height = luaL_checknumber(L, 5);
    u32 color = checkColor(L, 6);
    bool isFilled = lua_toboolean(L, 7) != 0;

    // The far edges are rounded on their own, so that rectangles side by side meet
    LuaOverlay::Rect rect = {
        toOverlayCoordinate(L, x, 2),
        toOverlayCoordinate(L, y, 3),
        toOverlayCoordinate(L, x + width, 4),
        toOverlayCoordinate(L, y + height, 5),
    };
    if (color == 0 || rect.isEmpty())
        return 0;

    if (isFilled)
    {
        LuaOverlay::Rect clipped = LuaOverlay::clipToScreen(rect);
        addDrawCommand(L, LuaDrawCommand::Type::Rect, screen, clipped.x0, clipped.y0, clipped.x1, clipped.y1, color, LuaOverlay::clippedArea(rect));
    }
    else
    {
        LuaOverlay::Rect sides[4];
        LuaOverlay::getOutlineSides(rect, sides);
        int64_t pixelCount = 0;
        for (const LuaOverlay::Rect& side : sides)
            pixelCount += LuaOverlay::clippedArea(side);

        // Capped to just outside the screen: the sides that can be seen stay where they are
        const int64_t margin = LuaOverlay::kScale;
        addDrawCommand(L, LuaDrawCommand::Type::Outline, screen,
                       std::clamp<int64_t>(rect.x0, -margin, LuaOverlay::kScreenWidth + margin),
                       std::clamp<int64_t>(rect.y0, -margin, LuaOverlay::kScreenHeight + margin),
                       std::clamp<int64_t>(rect.x1, -margin, LuaOverlay::kScreenWidth + margin),
                       std::clamp<int64_t>(rect.y1, -margin, LuaOverlay::kScreenHeight + margin),
                       color, pixelCount);
    }
    return 0;
}

int LuaDrawApi::text(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t x = checkCoordinate(L, 2);
    int64_t y = checkCoordinate(L, 3);
    size_t length;
    const char* text = luaL_checklstring(L, 4, &length);
    u32 color = checkColor(L, 5);
    u32 background = lua_isnoneornil(L, 6) ? 0 : checkColor(L, 6);
    lua_Integer textSize = luaL_optinteger(L, 7, 1);
    luaL_argcheck(L, textSize >= 1 && textSize <= LuaOverlay::kMaxTextSize, 7, "size must be 1, 2, 3 or 4");

    // Laying the text out goes through every byte, even where nothing is drawn
    melonade_charge_work(L, (lua_Integer) length);
    LuaOverlay::TextSize size = LuaOverlay::measureText(text, length);
    int64_t width = size.width * textSize;

    if (color != 0 || background != 0)
    {
        // The box's area covers the glyphs as well as the background, and a box that can be seen starts at an int (text is capped per frame)
        LuaOverlay::Rect box = { x, y, x + width, y + size.lineCount * LuaOverlay::kLineHeight * textSize };
        if (LuaDrawCommand* command = addDrawCommand(L, LuaDrawCommand::Type::Text, screen, x, y, textSize, 0, color, LuaOverlay::clippedArea(box), length))
        {
            LuaScript& script = getScript(L);
            command->background = background;
            command->textOffset = script.drawText.size();
            command->textLength = length;
            script.drawText.append(text, length);
        }
    }

    // In DS pixels
    if (width % LuaOverlay::kScale == 0)
        lua_pushinteger(L, width / LuaOverlay::kScale);
    else
        lua_pushnumber(L, (lua_Number) width / LuaOverlay::kScale);
    return 1;
}

int LuaDrawApi::circle(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t middleX = checkCoordinate(L, 2);
    int64_t middleY = checkCoordinate(L, 3);
    int64_t radius = checkCoordinate(L, 4);
    u32 color = checkColor(L, 5);
    bool isFilled = lua_toboolean(L, 6) != 0;

    // Nothing to draw for a radius of 0 or less, or a square around the circle that is off-screen, which also keeps the middle close enough to
    // the screen for getCircleRowSpans()
    radius = std::min(radius, LuaOverlay::kMaxCircleRadius);
    if (color == 0 || radius <= 0 || LuaOverlay::clippedArea({ middleX - radius, middleY - radius, middleX + radius, middleY + radius }) == 0)
        return 0;
    if (!getScript(L).engine->isDrawingAllowed)
        return 0;

    // Counting the pixels and drawing them each go through every row that can be seen, even where the circle has no pixels on it (a ring
    // around the screen, say), so the rows are charged twice before counting, on top of addDrawCommand()'s charge for the pixels
    int64_t rowCount = std::clamp<int64_t>(middleY + radius, 0, LuaOverlay::kScreenHeight) - std::clamp<int64_t>(middleY - radius, 0, LuaOverlay::kScreenHeight);
    melonade_charge_work(L, kDrawCallCost + 2 * rowCount);

    addDrawCommand(L, LuaDrawCommand::Type::Circle, screen, middleX, middleY, radius, isFilled ? 1 : 0, color, LuaOverlay::circlePixelCount(middleX, middleY, radius, isFilled));
    return 0;
}

int LuaDrawApi::image(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t x = checkCoordinate(L, 2);
    int64_t y = checkCoordinate(L, 3);
    auto* image = static_cast<const LuaImage*>(luaL_checkudata(L, 4, kImageType));

    // Overlay pixels per image pixel
    int scale = LuaOverlay::kScale;
    int64_t sourceX0 = 0, sourceY0 = 0, sourceX1 = image->width, sourceY1 = image->height;
    u32 alpha = 255;
    if (!lua_isnoneornil(L, 5))
    {
        luaL_checktype(L, 5, LUA_TTABLE);

        if (lua_getfield(L, 5, "scale") != LUA_TNIL)
        {
            int isNumber;
            lua_Number doubledScale = lua_tonumberx(L, -1, &isNumber) * LuaOverlay::kScale;
            if (!isNumber || !(doubledScale >= 1 && doubledScale <= LuaOverlay::kMaxImageScale) || doubledScale != std::floor(doubledScale))
                return luaL_error(L, "scale must be a multiple of 0.5 from 0.5 to 8");
            scale = (int) doubledScale;
        }
        lua_pop(L, 1);

        if (lua_getfield(L, 5, "src") != LUA_TNIL)
        {
            if (!lua_istable(L, -1))
                return luaL_error(L, "src must be a table {x, y, w, h}");

            lua_Integer values[4];
            for (int i = 0; i < 4; i++)
            {
                lua_geti(L, -1, i + 1);
                int isInteger;
                lua_Integer value = lua_tointegerx(L, -1, &isInteger);
                if (!isInteger)
                    return luaL_error(L, "src must be a table {x, y, w, h} of whole numbers");
                values[i] = std::clamp<lua_Integer>(value, -kMaxImageOffset, kMaxImageOffset);
                lua_pop(L, 1);
            }
            // The parts of src outside the image are transparent
            sourceX0 = values[0];
            sourceY0 = values[1];
            sourceX1 = values[0] + std::max<lua_Integer>(values[2], 0);
            sourceY1 = values[1] + std::max<lua_Integer>(values[3], 0);
        }
        lua_pop(L, 1);

        if (lua_getfield(L, 5, "alpha") != LUA_TNIL)
        {
            int isNumber;
            lua_Number value = lua_tonumberx(L, -1, &isNumber);
            if (!isNumber || std::isnan(value))
                return luaL_error(L, "alpha must be a number from 0 to 1");
            alpha = (u32) std::lround(std::clamp<lua_Number>(value, 0.0, 1.0) * 255);
        }
        lua_pop(L, 1);
    }

    // The part of src that is in the image, and where it lands
    int64_t clippedX0 = std::max<int64_t>(sourceX0, 0), clippedY0 = std::max<int64_t>(sourceY0, 0);
    int64_t clippedX1 = std::min<int64_t>(sourceX1, image->width), clippedY1 = std::min<int64_t>(sourceY1, image->height);
    if (alpha == 0 || clippedX0 >= clippedX1 || clippedY0 >= clippedY1)
        return 0;

    int64_t destinationX = x + (clippedX0 - sourceX0) * scale;
    int64_t destinationY = y + (clippedY0 - sourceY0) * scale;
    LuaOverlay::Rect destination = {
        destinationX,
        destinationY,
        destinationX + (clippedX1 - clippedX0) * scale,
        destinationY + (clippedY1 - clippedY0) * scale,
    };
    int64_t pixelCount = LuaOverlay::clippedArea(destination);
    LuaScript& script = getScript(L);
    if (pixelCount == 0 || !script.engine->isDrawingAllowed)
        return 0;

    // Anchored before the command is added, so that no command can point at an image that could be collected
    lua_rawgeti(L, LUA_REGISTRYINDEX, script.drawnImagesRef);
    lua_pushvalue(L, 4);
    lua_rawseti(L, -2, script.drawnImageCount + 1);
    script.drawnImageCount++;
    lua_pop(L, 1);

    // The destination can be seen, so its top left is at most an image's width (1024 * kMaxImageScale) outside the screen, and fits in an int
    if (LuaDrawCommand* command = addDrawCommand(L, LuaDrawCommand::Type::Image, screen, destinationX, destinationY, scale, 0, alpha, pixelCount * kImageStepsPerPixel))
    {
        command->imagePixels = image->pixels + clippedY0 * image->width + clippedX0;
        command->imageStride = image->width;
        command->imageWidth = (int) (clippedX1 - clippedX0);
        command->imageHeight = (int) (clippedY1 - clippedY0);
    }
    return 0;
}

const luaL_Reg LuaDrawApi::functions[] = {
    { "pixel", pixel },
    { "line", line },
    { "rect", rect },
    { "text", text },
    { "circle", circle },
    { "image", image },
    { nullptr, nullptr },
};

}
