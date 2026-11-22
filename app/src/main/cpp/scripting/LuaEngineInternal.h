#ifndef LUAENGINEINTERNAL_H
#define LUAENGINEINTERNAL_H

#include <array>
#include <new>
#include <optional>
#include <string>
#include <vector>
#include "LuaEngine.h"
#include "MelonLog.h"
#include "lua.hpp"

/**
 * Shared only by the files that make up LuaEngine: the sandbox (LuaSandbox.cpp) and the API files (Lua*Api.cpp), one per part of the melon
 * module
 */
namespace MelonDSAndroid
{

inline constexpr const char* TAG = LOG_TAG_LUA;

// melon.state slots per script
constexpr int kStateSlotCount = 4;
// The melon.image metatable's name
constexpr const char* kImageType = "melon.image";

/**
 * A melon.image userdata, whose premultiplied [pixels] are the folder's for loaded images (LuaFolderContent, which outlives every state) and
 * follow this struct inside the userdata for new() ones
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
        // A rectangle outline, its sides capped to just outside the screen (which draws the same as the full rectangle)
        Outline,
        // A line clipped by LuaOverlay::clipLine()
        Line,
        // Text with its top left at (x0, y0) and a size of x1 (1 to LuaOverlay::kMaxTextSize), stored in LuaScript::drawText
        Text,
        // A circle around the corner (x0, y0) with a radius of x1 (see LuaOverlay::getCircleRowSpans()), filled if y1 isn't 0
        Circle,
        // Part of an image with its top left at (x0, y0), a scale of x1 (overlay pixels per image pixel) and color as its alpha (0 to 255)
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

/**
 * What melon.screen.tilt_shift() asks for, clamped; a default one is off, and every call that means off makes a default one so that they compare equal
 */
struct LuaTiltShift
{
    float top = 0.0f;
    float bottom = 0.0f;
    float center = 0.5f;
    float width = 0.3f;
    float radius = 6.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    // Strength of saturation and contrast, or negative when the script didn't pass it: they follow the stronger band then
    float color = -1.0f;

    bool operator==(const LuaTiltShift&) const = default;
};

/**
 * What melon.screen.bloom(), melon.screen.vignette() and melon.screen.haze() ask for, clamped, each call setting its own part; a default one is
 * off, and a part that means off is made the default part so that they compare equal
 */
struct LuaBloom
{
    float strength = 0.0f;
    float threshold = 0.7f;
    float radius = 3.0f;
    // Whether the glow only picks up 3D pixels, which needs the frame's depth (LuaEngine::wantsDepth)
    bool onlyThreeD = true;
    float vignette = 0.0f;
    float vignetteSize = 0.6f;
    float haze = 0.0f;
    // 0xRRGGBB
    melonDS::u32 hazeColor = 0xDCE6F0;
    // Raw depth, 0..1
    float hazeStart = 0.9968f;
    float hazeFalloff = 0.0008f;

    bool operator==(const LuaBloom&) const = default;
};

/**
 * What melon.screen.depth_of_field() asks for, clamped; a default one is off, and every call that means off makes a default one so that they
 * compare equal
 */
struct LuaDepthOfField
{
    float strength = 0.0f;
    float radius = 4.0f;
    // DS pixels of the top screen's 256 x 192 view, whose depth is the focus unless focusDepth isn't negative
    float focusX = 128.0f;
    float focusY = 96.0f;
    float focusDepth = -1.0f;
    float range = 0.0002f;
    float falloff = 0.0015f;

    bool operator==(const LuaDepthOfField&) const = default;
};

/**
 * What melon.screen.shade() asks for, clamped; a default one is off, and every call that means off makes a default one so that they compare
 * equal
 */
struct LuaShade
{
    float strength = 0.0f;
    // DS pixels
    float radius = 8.0f;
    // Raw depth, 0..1
    float range = 0.0008f;
    // The shadow's tint, 0xRRGGBB
    melonDS::u32 color = 0x33384F;

    bool operator==(const LuaShade&) const = default;
};

/**
 * What melon.screen.sun() asks for, clamped, with its vectors normalized; a default one is off, and every call that means off makes a default
 * one so that they compare equal. The defaults suit Pokémon White's field
 */
struct LuaSun
{
    float strength = 0.0f;
    // Towards the sun, in the depth camera's view space
    std::array<float, 3> direction = { 0.50f, 0.83f, 0.26f };
    // The world's up in view space. The march doesn't use it; it's kept for the characters' shadows, which the core will draw
    std::array<float, 3> up = { 0.0006f, 0.607f, 0.794f };
    // View-space units (a tile is 16 in White)
    float length = 96.0f;
    int steps = 64;
    float bias = 1.0f;
    // DS pixels
    float softness = 1.5f;
    // The shadow's tint, 0xRRGGBB
    melonDS::u32 color = 0x33384F;

    bool operator==(const LuaSun&) const = default;
};

/**
 * What a script's melon.wide calls ask of the WideMelon camera, clamped to what any game could take (the app clamps further); a default one
 * asks for nothing
 */
struct LuaWideCamera
{
    // set_zoom(): the zoom factor, or negative for the player's
    float zoom = -1.0f;
    // pan(): the window's offset from center in DS pixels, if hasPan, and down from center, if hasPanY
    bool hasPan = false;
    float pan = 0.0f;
    bool holdNative = false;
    bool hasPanY = false;
    float panY = 0.0f;

    bool operator==(const LuaWideCamera&) const = default;
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
    // Work budget of the current call, and the limit it ran into (if any), after which the call keeps failing until it ends
    lua_Integer workLeft = 0;
    const char* limitError = nullptr;

    // Input requests from the current frame, cleared once it has run so that a script that stops calling them leaves no key held
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
    std::optional<LuaTiltShift> tiltShift;
    // What the app was last told, so that a script can call tilt_shift() every frame and only changes cost an event
    LuaTiltShift sentTiltShift;
    // melon.screen.bloom(), vignette() and haze(): kept until the script changes them or stops, since each call sets only its part, and what
    // the app was last told of them, which also decides whether the frames keep their depth (LuaEngine::wantsDepth)
    LuaBloom bloom;
    LuaBloom sentBloom;
    // melon.screen.depth_of_field(): kept until the script changes it or stops, and what the app was last told of it, which also decides whether
    // the frames keep their depth (LuaEngine::wantsDepth)
    LuaDepthOfField depthOfField;
    LuaDepthOfField sentDepthOfField;
    // melon.screen.shade(): the same as the depth of field
    LuaShade shade;
    LuaShade sentShade;
    // melon.screen.sun(): the same, and when the app was told of it (LuaEngine::sunShadows), as the newest request is the one shown
    LuaSun sun;
    LuaSun sentSun;
    unsigned int sentSunOrder = 0;
    // melon.wide: kept until the script changes it or stops (not a per-call request), and what the app was last told of it
    LuaWideCamera wideCamera;
    LuaWideCamera sentWideCamera;

    void clearRequests()
    {
        swapScreens.reset();
        showControls.reset();
        fastForward.reset();
        layoutKey.reset();
        tiltShift.reset();
    }

    // melon.state slots: whole savestates, empty when unused, kept outside the Lua state so they survive a reset (until the script is switched off)
    std::vector<melonDS::u8> stateSlots[kStateSlotCount];
    // LuaEngine::framePass of the last save, for the cooldown
    std::optional<int64_t> lastStateSaveFramePass;

    // melon.draw calls from the current frame (on_frame()'s, then on_frame_end()'s), their text in one string of at most kMaxDrawTextBytes
    std::vector<LuaDrawCommand> drawCommands;
    std::string drawText;
    // The images drawn this frame, in a registry table (its reference), so that they can't be collected before they're drawn
    int drawnImagesRef = LUA_NOREF;
    int drawnImageCount = 0;

    // melon.storage, outside the Lua state so that it survives reloads
    LuaStorage::Store storage;
};

inline LuaScript& getScript(lua_State* L)
{
    return **static_cast<LuaScript**>(lua_getextraspace(L));
}

/**
 * Charges [amount] steps of work done in C to the running script's budget and raises an error once it's used up; lua/patched/lstrlib.c
 * calls it too
 */
extern "C" void melonade_charge_work(lua_State* L, lua_Integer amount);

/**
 * Runs a LuaSavestateHost call, which allocates a whole savestate, counting out of memory as a failed call
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

// lua_Alloc for a state whose user data is its LuaScript: keeps the script under the memory limit and charges what it allocates as work
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
 * melon.wide (LuaWideApi.cpp): the WideMelon camera
 */
struct LuaWideApi
{
    static const luaL_Reg functions[];

private:
    static int camera(lua_State* L);
    static int setZoom(lua_State* L);
    static int pan(lua_State* L);
    static int holdNative(lua_State* L);
};

/**
 * melon.game.state() (LuaGameApi.cpp): what a Pokemon game the field read knows is doing (gamestate/GameState.h); the rest of melon.game is in
 * LuaEngine.cpp
 */
struct LuaGameApi
{
    static int state(lua_State* L);
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
