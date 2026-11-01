#ifndef LUAENGINEINTERNAL_H
#define LUAENGINEINTERNAL_H

#include <new>
#include <optional>
#include <string>
#include <vector>
#include "LuaEngine.h"
#include "MelonLog.h"
#include "lua.hpp"

/**
 * Shared by the files that make up LuaEngine: the sandbox (LuaSandbox.cpp) and the API files (Lua*Api.cpp), one per part of the melon module.
 * Not for the rest of the frontend, which only sees LuaEngine.h
 */
namespace MelonDSAndroid
{

inline constexpr const char* TAG = LOG_TAG_LUA;

// melon.state slots per script
constexpr int kStateSlotCount = 4;
// The melon.image metatable's name
constexpr const char* kImageType = "melon.image";

/**
 * A melon.image userdata. [pixels] (premultiplied) are the folder's for loaded images (LuaFolderContent, which outlives every state), and right
 * after this struct, inside the userdata, for new() ones
 */
struct LuaImage
{
    int width;
    int height;
    const melonDS::u32* pixels;
};

/**
 * A recorded melon.draw call, drawn once every script's on_frame_end() has run
 */
struct LuaDrawCommand
{
    enum class Type : melonDS::u8
    {
        // A filled rectangle, clipped to the screen
        Rect,
        // A rectangle outline. Its sides are capped to just outside the screen, which draws the same as the full rectangle
        Outline,
        // A line clipped by LuaOverlay::clipLine()
        Line,
        // Text with its top left at (x0, y0), stored in LuaScript::drawText. x1 is its size (1 to LuaOverlay::kMaxTextSize)
        Text,
        // A circle around the corner (x0, y0) with a radius of x1 (see LuaOverlay::getCircleRowSpans()), filled if y1 isn't 0
        Circle,
        // Part of an image with its top left at (x0, y0), see LuaOverlay::drawImage(). x1 is its scale (overlay pixels per image pixel), and
        // color its alpha (0 to 255)
        Image,
    };

    Type type;
    LuaOverlay::Screen screen;
    int x0;
    int y0;
    int x1;
    int y1;
    // Premultiplied
    melonDS::u32 color;
    melonDS::u32 background;
    size_t textOffset;
    size_t textLength;
    // Images: the first pixel to draw, the pixels from one row of the image to the next, and how many columns and rows to draw
    const melonDS::u32* imagePixels = nullptr;
    int imageStride = 0;
    int imageWidth = 0;
    int imageHeight = 0;
};

struct LuaScript
{
    LuaEngine* engine;
    int id;
    LuaScriptSource source;
    // "=name", so that errors read "name:12: ..."
    std::string chunkName;
    lua_State* L = nullptr;
    size_t memoryUsed = 0;
    // Work budget of the current call, and the limit it ran into (if any). Once a limit is hit, the call keeps failing until it ends
    lua_Integer workLeft = 0;
    const char* limitError = nullptr;

    // Input requests from the current frame. Cleared once the frame has run, so a script that stops calling them leaves no key held
    melonDS::u32 press = 0;
    melonDS::u32 release = 0;
    // Kept until clear_touch()
    bool hasTouchPosition = false;
    melonDS::u16 touchX = 0;
    melonDS::u16 touchY = 0;

    // Requests to the app from the current callback, sent once it returns
    std::optional<bool> swapScreens;
    std::optional<bool> showControls;
    std::optional<bool> fastForward;
    // melon.screen.set_layout(): the layout option's key, or an empty string for set_layout(nil)
    std::optional<std::string> layoutKey;

    void clearRequests()
    {
        swapScreens.reset();
        showControls.reset();
        fastForward.reset();
        layoutKey.reset();
    }

    // melon.state slots: whole savestates, empty when unused. Outside the Lua state, so they survive reloads (a reset). Freed when the
    // script is switched off
    std::vector<melonDS::u8> stateSlots[kStateSlotCount];
    // LuaEngine::framePass of the last save, for the cooldown
    std::optional<int64_t> lastStateSaveFramePass;

    // melon.draw calls from the current frame: on_frame()'s, then on_frame_end()'s. Text goes into one string, which the commands point into,
    // of at most kMaxDrawTextBytes
    std::vector<LuaDrawCommand> drawCommands;
    std::string drawText;
    // The images drawn this frame, in a registry table (its reference), so that they can't be collected before they're drawn
    int drawnImagesRef = LUA_NOREF;
    int drawnImageCount = 0;

    // melon.storage. Outside the Lua state, so that it survives reloads
    LuaStorage::Store storage;
};

inline LuaScript& getScript(lua_State* L)
{
    return **static_cast<LuaScript**>(lua_getextraspace(L));
}

/**
 * Charges [amount] steps of work done in C to the running script's budget, and raises an error once it's used up. Also called by
 * lua/patched/lstrlib.c
 */
extern "C" void melonade_charge_work(lua_State* L, lua_Integer amount);

/**
 * Runs a LuaSavestateHost call, which allocates a whole savestate. Out of memory counts as a failed call
 */
template <typename Call>
inline bool callSavestateHost(Call&& call)
{
    try
    {
        return call();
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}

/**
 * What every script's state runs in: its allocator, the instruction and work limits, and the standard library with the unsafe parts taken out
 */
namespace LuaSandbox
{

// lua_Alloc for a state whose user data is its LuaScript. Keeps the script under the memory limit and charges what it allocates as work
void* allocate(void* userData, void* pointer, size_t oldSize, size_t newSize);
// Opens the allowed libraries into a fresh state, with the functions that aren't safe removed or wrapped
void open(lua_State* L);
// Sets the instruction limit and the work budget of the call that is about to be made with lua_pcall()
void beginCall(LuaScript& script, int instructionLimit);
// Fails the rest of the call and raises the limit that was hit (the first one, if another was hit before)
[[noreturn]] void raiseLimitError(lua_State* L, LuaScript& script, const char* error);
// The lua_pcall() message handler: adds a traceback to the error, unless it already has one
int messageHandler(lua_State* L);

}

/**
 * melon.mem
 */
struct LuaMemoryApi
{
    static const luaL_Reg arm9Functions[];
    static const luaL_Reg arm7Functions[];

private:
    template <typename T, bool isSigned, bool isArm9> static int read(lua_State* L);
    template <typename T, bool isArm9> static int write(lua_State* L);
};

/**
 * melon.draw: the calls record LuaDrawCommands for the script, which [rasterize] draws once the frame has run
 */
struct LuaDrawApi
{
    static const luaL_Reg functions[];

    // Makes the state's table of drawn images (LuaScript::drawnImagesRef)
    static void install(lua_State* L, LuaScript& script);
    // Drops the script's commands and lets its drawn images be collected
    static void clearCommands(LuaScript& script);
    // Draws the script's commands into [overlay]
    static void rasterize(const LuaScript& script, LuaOverlay& overlay);

private:
    static LuaDrawCommand* addDrawCommand(lua_State* L, LuaDrawCommand::Type type, LuaOverlay::Screen screen, int64_t x0, int64_t y0, int64_t x1, int64_t y1, melonDS::u32 color, int64_t pixelCount, size_t textLength = 0);
    static int pixel(lua_State* L);
    static int line(lua_State* L);
    static int rect(lua_State* L);
    static int text(lua_State* L);
    static int circle(lua_State* L);
    static int image(lua_State* L);
};

/**
 * require() and melon.image: what the scripts get from their folder (LuaFolderContent)
 */
struct LuaFolderApi
{
    static const luaL_Reg imageFunctions[];

    // Registers require(), its registry tables and the image metatable in a fresh state
    static void install(lua_State* L);

private:
    static int require(lua_State* L);
    static int imageLoad(lua_State* L);
    static int imageNew(lua_State* L);
    static int imageSize(lua_State* L);
};

/**
 * melon.state
 */
struct LuaStateApi
{
    static const luaL_Reg functions[];

private:
    static int save(lua_State* L);
    static int load(lua_State* L);
    static int has(lua_State* L);
};

/**
 * melon.storage
 */
struct LuaStorageApi
{
    static const luaL_Reg functions[];

private:
    static int get(lua_State* L);
    static int set(lua_State* L);
    static int keys(lua_State* L);
    static int clear(lua_State* L);
    static int flush(lua_State* L);
    static int size(lua_State* L);
};

}

#endif //LUAENGINEINTERNAL_H
