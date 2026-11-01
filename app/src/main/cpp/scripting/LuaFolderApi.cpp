#include <algorithm>
#include <cstdint>
#include <string>
#include "LuaEngineInternal.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// melon.image: images made with new() have the same size cap as the ones from the folder
static const lua_Integer kMaxImageSide = 1024;

// Registry keys (their addresses are): the modules a state has loaded, and the ones that are loading, in order. Not const, so that the compiler
// can't merge them
static char kLoadedModulesKey;
static char kRequireChainKey;

const luaL_Reg LuaFolderApi::imageFunctions[] = {
    { "load", imageLoad },
    { "new", imageNew },
    { nullptr, nullptr },
};

void LuaFolderApi::install(lua_State* L)
{
    // There's no package library: require() only loads modules from the script folder
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoadedModulesKey);
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kRequireChainKey);
    lua_pushcfunction(L, require);
    lua_setglobal(L, "require");

    // The images' metatable, hidden from getmetatable() so that scripts can't change it
    static const luaL_Reg imageMethods[] = {
        { "size", imageSize },
        { nullptr, nullptr },
    };
    luaL_newmetatable(L, kImageType);
    luaL_newlib(L, imageMethods);
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
}

/**
 * The entry named [name] in one of the folder's maps (LuaFolderContent::modules or images), or null. [folder] may be null
 */
template <typename Map>
static const typename Map::mapped_type* findInFolder(const Map* folder, const char* name, size_t length)
{
    if (!folder)
        return nullptr;

    auto entry = folder->find(std::string(name, length));
    return entry == folder->end() ? nullptr : &entry->second;
}

/**
 * Raises the string at the top of the stack as an error, with the caller's position in front like luaL_error()
 */
static int raiseWithPosition(lua_State* L)
{
    luaL_where(L, 1);
    lua_insert(L, -2);
    lua_concat(L, 2);
    return lua_error(L);
}

/**
 * require(name): loads a module from the script folder (see LuaFolderContent) once per state, and gives what it returned (true if nothing). Text
 * only, like load(). There's no package table, so the folder is the only place modules come from. Lua is built as C, so nothing here holds a
 * C++ object while a Lua call could raise an error
 */
int LuaFolderApi::require(lua_State* L)
{
    size_t nameLength;
    const char* name = luaL_checklstring(L, 1, &nameLength);
    lua_settop(L, 1);

    // Loaded already
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kLoadedModulesKey);
    lua_pushvalue(L, 1);
    if (lua_rawget(L, 2) != LUA_TNIL)
        return 1;
    lua_pop(L, 1);

    // Still loading, further up: a loop
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kRequireChainKey);
    lua_Integer chainLength = (lua_Integer) lua_rawlen(L, 3);
    for (lua_Integer i = 1; i <= chainLength; i++)
    {
        lua_rawgeti(L, 3, i);
        bool isSame = lua_rawequal(L, -1, 1);
        lua_pop(L, 1);
        if (!isSame)
            continue;

        luaL_Buffer buffer;
        luaL_buffinit(L, &buffer);
        luaL_addstring(&buffer, "loop in require: ");
        for (lua_Integer j = i; j <= chainLength; j++)
        {
            lua_rawgeti(L, 3, j);
            luaL_addvalue(&buffer);
            luaL_addstring(&buffer, " -> ");
        }
        lua_pushvalue(L, 1);
        luaL_addvalue(&buffer);
        luaL_pushresult(&buffer);
        return raiseWithPosition(L);
    }

    const LuaFolderContent* folderContent = getScript(L).engine->folderContent.get();
    const LuaFolderContent::Module* module = findInFolder(folderContent ? &folderContent->modules : nullptr, name, nameLength);
    if (!module)
    {
        // As a path: lib.util is lib/util.lua or lib/util/init.lua
        const char* path = luaL_gsub(L, name, ".", "/");
        return luaL_error(L, "module '%s' not found (looked for %s.lua and %s/init.lua in the script folder's subfolders)", name, path, path);
    }
    if (!module->source)
        return luaL_error(L, "module '%s' wasn't loaded: %s", name, module->leftOutReason.c_str());

    melonade_charge_work(L, (lua_Integer) module->source->size());
    // "=lib/util.lua", so that errors read "lib/util.lua:12: ..."
    const char* chunkName = lua_pushfstring(L, "=%s", module->path.c_str());
    if (luaL_loadbufferx(L, module->source->data(), module->source->size(), chunkName, "t") != LUA_OK)
        return lua_error(L);

    // The chunk name isn't needed anymore: its slot gets the message handler, so that a module's runtime error keeps its traceback (the
    // handler doesn't add a second one when the error goes on up)
    lua_pushcfunction(L, LuaSandbox::messageHandler);
    lua_replace(L, 4);

    // On the chain while it runs, and taken off again whether it fails or not, so that a pcall(require, ...) that failed can be tried again.
    // Like Lua's require, the module gets its name and its path
    lua_pushvalue(L, 1);
    lua_rawseti(L, 3, chainLength + 1);
    lua_pushvalue(L, 1);
    lua_pushstring(L, module->path.c_str());
    int result = lua_pcall(L, 2, 1, 4);
    lua_pushnil(L);
    lua_rawseti(L, 3, chainLength + 1);
    if (result != LUA_OK)
        return lua_error(L);

    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -2);
    lua_rawset(L, 2);
    return 1;
}

static LuaImage* pushImage(lua_State* L, int width, int height, const u32* pixels, size_t pixelBytes)
{
    auto* image = static_cast<LuaImage*>(lua_newuserdatauv(L, sizeof(LuaImage) + pixelBytes, 0));
    image->width = width;
    image->height = height;
    image->pixels = pixels ? pixels : reinterpret_cast<const u32*>(image + 1);
    luaL_setmetatable(L, kImageType);
    return image;
}

/**
 * melon.image.load(path): an image from the script folder by its path in it ("img/hud.png"), or nil and why not. Its pixels stay the folder's,
 * outside the script's memory
 */
int LuaFolderApi::imageLoad(lua_State* L)
{
    size_t pathLength;
    const char* path = luaL_checklstring(L, 1, &pathLength);
    const LuaFolderContent* folderContent = getScript(L).engine->folderContent.get();
    const LuaFolderContent::Image* image = findInFolder(folderContent ? &folderContent->images : nullptr, path, pathLength);

    if (!image)
    {
        lua_pushnil(L);
        lua_pushfstring(L, "image '%s' not found in the script folder", path);
        return 2;
    }
    if (image->pixels.empty())
    {
        lua_pushnil(L);
        lua_pushfstring(L, "image '%s' wasn't loaded: %s", path, image->leftOutReason.c_str());
        return 2;
    }

    pushImage(L, image->width, image->height, image->pixels.data(), 0);
    return 1;
}

/**
 * melon.image.new(w, h, argb): an image made from a string of 4 bytes per pixel, each one 0xAARRGGBB little-endian (so B, G, R, A). It lives in
 * the script's memory
 */
int LuaFolderApi::imageNew(lua_State* L)
{
    lua_Integer width = luaL_checkinteger(L, 1);
    lua_Integer height = luaL_checkinteger(L, 2);
    size_t length;
    const char* data = luaL_checklstring(L, 3, &length);
    luaL_argcheck(L, width >= 1 && width <= kMaxImageSide, 1, "width must be from 1 to 1024");
    luaL_argcheck(L, height >= 1 && height <= kMaxImageSide, 2, "height must be from 1 to 1024");

    size_t pixelCount = (size_t) (width * height);
    if (length != pixelCount * 4)
    {
        return luaL_error(L, "a %dx%d image needs %d bytes (4 per pixel), not %d", (int) width, (int) height, (int) (pixelCount * 4),
                          (int) std::min<size_t>(length, INT32_MAX));
    }

    // Converting goes through every pixel. The userdata's memory is charged when it's allocated
    melonade_charge_work(L, (lua_Integer) pixelCount);
    LuaImage* image = pushImage(L, (int) width, (int) height, nullptr, pixelCount * sizeof(u32));
    auto* pixels = const_cast<u32*>(image->pixels);
    auto* bytes = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < pixelCount; i++, bytes += 4)
        pixels[i] = premultiplyColor((u32) bytes[0] | ((u32) bytes[1] << 8) | ((u32) bytes[2] << 16) | ((u32) bytes[3] << 24));
    return 1;
}

int LuaFolderApi::imageSize(lua_State* L)
{
    auto* image = static_cast<const LuaImage*>(luaL_checkudata(L, 1, kImageType));
    lua_pushinteger(L, image->width);
    lua_pushinteger(L, image->height);
    return 2;
}

}
