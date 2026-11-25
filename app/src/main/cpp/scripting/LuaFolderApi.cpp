#include <algorithm>
#include <cstdint>
#include <string>
#include "LuaEngineInternal.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// melon.image: images made with new() have the same size cap as the ones from the folder
static const lua_Integer kMaxImageSide = 1024;

// Registry keys (their addresses are) for the modules a state has loaded and the ones that are loading, in order; not const, so that the
// compiler can't merge them
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
 * The entry named [name] in one of the folder's maps (LuaFolderContent::modules or images), or null (also when [folder] is)
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
 * The length of the folder the script is in, relative to the linked folder: its name up to the last '/', 0 for a top-level script
 */
static size_t getScriptFolderLength(const LuaScript& script)
{
    size_t slash = script.source.name.rfind('/');
    return slash == std::string::npos ? 0 : slash;
}

/**
 * Pushes the key of [name] in a subfolder script's own folder (its first [folderLength] chars of [folder]), with [separator] between the
 * parts: "pbi.ui" in "mod/sub" is "mod.sub.pbi.ui" with '.', "img/a.png" is "mod/sub/img/a.png" with '/'. Built on the Lua stack, so that
 * no C++ object is alive if it raises a memory error.
 */
static const char* pushFolderKey(lua_State* L, const char* folder, size_t folderLength, char separator, const char* name, size_t nameLength,
                                 size_t* keyLength)
{
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    for (size_t i = 0; i < folderLength; i++)
        luaL_addchar(&buffer, folder[i] == '/' ? separator : folder[i]);
    luaL_addchar(&buffer, separator);
    luaL_addlstring(&buffer, name, nameLength);
    luaL_pushresult(&buffer);
    return lua_tolstring(L, -1, keyLength);
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
 * require(name): loads a text module from the script folder (see LuaFolderContent) once per state and gives what it returned (true if nothing),
 * holding no C++ object while a Lua call could raise an error, since Lua is built as C
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

    // A script in a subfolder (and every module it requires) looks for modules in that folder: "pbi.ui" from "mod/main.lua" is "mod.pbi.ui"
    const LuaScript& script = getScript(L);
    const char* folder = script.source.name.c_str();
    size_t folderLength = getScriptFolderLength(script);
    const char* key = name;
    size_t keyLength = nameLength;
    if (folderLength > 0)
        key = pushFolderKey(L, folder, folderLength, '.', name, nameLength, &keyLength);

    const LuaFolderContent* folderContent = script.engine->folderContent.get();
    const LuaFolderContent::Module* module = findInFolder(folderContent ? &folderContent->modules : nullptr, key, keyLength);
    if (!module)
    {
        // As a path: lib.util is lib/util.lua or lib/util/init.lua
        const char* path = luaL_gsub(L, name, ".", "/");
        if (folderLength == 0)
            return luaL_error(L, "module '%s' not found (looked for %s.lua and %s/init.lua in the script folder's subfolders)", name, path, path);
        const char* folderName = lua_pushlstring(L, folder, folderLength);
        return luaL_error(L, "module '%s' not found (looked for %s.lua and %s/init.lua in %s/)", name, path, path, folderName);
    }
    if (!module->source)
        return luaL_error(L, "module '%s' wasn't loaded: %s", name, module->leftOutReason.c_str());
    // The key isn't needed anymore, and the slots from 4 on are used below
    lua_settop(L, 3);

    melonade_charge_work(L, (lua_Integer) module->source->size());
    // "=lib/util.lua", so that errors read "lib/util.lua:12: ..."
    const char* chunkName = lua_pushfstring(L, "=%s", module->path.c_str());
    if (luaL_loadbufferx(L, module->source->data(), module->source->size(), chunkName, "t") != LUA_OK)
        return lua_error(L);

    // The chunk name isn't needed anymore: its slot gets the message handler, so that a module's runtime error keeps its traceback (the
    // handler doesn't add a second one when the error goes on up)
    lua_pushcfunction(L, LuaSandbox::messageHandler);
    lua_replace(L, 4);

    // On the chain while it runs (getting its name and path, like Lua's require), and taken off again whether it fails or not, so that a
    // pcall(require, ...) that failed can be tried again
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
 * melon.image.load(path): an image from the script folder by its path in it ("img/hud.png"), or from the script's own folder for a script in
 * a subfolder, or nil and why not
 */
int LuaFolderApi::imageLoad(lua_State* L)
{
    size_t pathLength;
    const char* path = luaL_checklstring(L, 1, &pathLength);
    const LuaScript& script = getScript(L);
    const char* folder = script.source.name.c_str();
    size_t folderLength = getScriptFolderLength(script);
    const char* key = path;
    size_t keyLength = pathLength;
    if (folderLength > 0)
        key = pushFolderKey(L, folder, folderLength, '/', path, pathLength, &keyLength);

    const LuaFolderContent* folderContent = script.engine->folderContent.get();
    const LuaFolderContent::Image* image = findInFolder(folderContent ? &folderContent->images : nullptr, key, keyLength);
    if (!image)
    {
        if (folderLength == 0)
        {
            lua_pushnil(L);
            lua_pushfstring(L, "image '%s' not found in the script folder", path);
            return 2;
        }
        const char* folderName = lua_pushlstring(L, folder, folderLength);
        lua_pushnil(L);
        lua_pushfstring(L, "image '%s' not found in %s/", path, folderName);
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
 * melon.image.new(w, h, argb): an image made from a string of 4 bytes per pixel, each one 0xAARRGGBB little-endian (so B, G, R, A)
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

    // Converting goes through every pixel (the userdata's memory is charged when it's allocated)
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
