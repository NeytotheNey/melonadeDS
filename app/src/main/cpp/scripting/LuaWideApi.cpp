#include <algorithm>
#include <cmath>
#include "LuaEngineInternal.h"

namespace MelonDSAndroid
{

// melon.wide: what the native clamps to before the app clamps to the game's real range. The widest window is 768 wide (WideMelon's
// largest width), 3 times the DS's 256, and a window can sit at most (768 - 256) / 2 DS pixels off center; up and down it's at most the
// camera's largest margin, 128 rows, but the same cap keeps it simple
static const lua_Number kMaxWideZoom = 3.0;
static const lua_Number kMaxWidePan = 256.0;

/**
 * Whether argument [index] is a number (put in [value]) rather than nil; anything else or NaN is an error, and so is no argument at all unless
 * [isOptional], so that set_zoom() can't be a typo for set_zoom(z)
 */
static bool checkNumberOrNil(lua_State* L, int index, lua_Number& value, bool isOptional = false)
{
    // lua_isnil() is false for no argument, which lua_tonumberx() then refuses
    if (isOptional ? lua_isnoneornil(L, index) : lua_isnil(L, index))
        return false;

    int isNumber;
    value = lua_tonumberx(L, index, &isNumber);
    if (!isNumber)
        luaL_typeerror(L, index, "number or nil");
    if (std::isnan(value))
        luaL_argerror(L, index, "number expected, got NaN");
    return true;
}

int LuaWideApi::camera(lua_State* L)
{
    LuaEngine* engine = getScript(L).engine;
    lua_pushnumber(L, engine->wideCamera[0].load(std::memory_order_relaxed));
    lua_pushnumber(L, engine->wideCamera[1].load(std::memory_order_relaxed));
    // Last, so stage 1's x, zoom = camera() still works
    lua_pushnumber(L, engine->wideCamera[2].load(std::memory_order_relaxed));
    return 3;
}

int LuaWideApi::setZoom(lua_State* L)
{
    LuaScript& script = getScript(L);
    lua_Number zoom;
    bool hasZoom = checkNumberOrNil(L, 1, zoom);
    if (!script.engine->isUnloading)
        script.wideCamera.zoom = hasZoom ? (float) std::clamp<lua_Number>(zoom, 1.0, kMaxWideZoom) : -1.0f;
    return 0;
}

int LuaWideApi::pan(lua_State* L)
{
    LuaScript& script = getScript(L);
    lua_Number offset;
    bool hasPan = checkNumberOrNil(L, 1, offset);
    // y is optional: left out or nil, the player keeps that axis
    lua_Number offsetY;
    bool hasPanY = checkNumberOrNil(L, 2, offsetY, true);
    if (!script.engine->isUnloading)
    {
        script.wideCamera.hasPan = hasPan;
        script.wideCamera.pan = hasPan ? (float) std::clamp<lua_Number>(offset, -kMaxWidePan, kMaxWidePan) : 0.0f;
        script.wideCamera.hasPanY = hasPanY;
        script.wideCamera.panY = hasPanY ? (float) std::clamp<lua_Number>(offsetY, -kMaxWidePan, kMaxWidePan) : 0.0f;
    }
    return 0;
}

int LuaWideApi::holdNative(lua_State* L)
{
    LuaScript& script = getScript(L);
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    if (!script.engine->isUnloading)
        script.wideCamera.holdNative = lua_toboolean(L, 1) != 0;
    return 0;
}

const luaL_Reg LuaWideApi::functions[] = {
    { "camera", camera },
    { "set_zoom", setZoom },
    { "pan", pan },
    { "hold_native", holdNative },
    { nullptr, nullptr },
};

}
