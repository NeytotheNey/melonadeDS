#include "LuaEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <type_traits>
#include "MelonLog.h"
#include "lua.hpp"
#include "debug/DebugLog.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// Threat model: scripts are files the player picked and installed. The limits below are there so that a buggy or careless script can't freeze
// the emulator thread or the app (it gets switched off instead). They aren't meant to stop a script written to hang its own player's game.

static const char* const TAG = LOG_TAG_LUA;

// VM instructions a script may run per call. The main chunk and on_load() get more room, since they may build tables
static const int kLoadInstructionLimit = 10'000'000;
static const int kCallbackInstructionLimit = 1'000'000;
// C functions don't count instructions, so the work they do (pattern matching and searches in lua/patched/lstrlib.c, bytes allocated, table
// elements moved or sorted) is charged to a budget instead. It's set when each call starts and scales with its instruction limit: 20M steps
// for a callback, which ordinary scripts never get near, and roughly 40-100 ms of work when one does
static const lua_Integer kWorkPerInstruction = 20;
// Steps per table element that table.move/insert/remove/sort go through
static const lua_Integer kTableElementCost = 4;
// Bytes allocated per step
static const size_t kBytesPerStep = 16;
static const size_t kMemoryLimit = 32 * 1024 * 1024;
// Most elements table.move/insert/remove/sort may go through in one call. More than the memory limit could hold anyway
static const lua_Integer kMaxTableLoop = 1 << 22;
static const char* const kInstructionLimitError = "ran for too long (over the instruction limit)";
static const char* const kWorkLimitError = "did too much work in one call (over the work limit)";
// melon.draw: each recorded call costs this much work, plus a step per pixel it draws (after clipping) and, for text, per byte. A callback's
// budget covers about 100 draws of a whole screen
static const lua_Integer kDrawCallCost = 32;
// An image pixel is read from the image, and often scaled by alpha and blended, so it costs more than a filled rectangle's
static const lua_Integer kImageStepsPerPixel = 2;
static const size_t kMaxDrawCommands = 4096;
static const char* const kDrawLimitError = "drew too much in one frame (over 4096 draw calls)";
// Text a script can draw in one frame. It's kept outside the Lua memory limit, so it has a cap of its own
static const size_t kMaxDrawTextBytes = 64 * 1024;
static const char* const kDrawTextLimitError = "drew too much text in one frame (over 64 KB)";
// Coordinates are capped this far out, in overlay pixels, so that clipping and pixel counts can't overflow. Only a line with an end further out
// than that could change (its slope), and it would be over a billion screens away
static const double kMaxCoordinate = (double) (1ll << 40);
// melon.image: images made with new() have the same size cap as the ones from the folder. src rectangles are capped this far out (in image
// pixels) so that their maths can't overflow, which only changes rectangles that couldn't be seen anyway
static const lua_Integer kMaxImageSide = 1024;
static const lua_Integer kMaxImageOffset = 1 << 30;
static const char* const kImageType = "melon.image";
// melon.storage: changed data is snapshotted at least this often, in frames (about 10 seconds)
static const int kStorageSaveInterval = 600;
// melon.state: slots per script. A save copies the whole emulator state (a few MB on a DS, more on a DSi), which takes milliseconds, so each
// script can only save once every kStateSaveInterval frames (half a second), and saves and loads are charged kStateWork steps (5% of a
// callback's budget). Every script's slots together hold at most kMaxStateSlotMemory
static const int kStateSlotCount = 4;
static const int kStateSaveInterval = 30;
static const lua_Integer kStateWork = 1'000'000;
static const size_t kMaxStateSlotMemory = 64 * 1024 * 1024;

// Registry keys (their addresses are): the modules a state has loaded, and the ones that are loading, in order. Not const, so that the compiler
// can't merge them
static char kLoadedModulesKey;
static char kRequireChainKey;

/**
 * A melon.image userdata. [pixels] (premultiplied) are the folder's for loaded images (LuaFolderContent, which outlives every state), and right
 * after this struct, inside the userdata, for new() ones
 */
struct LuaImage
{
    int width;
    int height;
    const u32* pixels;
};

/**
 * A recorded melon.draw call, drawn once every script's on_frame_end() has run
 */
struct LuaEngine::DrawCommand
{
    enum class Type : u8
    {
        // A filled rectangle, clipped to the screen
        Rect,
        // A rectangle outline. Its sides are capped to just outside the screen, which draws the same as the full rectangle
        Outline,
        // A line clipped by LuaOverlay::clipLine()
        Line,
        // Text with its top left at (x0, y0), stored in Script::drawText. x1 is its size (1 to LuaOverlay::kMaxTextSize)
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
    u32 color;
    u32 background;
    size_t textOffset;
    size_t textLength;
    // Images: the first pixel to draw, the pixels from one row of the image to the next, and how many columns and rows to draw
    const u32* imagePixels = nullptr;
    int imageStride = 0;
    int imageWidth = 0;
    int imageHeight = 0;
};

struct LuaEngine::Script
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
    u32 press = 0;
    u32 release = 0;
    // Kept until clear_touch()
    bool hasTouchPosition = false;
    u16 touchX = 0;
    u16 touchY = 0;

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
    std::vector<u8> stateSlots[kStateSlotCount];
    // LuaEngine::framePass of the last save, for the cooldown
    std::optional<int64_t> lastStateSaveFramePass;

    // melon.draw calls from the current frame: on_frame()'s, then on_frame_end()'s. Text goes into one string, which the commands point into,
    // of at most kMaxDrawTextBytes
    std::vector<DrawCommand> drawCommands;
    std::string drawText;
    // The images drawn this frame, in a registry table (its reference), so that they can't be collected before they're drawn
    int drawnImagesRef = LUA_NOREF;
    int drawnImageCount = 0;

    // melon.storage. Outside the Lua state, so that it survives reloads
    LuaStorage::Store storage;
};

static void clearDrawCommands(LuaEngine::Script& script)
{
    script.drawCommands.clear();
    script.drawText.clear();

    // Lets this frame's images be collected again. Setting fields that are there to nil never allocates, so this can't raise an error
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

static LuaEngine::Script& getScript(lua_State* L)
{
    return **static_cast<LuaEngine::Script**>(lua_getextraspace(L));
}

static void instructionLimitHook(lua_State* L, lua_Debug*);

/**
 * Stops the current call once a limit is hit: every instruction fails from now on, so that the script can't catch the error with pcall and
 * keep going
 */
static void failRestOfCall(LuaEngine::Script& script, const char* error)
{
    if (!script.limitError)
        script.limitError = error;
    lua_sethook(script.L, instructionLimitHook, LUA_MASKCOUNT, 1);
}

static void* allocate(void* userData, void* pointer, size_t oldSize, size_t newSize)
{
    auto& script = *static_cast<LuaEngine::Script*>(userData);
    // When the pointer is null, oldSize is the object type instead of a size
    size_t currentSize = pointer ? oldSize : 0;

    if (newSize == 0)
    {
        free(pointer);
        script.memoryUsed -= currentSize;
        return nullptr;
    }

    if (newSize > currentSize)
    {
        if (script.memoryUsed - currentSize + newSize > kMemoryLimit)
            return nullptr;

        // Allocating and filling memory is work too (string.rep, concatenation in a loop). The state is still being created while L is null
        if (script.L)
        {
            script.workLeft -= (lua_Integer) ((newSize - currentSize) / kBytesPerStep + 1);
            if (script.workLeft < 0)
            {
                failRestOfCall(script, kWorkLimitError);
                return nullptr;
            }
        }
    }

    void* newPointer = realloc(pointer, newSize);
    if (newPointer)
        script.memoryUsed = script.memoryUsed - currentSize + newSize;

    return newPointer;
}

static void instructionLimitHook(lua_State* L, lua_Debug*)
{
    LuaEngine::Script& script = getScript(L);
    failRestOfCall(script, kInstructionLimitError);
    luaL_error(L, "%s", script.limitError);
}

/**
 * Charges [amount] steps of work done in C to the running script's budget, and raises an error once it's used up. Also called by
 * lua/patched/lstrlib.c
 */
extern "C" void melonade_charge_work(lua_State* L, lua_Integer amount)
{
    LuaEngine::Script& script = getScript(L);
    script.workLeft -= amount;
    if (script.workLeft < 0) [[unlikely]]
    {
        failRestOfCall(script, kWorkLimitError);
        luaL_error(L, "%s", script.limitError);
    }
}

static int messageHandler(lua_State* L)
{
    const char* message = lua_tostring(L, 1);
    if (message == nullptr)
    {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            message = lua_tostring(L, -1);
        else
            message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    // Already has one: a module's error that require() passed on. Its traceback was made further down, so it has every level this one would
    else if (strstr(message, "\nstack traceback:"))
    {
        return 1;
    }

    luaL_traceback(L, L, message, 1);
    return 1;
}

/**
 * Calls the function the wrapper replaced (its first upvalue) with the same arguments
 */
static int callWrappedFunction(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, argumentCount, LUA_MULTRET);
    return lua_gettop(L);
}

/**
 * Replaces [table].[name] (or the global [name] if [table] is null) with [wrapper], which gets the original function as its first upvalue
 */
static void wrapFunction(lua_State* L, const char* table, const char* name, lua_CFunction wrapper)
{
    if (table)
    {
        lua_getglobal(L, table);
        lua_getfield(L, -1, name);
        lua_pushcclosure(L, wrapper, 1);
        lua_setfield(L, -2, name);
        lua_pop(L, 1);
    }
    else
    {
        lua_getglobal(L, name);
        lua_pushcclosure(L, wrapper, 1);
        lua_setglobal(L, name);
    }
}

/**
 * load() that only takes text chunks. Binary chunks can crash the VM
 */
static int loadTextOnly(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    // Pad to two arguments so index 2 is the chunk name (or nil), not the original load pushed below. env is only passed if the caller gave
    // one, because load treats an explicit nil env as "set _ENV to nil"
    if (argumentCount < 2)
        lua_settop(L, 2);
    int top = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushliteral(L, "t");
    if (argumentCount >= 4)
        lua_pushvalue(L, 4);

    lua_call(L, argumentCount >= 4 ? 4 : 3, LUA_MULTRET);
    return lua_gettop(L) - top;
}

/**
 * setmetatable() that refuses __gc. Finalizers run with hooks off, so the instruction limit couldn't stop one that loops forever. A finalizer
 * is only registered if __gc is there when setmetatable() is called, so checking here is enough
 */
static int setMetatableWithoutGc(lua_State* L)
{
    if (lua_type(L, 2) == LUA_TTABLE)
    {
        lua_pushliteral(L, "__gc");
        if (lua_rawget(L, 2) != LUA_TNIL)
            return luaL_error(L, "__gc metamethods aren't allowed");
        lua_pop(L, 1);
    }
    return callWrappedFunction(L);
}

/**
 * collectgarbage() without the options that change how the collector runs. "stop" would let memory run into the limit
 */
static int safeCollectGarbage(lua_State* L)
{
    static const char* const allowedOptions[] = { "collect", "count", "step", "isrunning", nullptr };
    luaL_checkoption(L, 1, "collect", allowedOptions);
    return callWrappedFunction(L);
}

static int safeStringRep(lua_State* L)
{
    size_t length, separatorLength;
    luaL_checklstring(L, 1, &length);
    lua_Integer count = luaL_checkinteger(L, 2);
    luaL_optlstring(L, 3, "", &separatorLength);
    // With nothing to copy, string.rep would loop [count] times without allocating anything
    if (count > 0 && length + separatorLength == 0)
    {
        lua_pushliteral(L, "");
        return 1;
    }
    return callWrappedFunction(L);
}

static int safeTableMove(lua_State* L)
{
    lua_Integer first = luaL_checkinteger(L, 2);
    lua_Integer last = luaL_checkinteger(L, 3);
    if (last >= first)
    {
        if ((lua_Unsigned) last - (lua_Unsigned) first >= (lua_Unsigned) kMaxTableLoop)
            return luaL_error(L, "too many elements to move (the limit is %d)", (int) kMaxTableLoop);
        melonade_charge_work(L, (last - first + 1) * kTableElementCost);
    }
    return callWrappedFunction(L);
}

/**
 * The length that table.insert/remove/sort go up to. A sparse table or a __len metamethod can make #t huge without the table holding anything,
 * so __len isn't allowed and the length is capped
 */
static lua_Integer checkTableLength(lua_State* L)
{
    if (lua_type(L, 1) != LUA_TTABLE)
        return 0;

    if (luaL_getmetafield(L, 1, "__len") != LUA_TNIL)
        luaL_error(L, "tables with a __len metamethod aren't supported here");
    lua_Unsigned length = lua_rawlen(L, 1);
    if (length > (lua_Unsigned) kMaxTableLoop)
        luaL_error(L, "table too long (the limit is %d elements)", (int) kMaxTableLoop);
    return (lua_Integer) length;
}

/**
 * table.insert() and table.remove() shift every element up to #t
 */
static int safeTableShift(lua_State* L)
{
    melonade_charge_work(L, checkTableLength(L) * kTableElementCost);
    return callWrappedFunction(L);
}

static int safeTableSort(lua_State* L)
{
    // About n * log2(n) comparisons
    lua_Integer length = checkTableLength(L);
    lua_Integer comparisons = length;
    for (lua_Integer n = length; n > 1; n >>= 1)
        comparisons += length;
    melonade_charge_work(L, comparisons * kTableElementCost);
    return callWrappedFunction(L);
}

template <typename T>
static bool readTcm(ARMv5& arm9, u32 address, T& value)
{
    if (address < arm9.ITCMSize)
    {
        memcpy(&value, &arm9.ITCM[address & (ITCMPhysicalSize - 1)], sizeof(T));
        return true;
    }
    if ((address & arm9.DTCMMask) == arm9.DTCMBase)
    {
        memcpy(&value, &arm9.DTCM[address & (DTCMPhysicalSize - 1)], sizeof(T));
        return true;
    }
    return false;
}

template <typename T>
static bool writeTcm(NDS& nds, u32 address, T value)
{
    ARMv5& arm9 = nds.ARM9;
    if (address < arm9.ITCMSize)
    {
        memcpy(&arm9.ITCM[address & (ITCMPhysicalSize - 1)], &value, sizeof(T));
        // Code can run from ITCM, so drop any JIT block compiled from it (like ARMv5::DataWrite does)
        nds.JIT.CheckAndInvalidate<0, ARMJIT_Memory::memregion_ITCM>(address);
        return true;
    }
    if ((address & arm9.DTCMMask) == arm9.DTCMBase)
    {
        memcpy(&arm9.DTCM[address & (DTCMPhysicalSize - 1)], &value, sizeof(T));
        return true;
    }
    return false;
}

template <typename T>
static T busRead(NDS& nds, u32 address, bool isArm9)
{
    if constexpr (sizeof(T) == 1)
        return isArm9 ? nds.ARM9Read8(address) : nds.ARM7Read8(address);
    else if constexpr (sizeof(T) == 2)
        return isArm9 ? nds.ARM9Read16(address) : nds.ARM7Read16(address);
    else
        return isArm9 ? nds.ARM9Read32(address) : nds.ARM7Read32(address);
}

template <typename T>
static void busWrite(NDS& nds, u32 address, T value, bool isArm9)
{
    // Through the bus, like AR cheats. Main RAM and shared WRAM writes invalidate the JIT there
    if constexpr (sizeof(T) == 1)
        isArm9 ? nds.ARM9Write8(address, value) : nds.ARM7Write8(address, value);
    else if constexpr (sizeof(T) == 2)
        isArm9 ? nds.ARM9Write16(address, value) : nds.ARM7Write16(address, value);
    else
        isArm9 ? nds.ARM9Write32(address, value) : nds.ARM7Write32(address, value);
}

LuaEngine::LuaEngine() = default;

LuaEngine::~LuaEngine()
{
    for (auto& script : scripts)
        closeScript(*script);
}

void LuaEngine::setScripts(std::vector<LuaScriptSource> newScripts, std::shared_ptr<const LuaFolderContent> newFolderContent, bool newAreStateLoadsAllowed)
{
    pendingScripts = std::move(newScripts);
    folderContent = std::move(newFolderContent);
    areStateLoadsAllowed = newAreStateLoadsAllowed;
}

void LuaEngine::start(NDS* newNds, std::shared_ptr<MelonEventMessenger> newEventMessenger, LuaSavestateHost* newSavestateHost)
{
    nds = newNds;
    eventMessenger = std::move(newEventMessenger);
    savestateHost = newSavestateHost;

    if (auto cart = nds->NDSCartSlot.GetCart())
        gameCode.assign(cart->GetHeader().GameCode, strnlen(cart->GetHeader().GameCode, 4));

    for (auto& source : pendingScripts)
    {
        auto script = std::make_unique<Script>();
        script->engine = this;
        script->id = (int) scripts.size();
        // Lua cuts chunk names to 59 bytes, which could split a UTF-8 character in the error the player sees. Cut it here instead
        size_t chunkNameLength = std::min(source.name.size(), (size_t) LUA_IDSIZE - 1);
        while (chunkNameLength > 0 && chunkNameLength < source.name.size() && (source.name[chunkNameLength] & 0xC0) == 0x80)
            chunkNameLength--;
        script->chunkName = "=" + source.name.substr(0, chunkNameLength);
        script->source = std::move(source);
        // A corrupt blob starts empty, and the file is only replaced once the script writes something
        if (!LuaStorage::decodeBlob(script->source.savedData, script->storage))
            LOG_WARN(TAG, "%s: its saved data couldn't be read, so it starts empty", script->source.name.c_str());
        std::string().swap(script->source.savedData);
        Script& loadedScript = *scripts.emplace_back(std::move(script));

        if (loadScript(loadedScript))
            callCallback(loadedScript, "on_load", gameCode.c_str(), kLoadInstructionLimit);
    }
    pendingScripts.clear();
}

LuaInputOverride LuaEngine::runFrame(int frame, const LuaUserInput& userInput)
{
    currentFrame = frame;
    currentUserInput = userInput;
    framePass++;

    // Before a pending reset, so a script the player switched off isn't loaded again
    if (isScriptSwitchPending.exchange(false, std::memory_order_acquire))
        applyPendingScriptSwitches();

    // A script's load that is still queued (from on_frame_end(), say) mustn't undo the player's reset or load
    if (isResetPending.exchange(false, std::memory_order_relaxed))
    {
        pendingStateLoadScript = -1;
        reloadScripts();
    }

    // Drawings only last one frame
    for (auto& script : scripts)
        clearDrawCommands(*script);

    bool isPlayerStateLoad = isStateLoadPending.exchange(false, std::memory_order_relaxed);
    if (isPlayerStateLoad)
        pendingStateLoadScript = -1;
    if (isPlayerStateLoad || isScriptStateLoaded)
    {
        isScriptStateLoaded = false;
        for (auto& script : scripts)
            callCallback(*script, "on_state_loaded", nullptr, kCallbackInstructionLimit);
    }

    // Only on_frame() and on_frame_end() draw, so that a frame's drawings come from one pass through the frame
    isDrawingAllowed = true;
    for (auto& script : scripts)
        callCallback(*script, "on_frame", nullptr, kCallbackInstructionLimit);
    isDrawingAllowed = false;

    // Scripts that were switched off don't count anymore
    LuaInputOverride inputOverride;
    for (auto& script : scripts)
    {
        if (script->L)
        {
            inputOverride.press |= script->press;
            inputOverride.release |= script->release;
            if (script->hasTouchPosition)
            {
                inputOverride.hasTouchPosition = true;
                inputOverride.touchX = script->touchX;
                inputOverride.touchY = script->touchY;
            }
        }
        script->press = 0;
        script->release = 0;
    }

    // A load from melon.state.load(), now that every script's on_frame() has run. The input changes were made for the state before it, so
    // the frame runs with the player's own input
    if (pendingStateLoadScript >= 0)
    {
        runPendingStateLoad();
        if (isScriptStateLoaded)
            inputOverride = LuaInputOverride();
    }

    return inputOverride;
}

void LuaEngine::runPendingStateLoad()
{
    int id = pendingStateLoadScript;
    int slot = pendingStateLoadSlot;
    pendingStateLoadScript = -1;

    // The script may have been switched off since it asked, which also freed its slots
    if (id < 0 || id >= (int) scripts.size() || !savestateHost || !areStateLoadsAllowed)
        return;
    Script& script = *scripts[id];
    if (!script.L || script.stateSlots[slot].empty())
        return;

    bool isLoaded = false;
    try
    {
        isLoaded = savestateHost->loadLuaState(script.stateSlots[slot]);
    }
    catch (const std::bad_alloc&)
    {
    }

    if (!isLoaded)
    {
        // The host went back to the state from before the load
        LOG_WARN(TAG, "%s: state %d couldn't be loaded", script.source.name.c_str(), slot + 1);
        return;
    }

    isScriptStateLoaded = true;
    if (eventMessenger)
        eventMessenger->onLuaStateLoaded();
    LOG_DEBUGLOG(TAG, "%s: loaded state %d", script.source.name.c_str(), slot + 1);
}

void LuaEngine::freeStateSlots(Script& script)
{
    for (auto& slot : script.stateSlots)
    {
        stateSlotMemory -= slot.size();
        std::vector<u8>().swap(slot);
    }
    script.lastStateSaveFramePass.reset();
    if (pendingStateLoadScript == script.id)
        pendingStateLoadScript = -1;
}

void LuaEngine::endFrame()
{
    // Its own instruction budget. press()/release() calls here are kept for the next frame's input, since runFrame() only clears them once
    // they've been applied
    isDrawingAllowed = true;
    for (auto& script : scripts)
        callCallback(*script, "on_frame_end", nullptr, kCallbackInstructionLimit);
    isDrawingAllowed = false;

    // Also when every script has stopped, so that their last drawings go away
    rasterizeOverlay();

    // Storage is never saved every frame: only when a script asks with flush(), or now and then
    bool isStorageSaveDue = ++framesSinceStorageSave >= kStorageSaveInterval;
    if (isStorageSaveDue)
        framesSinceStorageSave = 0;
    for (auto& script : scripts)
    {
        if (isStorageSaveDue || script->storage.isFlushRequested)
            saveStorage(*script);
    }
}

void LuaEngine::flushStorage()
{
    for (auto& script : scripts)
        saveStorage(*script);
}

void LuaEngine::saveStorage(Script& script)
{
    script.storage.isFlushRequested = false;
    if (!script.storage.isDirty)
        return;

    script.storage.isDirty = false;
    // Without a place to save it (no game key), the data only lasts for the session
    if (script.source.dataGameKey.empty())
        return;

    bool wasOutboxEmpty = LuaStorage::pushSnapshot({ script.source.dataGameKey, script.source.name, LuaStorage::encodeBlob(script.storage) });
    // Only a wake-up: the app drains the outbox itself. While it isn't empty, the app has already been woken up
    if (wasOutboxEmpty && eventMessenger)
        eventMessenger->onLuaStorageSaved();

    LOG_DEBUGLOG(TAG, "%s: storage saved (%zu bytes)", script.source.name.c_str(), script.storage.size);
}

void LuaEngine::stop()
{
    isUnloading = true;
    for (auto& script : scripts)
        callCallback(*script, "on_unload", nullptr, kCallbackInstructionLimit);

    for (auto& script : scripts)
    {
        closeScript(*script);
        saveStorage(*script);
        freeStateSlots(*script);
    }
    scripts.clear();
    framesSinceStorageSave = 0;
    pendingStateLoadScript = -1;
    isScriptStateLoaded = false;
    framePass = 0;
    isUnloading = false;
    overlay.clear();

    // The ids belong to this session's scripts
    std::lock_guard lock(pendingScriptSwitchMutex);
    pendingScriptSwitches.clear();
    isScriptSwitchPending.store(false, std::memory_order_relaxed);
}

void LuaEngine::disableScript(int id)
{
    queueScriptSwitch(id, ScriptSwitch::Action::Disable);
}

void LuaEngine::enableScript(int id)
{
    queueScriptSwitch(id, ScriptSwitch::Action::Enable);
}

void LuaEngine::restartScript(int id, std::vector<LuaOptionValue> options)
{
    queueScriptSwitch(id, ScriptSwitch::Action::Restart, std::move(options));
}

void LuaEngine::queueScriptSwitch(int id, ScriptSwitch::Action action, std::vector<LuaOptionValue> options)
{
    std::lock_guard lock(pendingScriptSwitchMutex);
    pendingScriptSwitches.push_back({ id, action, std::move(options) });
    isScriptSwitchPending.store(true, std::memory_order_release);
}

void LuaEngine::updateSticks(float leftX, float leftY, float rightX, float rightY)
{
    sticks[0].store(leftX, std::memory_order_relaxed);
    sticks[1].store(leftY, std::memory_order_relaxed);
    sticks[2].store(rightX, std::memory_order_relaxed);
    sticks[3].store(rightY, std::memory_order_relaxed);
}

bool LuaEngine::loadScript(Script& script)
{
    script.L = lua_newstate(allocate, &script);
    script.drawnImageCount = 0;
    if (!script.L)
    {
        switchOff(script, "not enough memory");
        return false;
    }

    *static_cast<Script**>(lua_getextraspace(script.L)) = &script;
    activeScriptCount++;

    if (!runProtected(script, setupScript, &script.source, kLoadInstructionLimit))
        return false;

    LOG_DEBUGLOG(TAG, "%s: loaded (%zu bytes)", script.source.name.c_str(), script.source.source.size());

    return true;
}

void LuaEngine::reloadScripts()
{
    // Scripts that were switched off before the reset stay off. An error in on_unload() doesn't stop the reload
    std::vector<Script*> runningScripts;
    isUnloading = true;
    for (auto& script : scripts)
    {
        if (script->L)
        {
            runningScripts.push_back(script.get());
            callCallback(*script, "on_unload", nullptr, kCallbackInstructionLimit);
        }
    }
    isUnloading = false;

    for (Script* script : runningScripts)
    {
        closeScript(*script);
        script->hasTouchPosition = false;

        if (loadScript(*script))
            callCallback(*script, "on_load", gameCode.c_str(), kLoadInstructionLimit);
    }
}

void LuaEngine::applyPendingScriptSwitches()
{
    std::vector<ScriptSwitch> switches;
    {
        std::lock_guard lock(pendingScriptSwitchMutex);
        switches.swap(pendingScriptSwitches);
    }

    // In order, so a quick off-on-off ends up off
    for (ScriptSwitch& scriptSwitch : switches)
    {
        // Ids are positions in [scripts]. Switching off one that is already off (after an error, say), or on one that runs, does nothing.
        // A restart stops the script if it runs, then starts it
        if (scriptSwitch.id < 0 || scriptSwitch.id >= (int) scripts.size())
            continue;

        Script& script = *scripts[scriptSwitch.id];
        bool isRunning = script.L != nullptr;
        bool isRestart = scriptSwitch.action == ScriptSwitch::Action::Restart;
        bool mustStop = isRunning && scriptSwitch.action != ScriptSwitch::Action::Enable;
        bool mustStart = isRestart || (scriptSwitch.action == ScriptSwitch::Action::Enable && !isRunning);

        if (mustStop)
        {
            // Like when the emulator stops: requests to the app are ignored, and an error is only logged
            isUnloading = true;
            callCallback(script, "on_unload", nullptr, kCallbackInstructionLimit);
            isUnloading = false;
            closeScript(script);
            // A restart keeps the data and the state slots in memory, so it only needs saving (and the slots freeing) when the script stays off
            if (!isRestart)
            {
                saveStorage(script);
                freeStateSlots(script);
            }
        }

        if (isRestart)
        {
            script.source.options = std::move(scriptSwitch.options);
            // The slots are kept, but a load the old run queued (from on_frame_end(), say) mustn't fire after the new run's first on_frame()
            if (pendingStateLoadScript == script.id)
                pendingStateLoadScript = -1;
        }

        if (mustStart)
        {
            // Like the reset reload. A load or on_load() error switches it off again and tells the app
            script.hasTouchPosition = false;
            if (loadScript(script))
                callCallback(script, "on_load", gameCode.c_str(), kLoadInstructionLimit);
        }

        if (mustStop || mustStart)
            LOG_DEBUGLOG(TAG, "%s: %s by the player", script.source.name.c_str(), isRestart ? "restarted with new options" : (mustStart ? "switched on" : "switched off"));
    }
}

struct CallbackArguments
{
    const char* name;
    // Passed to the callback as a string, if not null
    const char* argument;
};

bool LuaEngine::callCallback(Script& script, const char* name, const char* argument, int instructionLimit)
{
    if (!script.L)
        return false;

    CallbackArguments arguments { name, argument };
    return runProtected(script, runCallback, &arguments, instructionLimit);
}

bool LuaEngine::runProtected(Script& script, lua_CFunction function, void* data, int instructionLimit)
{
    lua_State* L = script.L;
    lua_settop(L, 0);
    // None of these allocate, so they can't fail outside the protected call
    lua_pushcfunction(L, messageHandler);
    lua_pushcfunction(L, function);
    lua_pushlightuserdata(L, data);

    lua_sethook(L, instructionLimitHook, LUA_MASKCOUNT, instructionLimit);
    script.workLeft = instructionLimit * kWorkPerInstruction;
    script.limitError = nullptr;
    int result = lua_pcall(L, 1, 0, 1);
    if (result == LUA_OK)
    {
        lua_settop(L, 0);
        sendRequests(script);
        return true;
    }

    const char* error = lua_tostring(L, -1);
    std::string errorString = error ? error : (result == LUA_ERRMEM ? "not enough memory" : "unknown error");
    // Once a limit is hit, allocations fail too (the error message's among them), so the error that comes out may only say "not enough memory"
    if (script.limitError && result != LUA_ERRRUN)
        errorString = script.limitError;
    lua_settop(L, 0);
    switchOff(script, errorString);
    return false;
}

void LuaEngine::sendRequests(Script& script)
{
    if (eventMessenger)
    {
        if (script.swapScreens)
            eventMessenger->onLuaSwapScreens(script.id, *script.swapScreens);
        if (script.showControls)
            eventMessenger->onLuaShowControls(script.id, *script.showControls);
        if (script.fastForward)
            eventMessenger->onLuaFastForward(script.id, *script.fastForward);
        if (script.layoutKey)
            eventMessenger->onLuaSetLayout(script.id, *script.layoutKey);
    }

    script.clearRequests();
}

int LuaEngine::setupScript(lua_State* L)
{
    auto& source = *static_cast<const LuaScriptSource*>(lua_touserdata(L, 1));
    Script& script = getScript(L);

    // Only these libraries. No io, os, package, debug or coroutine
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_settop(L, 1);

    // The base library functions that read files or write to stderr
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushnil(L);
    lua_setglobal(L, "warn");
    // print() would go to stdout, which nothing reads
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "print");
    // Still no package library: require() only loads modules from the script folder (see luaRequire())
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kLoadedModulesKey);
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kRequireChainKey);
    lua_pushcfunction(L, luaRequire);
    lua_setglobal(L, "require");
    lua_newtable(L);
    script.drawnImagesRef = luaL_ref(L, LUA_REGISTRYINDEX);

    wrapFunction(L, nullptr, "load", loadTextOnly);
    wrapFunction(L, nullptr, "setmetatable", setMetatableWithoutGc);
    wrapFunction(L, nullptr, "collectgarbage", safeCollectGarbage);
    wrapFunction(L, LUA_STRLIBNAME, "rep", safeStringRep);
    wrapFunction(L, LUA_TABLIBNAME, "move", safeTableMove);
    wrapFunction(L, LUA_TABLIBNAME, "insert", safeTableShift);
    wrapFunction(L, LUA_TABLIBNAME, "remove", safeTableShift);
    wrapFunction(L, LUA_TABLIBNAME, "sort", safeTableSort);

    // The melon module
    static const luaL_Reg melonFunctions[] = {
        { "frame", luaFrame },
        { "log", luaLog },
        { nullptr, nullptr },
    };
    static const luaL_Reg gameFunctions[] = {
        { "code", luaGameCode },
        { "title", luaGameTitle },
        { nullptr, nullptr },
    };
    static const luaL_Reg inputFunctions[] = {
        { "held", luaInputHeld },
        { "press", luaInputPress },
        { "release", luaInputRelease },
        { "touch", luaInputTouch },
        { "set_touch", luaInputSetTouch },
        { "clear_touch", luaInputClearTouch },
        { "stick", luaInputStick },
        { nullptr, nullptr },
    };
    static const luaL_Reg arm9MemFunctions[] = {
        { "read8", luaMemRead<u8, false, true> },
        { "read16", luaMemRead<u16, false, true> },
        { "read32", luaMemRead<u32, false, true> },
        { "read_s8", luaMemRead<u8, true, true> },
        { "read_s16", luaMemRead<u16, true, true> },
        { "read_s32", luaMemRead<u32, true, true> },
        { "write8", luaMemWrite<u8, true> },
        { "write16", luaMemWrite<u16, true> },
        { "write32", luaMemWrite<u32, true> },
        { nullptr, nullptr },
    };
    static const luaL_Reg arm7MemFunctions[] = {
        { "read8", luaMemRead<u8, false, false> },
        { "read16", luaMemRead<u16, false, false> },
        { "read32", luaMemRead<u32, false, false> },
        { "read_s8", luaMemRead<u8, true, false> },
        { "read_s16", luaMemRead<u16, true, false> },
        { "read_s32", luaMemRead<u32, true, false> },
        { "write8", luaMemWrite<u8, false> },
        { "write16", luaMemWrite<u16, false> },
        { "write32", luaMemWrite<u32, false> },
        { nullptr, nullptr },
    };
    static const luaL_Reg screenFunctions[] = {
        { "swap", luaSetBoolRequest<&Script::swapScreens> },
        { "show_controls", luaSetBoolRequest<&Script::showControls> },
        { "set_layout", luaScreenSetLayout },
        { nullptr, nullptr },
    };
    static const luaL_Reg stateFunctions[] = {
        { "save", luaStateSave },
        { "load", luaStateLoad },
        { "has", luaStateHas },
        { nullptr, nullptr },
    };
    static const luaL_Reg drawFunctions[] = {
        { "pixel", luaDrawPixel },
        { "line", luaDrawLine },
        { "rect", luaDrawRect },
        { "text", luaDrawText },
        { "circle", luaDrawCircle },
        { "image", luaDrawImage },
        { nullptr, nullptr },
    };
    static const luaL_Reg imageFunctions[] = {
        { "load", luaImageLoad },
        { "new", luaImageNew },
        { nullptr, nullptr },
    };
    static const luaL_Reg imageMethods[] = {
        { "size", luaImageSize },
        { nullptr, nullptr },
    };
    static const luaL_Reg emuFunctions[] = {
        { "fast_forward", luaSetBoolRequest<&Script::fastForward> },
        { nullptr, nullptr },
    };
    static const luaL_Reg storageFunctions[] = {
        { "get", luaStorageGet },
        { "set", luaStorageSet },
        { "keys", luaStorageKeys },
        { "clear", luaStorageClear },
        { "flush", luaStorageFlush },
        { "size", luaStorageSize },
        { nullptr, nullptr },
    };
    static const struct { const char* name; u32 value; } keys[] = {
        { "A", 1 << 0 }, { "B", 1 << 1 }, { "SELECT", 1 << 2 }, { "START", 1 << 3 }, { "RIGHT", 1 << 4 }, { "LEFT", 1 << 5 },
        { "UP", 1 << 6 }, { "DOWN", 1 << 7 }, { "R", 1 << 8 }, { "L", 1 << 9 }, { "X", 1 << 10 }, { "Y", 1 << 11 },
        { "TOUCH", LuaKey::TOUCH },
    };

    luaL_newlib(L, melonFunctions);
    luaL_newlib(L, gameFunctions);
    lua_setfield(L, -2, "game");
    luaL_newlib(L, inputFunctions);
    lua_setfield(L, -2, "input");
    luaL_newlib(L, arm9MemFunctions);
    luaL_newlib(L, arm7MemFunctions);
    lua_setfield(L, -2, "arm7");
    lua_setfield(L, -2, "mem");
    luaL_newlib(L, screenFunctions);
    lua_setfield(L, -2, "screen");
    luaL_newlib(L, drawFunctions);
    lua_setfield(L, -2, "draw");
    luaL_newlib(L, imageFunctions);
    lua_setfield(L, -2, "image");
    luaL_newlib(L, emuFunctions);
    lua_setfield(L, -2, "emu");
    luaL_newlib(L, storageFunctions);
    lua_setfield(L, -2, "storage");
    luaL_newlib(L, stateFunctions);
    lua_setfield(L, -2, "state");
    // A plain table of the option values the app sent, built again at each load
    lua_createtable(L, 0, (int) source.options.size());
    for (const LuaOptionValue& option : source.options)
    {
        switch (option.type)
        {
            case LuaOptionValue::Type::Boolean:
            case LuaOptionValue::Type::Layout:
                lua_pushboolean(L, option.boolean);
                break;
            case LuaOptionValue::Type::Integer:
                lua_pushinteger(L, (lua_Integer) option.integer);
                break;
            case LuaOptionValue::Type::Number:
                lua_pushnumber(L, (lua_Number) option.number);
                break;
            case LuaOptionValue::Type::String:
                lua_pushlstring(L, option.string.data(), option.string.size());
                break;
        }
        lua_setfield(L, -2, option.key.c_str());
    }
    lua_setfield(L, -2, "options");
    lua_createtable(L, 0, sizeof(keys) / sizeof(keys[0]));
    for (const auto& key : keys)
    {
        lua_pushinteger(L, key.value);
        lua_setfield(L, -2, key.name);
    }
    lua_setfield(L, -2, "KEY");
    lua_setglobal(L, "melon");

    // The images' metatable, hidden from getmetatable() so that scripts can't change it
    luaL_newmetatable(L, kImageType);
    luaL_newlib(L, imageMethods);
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);

    // Text only, never a precompiled chunk
    if (luaL_loadbufferx(L, source.source.data(), source.source.size(), script.chunkName.c_str(), "t") != LUA_OK)
        return lua_error(L);

    lua_call(L, 0, 0);
    return 0;
}

int LuaEngine::runCallback(lua_State* L)
{
    auto& arguments = *static_cast<const CallbackArguments*>(lua_touserdata(L, 1));
    lua_settop(L, 0);

    // Every callback is optional
    if (lua_getglobal(L, arguments.name) == LUA_TNIL)
        return 0;

    int argumentCount = 0;
    if (arguments.argument)
    {
        lua_pushstring(L, arguments.argument);
        argumentCount = 1;
    }

    lua_call(L, argumentCount, 0);
    return 0;
}

void LuaEngine::rasterizeOverlay()
{
    overlay.clear();
    for (auto& script : scripts)
    {
        // Scripts that stopped during the frame have no commands left
        if (!script->L)
            continue;

        for (const DrawCommand& command : script->drawCommands)
        {
            switch (command.type)
            {
                case DrawCommand::Type::Rect:
                    overlay.fillRect(command.screen, { command.x0, command.y0, command.x1, command.y1 }, command.color);
                    break;
                case DrawCommand::Type::Outline:
                {
                    LuaOverlay::Rect sides[4];
                    LuaOverlay::getOutlineSides({ command.x0, command.y0, command.x1, command.y1 }, sides);
                    for (const LuaOverlay::Rect& side : sides)
                        overlay.fillRect(command.screen, side, command.color);
                    break;
                }
                case DrawCommand::Type::Line:
                    overlay.drawLine(command.screen, command.x0, command.y0, command.x1, command.y1, command.color);
                    break;
                case DrawCommand::Type::Text:
                    overlay.drawText(command.screen, command.x0, command.y0, script->drawText.data() + command.textOffset, command.textLength, command.color, command.background, command.x1);
                    break;
                case DrawCommand::Type::Circle:
                    overlay.drawCircle(command.screen, command.x0, command.y0, command.x1, command.y1 != 0, command.color);
                    break;
                case DrawCommand::Type::Image:
                    overlay.drawImage(command.screen, command.x0, command.y0, command.imagePixels, command.imageStride, command.imageWidth, command.imageHeight, command.x1, command.color);
                    break;
            }
        }
    }
}

void LuaEngine::switchOff(Script& script, const std::string& error)
{
    LOG_WARN(TAG, "%s was switched off: %s", script.source.name.c_str(), error.c_str());

    // The app only shows the first line, without the traceback. Errors in on_unload() are only logged, since the session is ending (or the
    // script is about to be reloaded)
    if (eventMessenger && !isUnloading)
        eventMessenger->onLuaScriptStopped(script.id, error.substr(0, error.find('\n')));

    closeScript(script);
    saveStorage(script);
    // An error in on_unload() comes during a stop, reset or switch, which decide about the slots themselves (a reset keeps them)
    if (!isUnloading)
        freeStateSlots(script);
}

void LuaEngine::closeScript(Script& script)
{
    script.clearRequests();
    script.press = 0;
    script.release = 0;
    clearDrawCommands(script);

    if (!script.L)
        return;

    // Scripts can't register __gc finalizers (setmetatable() refuses them), so closing the state runs no script code
    lua_close(script.L);
    script.L = nullptr;
    activeScriptCount--;

    // The app stops sending stick motion once no script runs, so a script switched back on later would read the last position forever
    if (activeScriptCount == 0)
        updateSticks(0.0f, 0.0f, 0.0f, 0.0f);
}

int LuaEngine::luaGameCode(lua_State* L)
{
    lua_pushstring(L, getScript(L).engine->gameCode.c_str());
    return 1;
}

int LuaEngine::luaGameTitle(lua_State* L)
{
    auto cart = getScript(L).engine->nds->NDSCartSlot.GetCart();
    if (cart)
        lua_pushlstring(L, cart->GetHeader().GameTitle, strnlen(cart->GetHeader().GameTitle, 12));
    else
        lua_pushliteral(L, "");
    return 1;
}

int LuaEngine::luaFrame(lua_State* L)
{
    lua_pushinteger(L, getScript(L).engine->currentFrame);
    return 1;
}

int LuaEngine::luaLog(lua_State* L)
{
    // Only goes anywhere while the debug log is on
    if (!DebugLog::isEnabled())
        return 0;

    int argumentCount = lua_gettop(L);
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    for (int i = 1; i <= argumentCount; i++)
    {
        if (i > 1)
            luaL_addchar(&buffer, '\t');
        luaL_tolstring(L, i, nullptr);
        luaL_addvalue(&buffer);
    }
    luaL_pushresult(&buffer);

    LOG_INFO(TAG, "%s: %s", getScript(L).source.name.c_str(), lua_tostring(L, -1));
    return 0;
}

int LuaEngine::luaInputHeld(lua_State* L)
{
    lua_pushinteger(L, getScript(L).engine->currentUserInput.held);
    return 1;
}

int LuaEngine::luaInputPress(lua_State* L)
{
    getScript(L).press |= (u32) luaL_checkinteger(L, 1) & (LuaKey::BUTTONS | LuaKey::TOUCH);
    return 0;
}

int LuaEngine::luaInputRelease(lua_State* L)
{
    getScript(L).release |= (u32) luaL_checkinteger(L, 1) & (LuaKey::BUTTONS | LuaKey::TOUCH);
    return 0;
}

int LuaEngine::luaInputTouch(lua_State* L)
{
    const LuaUserInput& userInput = getScript(L).engine->currentUserInput;
    lua_pushinteger(L, userInput.touchX);
    lua_pushinteger(L, userInput.touchY);
    lua_pushboolean(L, (userInput.held & LuaKey::TOUCH) != 0);
    return 3;
}

int LuaEngine::luaInputSetTouch(lua_State* L)
{
    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    Script& script = getScript(L);
    script.hasTouchPosition = true;
    script.touchX = (u16) std::clamp<lua_Integer>(x, 0, 255);
    script.touchY = (u16) std::clamp<lua_Integer>(y, 0, 191);
    return 0;
}

int LuaEngine::luaInputClearTouch(lua_State* L)
{
    getScript(L).hasTouchPosition = false;
    return 0;
}

int LuaEngine::luaInputStick(lua_State* L)
{
    static const char* const stickNames[] = { "left", "right", nullptr };
    int stick = luaL_checkoption(L, 1, nullptr, stickNames);
    LuaEngine* engine = getScript(L).engine;
    lua_pushnumber(L, engine->sticks[stick * 2].load(std::memory_order_relaxed));
    lua_pushnumber(L, engine->sticks[stick * 2 + 1].load(std::memory_order_relaxed));
    return 2;
}

template <typename T, bool isSigned, bool isArm9>
int LuaEngine::luaMemRead(lua_State* L)
{
    u32 address = (u32) luaL_checkinteger(L, 1) & ~(u32) (sizeof(T) - 1);
    NDS& nds = *getScript(L).engine->nds;

    T value;
    bool isTcm = false;
    if constexpr (isArm9)
        isTcm = readTcm(nds.ARM9, address, value);

    if (!isTcm)
    {
        // Reading some I/O registers has side effects (like popping the IPC FIFO), so they are off limits
        if ((address & 0xFF000000) == 0x04000000)
            return luaL_error(L, "can't read I/O register 0x%08X: reads can change the emulated state", (unsigned int) address);

        value = busRead<T>(nds, address, isArm9);
    }

    if constexpr (isSigned)
        lua_pushinteger(L, (std::make_signed_t<T>) value);
    else
        lua_pushinteger(L, value);
    return 1;
}

template <typename T, bool isArm9>
int LuaEngine::luaMemWrite(lua_State* L)
{
    u32 address = (u32) luaL_checkinteger(L, 1) & ~(u32) (sizeof(T) - 1);
    T value = (T) luaL_checkinteger(L, 2);
    NDS& nds = *getScript(L).engine->nds;

    if constexpr (isArm9)
    {
        if (writeTcm(nds, address, value))
            return 0;
    }

    busWrite<T>(nds, address, value, isArm9);
    return 0;
}

template <std::optional<bool> LuaEngine::Script::* request>
int LuaEngine::luaSetBoolRequest(lua_State* L)
{
    Script& script = getScript(L);
    if (!script.engine->isUnloading)
        script.*request = lua_toboolean(L, 1) != 0;
    return 0;
}

int LuaEngine::luaScreenSetLayout(lua_State* L)
{
    Script& script = getScript(L);
    // nil goes back to the player's layout
    if (lua_isnoneornil(L, 1))
    {
        if (!script.engine->isUnloading)
            script.layoutKey = std::string();
        return 0;
    }

    size_t keyLength;
    const char* key = luaL_checklstring(L, 1, &keyLength);
    std::string_view keyView(key, keyLength);
    bool isLayoutOption = false;
    for (const LuaOptionValue& option : script.source.options)
    {
        if (option.type == LuaOptionValue::Type::Layout && option.key == keyView)
        {
            isLayoutOption = true;
            break;
        }
    }
    // Option keys never hold a zero byte, so %s shows the whole key of any that could have matched
    if (!isLayoutOption)
        return luaL_error(L, "'%s' isn't one of this script's layout options (declare it with -- option: %s type=layout)", key, key);

    if (!script.engine->isUnloading)
        script.layoutKey = std::string(keyView);
    return 0;
}

/**
 * The slot (0 to kStateSlotCount - 1) of a melon.state call. A slot that isn't 1 to 4 is a mistake in the script, so it raises an error
 */
static int checkStateSlot(lua_State* L)
{
    lua_Integer slot = luaL_checkinteger(L, 1);
    luaL_argcheck(L, slot >= 1 && slot <= kStateSlotCount, 1, "slot must be 1 to 4");
    return (int) slot - 1;
}

/**
 * false plus [error], what melon.state calls return when they can't do what was asked
 */
static int pushStateFailure(lua_State* L, const char* error)
{
    lua_pushboolean(L, 0);
    lua_pushstring(L, error);
    return 2;
}

int LuaEngine::luaStateSave(lua_State* L)
{
    int slot = checkStateSlot(L);
    Script& script = getScript(L);
    LuaEngine* engine = script.engine;

    if (engine->isUnloading)
        return pushStateFailure(L, "can't save a state while the script unloads");
    if (!engine->savestateHost)
        return pushStateFailure(L, "savestates aren't available");
    if (script.lastStateSaveFramePass && engine->framePass - *script.lastStateSaveFramePass < kStateSaveInterval)
        return pushStateFailure(L, "saved a state too recently (a script can save once every 30 frames)");

    // Charged first: once the state is in the slot, nothing may fail. Lua errors are longjmps, which skip C++ destructors, so the block below
    // keeps its C++ objects to itself and raises nothing
    melonade_charge_work(L, kStateWork);
    script.lastStateSaveFramePass = engine->framePass;

    const char* error = nullptr;
    {
        std::vector<u8> data;
        bool isSaved = false;
        try
        {
            isSaved = engine->savestateHost->saveLuaState(data);
        }
        catch (const std::bad_alloc&)
        {
        }

        std::vector<u8>& slotData = script.stateSlots[slot];
        if (!isSaved)
        {
            error = "the state couldn't be saved";
        }
        else if (engine->stateSlotMemory - slotData.size() + data.size() > kMaxStateSlotMemory)
        {
            error = "not enough room for the state (the states of every script together are limited to 64 MB)";
        }
        else
        {
            engine->stateSlotMemory = engine->stateSlotMemory - slotData.size() + data.size();
            // The old state goes with [data]
            slotData.swap(data);
        }
    }

    if (error)
        return pushStateFailure(L, error);

    LOG_DEBUGLOG(TAG, "%s: saved state %d (%zu bytes)", script.source.name.c_str(), slot + 1, script.stateSlots[slot].size());
    lua_pushboolean(L, 1);
    return 1;
}

int LuaEngine::luaStateLoad(lua_State* L)
{
    int slot = checkStateSlot(L);
    Script& script = getScript(L);
    LuaEngine* engine = script.engine;

    if (engine->isUnloading)
        return pushStateFailure(L, "can't load a state while the script unloads");
    if (!engine->areStateLoadsAllowed)
        return pushStateFailure(L, "states can't be loaded in RetroAchievements hardcore mode");
    if (!engine->savestateHost)
        return pushStateFailure(L, "savestates aren't available");
    if (script.stateSlots[slot].empty())
    {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "slot %d is empty", slot + 1);
        return 2;
    }

    // The load itself (with its backup) runs once every script's on_frame() has run. If several are asked for before then, the last one wins
    melonade_charge_work(L, kStateWork);
    engine->pendingStateLoadScript = script.id;
    engine->pendingStateLoadSlot = slot;
    lua_pushboolean(L, 1);
    return 1;
}

int LuaEngine::luaStateHas(lua_State* L)
{
    int slot = checkStateSlot(L);
    lua_pushboolean(L, !getScript(L).stateSlots[slot].empty());
    return 1;
}

/**
 * A coordinate in DS pixels as overlay pixels, rounded to the nearest half DS pixel. [argument] is the one an error points at
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
 * 0xAARRGGBB, masked to 32 bits (so -1 is opaque white), premultiplied. There's no default alpha: 0xFF0000 is transparent
 */
static u32 checkColor(lua_State* L, int argument)
{
    return premultiplyColor((u32) luaL_checkinteger(L, argument));
}

/**
 * Adds a draw command for the running script and charges its work. Returns null when there's nothing to draw: outside on_frame() and
 * on_frame_end(), or when none of it can be seen ([pixelCount] is 0). Raises an error once the script has drawn too much this frame (both
 * callbacks together): too many calls, or more than kMaxDrawTextBytes of text once [textLength] more is added
 */
LuaEngine::DrawCommand* LuaEngine::addDrawCommand(lua_State* L, int64_t pixelCount, size_t textLength)
{
    Script& script = getScript(L);
    if (!script.engine->isDrawingAllowed || pixelCount == 0)
        return nullptr;

    if (script.drawCommands.size() >= kMaxDrawCommands)
    {
        failRestOfCall(script, kDrawLimitError);
        luaL_error(L, "%s", script.limitError);
    }
    if (textLength > kMaxDrawTextBytes - script.drawText.size())
    {
        failRestOfCall(script, kDrawTextLimitError);
        luaL_error(L, "%s", script.limitError);
    }

    melonade_charge_work(L, kDrawCallCost + pixelCount);
    return &script.drawCommands.emplace_back();
}

int LuaEngine::luaDrawPixel(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t x = checkCoordinate(L, 2);
    int64_t y = checkCoordinate(L, 3);
    u32 color = checkColor(L, 4);

    LuaOverlay::Rect rect = LuaOverlay::clipToScreen({ x, y, x + LuaOverlay::kScale, y + LuaOverlay::kScale });
    if (color == 0 || rect.isEmpty())
        return 0;

    if (DrawCommand* command = addDrawCommand(L, (rect.x1 - rect.x0) * (rect.y1 - rect.y0)))
        *command = { DrawCommand::Type::Rect, screen, (int) rect.x0, (int) rect.y0, (int) rect.x1, (int) rect.y1, color };
    return 0;
}

int LuaEngine::luaDrawLine(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t x0 = checkCoordinate(L, 2);
    int64_t y0 = checkCoordinate(L, 3);
    int64_t x1 = checkCoordinate(L, 4);
    int64_t y1 = checkCoordinate(L, 5);
    u32 color = checkColor(L, 6);

    if (color == 0 || !LuaOverlay::clipLine(x0, y0, x1, y1))
        return 0;

    if (DrawCommand* command = addDrawCommand(L, LuaOverlay::linePixelCount(x0, y0, x1, y1)))
        *command = { DrawCommand::Type::Line, screen, (int) x0, (int) y0, (int) x1, (int) y1, color };
    return 0;
}

int LuaEngine::luaDrawRect(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number width = luaL_checknumber(L, 4);
    lua_Number height = luaL_checknumber(L, 5);
    u32 color = checkColor(L, 6);
    bool isFilled = lua_toboolean(L, 7) != 0;

    // The far edges are rounded on their own, so that rectangles side by side meet. A width or height of 0 or less draws nothing
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
        if (DrawCommand* command = addDrawCommand(L, LuaOverlay::clippedArea(rect)))
            *command = { DrawCommand::Type::Rect, screen, (int) clipped.x0, (int) clipped.y0, (int) clipped.x1, (int) clipped.y1, color };
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
        if (DrawCommand* command = addDrawCommand(L, pixelCount))
        {
            *command = {
                DrawCommand::Type::Outline, screen,
                (int) std::clamp<int64_t>(rect.x0, -margin, LuaOverlay::kScreenWidth + margin),
                (int) std::clamp<int64_t>(rect.y0, -margin, LuaOverlay::kScreenHeight + margin),
                (int) std::clamp<int64_t>(rect.x1, -margin, LuaOverlay::kScreenWidth + margin),
                (int) std::clamp<int64_t>(rect.y1, -margin, LuaOverlay::kScreenHeight + margin),
                color,
            };
        }
    }
    return 0;
}

int LuaEngine::luaDrawText(lua_State* L)
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
        // The box's area covers the glyphs as well as the background. Text whose box can be seen starts at an int: a frame's text is at most
        // kMaxDrawTextBytes, and glyphs are at most 14 pixels wide at size 1
        LuaOverlay::Rect box = { x, y, x + width, y + size.lineCount * LuaOverlay::kLineHeight * textSize };
        if (DrawCommand* command = addDrawCommand(L, LuaOverlay::clippedArea(box), length))
        {
            Script& script = getScript(L);
            *command = { DrawCommand::Type::Text, screen, (int) x, (int) y, (int) textSize, 0, color, background, script.drawText.size(), length };
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

int LuaEngine::luaDrawCircle(lua_State* L)
{
    LuaOverlay::Screen screen = checkScreen(L, 1);
    int64_t middleX = checkCoordinate(L, 2);
    int64_t middleY = checkCoordinate(L, 3);
    int64_t radius = checkCoordinate(L, 4);
    u32 color = checkColor(L, 5);
    bool isFilled = lua_toboolean(L, 6) != 0;

    // A radius of 0 or less (after rounding to half a pixel) draws nothing. A circle whose square around it is off-screen can't be seen either,
    // which also keeps the middle close enough to the screen for getCircleRowSpans()
    radius = std::min(radius, LuaOverlay::kMaxCircleRadius);
    if (color == 0 || radius <= 0 || LuaOverlay::clippedArea({ middleX - radius, middleY - radius, middleX + radius, middleY + radius }) == 0)
        return 0;
    if (!getScript(L).engine->isDrawingAllowed)
        return 0;

    // Counting the pixels and drawing them each go through every row that can be seen, even where the circle has no pixels on it (a ring
    // around the screen, say), so the rows are charged twice before counting, on top of addDrawCommand()'s charge for the pixels
    int64_t rowCount = std::clamp<int64_t>(middleY + radius, 0, LuaOverlay::kScreenHeight) - std::clamp<int64_t>(middleY - radius, 0, LuaOverlay::kScreenHeight);
    melonade_charge_work(L, kDrawCallCost + 2 * rowCount);

    if (DrawCommand* command = addDrawCommand(L, LuaOverlay::circlePixelCount(middleX, middleY, radius, isFilled)))
        *command = { DrawCommand::Type::Circle, screen, (int) middleX, (int) middleY, (int) radius, isFilled ? 1 : 0, color };
    return 0;
}

int LuaEngine::luaDrawImage(lua_State* L)
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
    Script& script = getScript(L);
    if (pixelCount == 0 || !script.engine->isDrawingAllowed)
        return 0;

    // Anchored before the command is added, so that no command can point at an image that could be collected. The table only holds them until
    // the next frame, and at most one per command (plus the one whose command went over the limit)
    lua_rawgeti(L, LUA_REGISTRYINDEX, script.drawnImagesRef);
    lua_pushvalue(L, 4);
    lua_rawseti(L, -2, script.drawnImageCount + 1);
    script.drawnImageCount++;
    lua_pop(L, 1);

    // The destination can be seen, so its top left is at most an image's width (1024 * kMaxImageScale) outside the screen, and fits in an int
    if (DrawCommand* command = addDrawCommand(L, pixelCount * kImageStepsPerPixel))
    {
        *command = { DrawCommand::Type::Image, screen, (int) destinationX, (int) destinationY, scale, 0, alpha };
        command->imagePixels = image->pixels + clippedY0 * image->width + clippedX0;
        command->imageStride = image->width;
        command->imageWidth = (int) (clippedX1 - clippedX0);
        command->imageHeight = (int) (clippedY1 - clippedY0);
    }
    return 0;
}

static const LuaFolderContent::Module* findModule(const LuaFolderContent* folderContent, const char* name, size_t length)
{
    if (!folderContent)
        return nullptr;

    auto module = folderContent->modules.find(std::string(name, length));
    return module == folderContent->modules.end() ? nullptr : &module->second;
}

static const LuaFolderContent::Image* findImage(const LuaFolderContent* folderContent, const char* path, size_t length)
{
    if (!folderContent)
        return nullptr;

    auto image = folderContent->images.find(std::string(path, length));
    return image == folderContent->images.end() ? nullptr : &image->second;
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
int LuaEngine::luaRequire(lua_State* L)
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

    const LuaFolderContent::Module* module = findModule(getScript(L).engine->folderContent.get(), name, nameLength);
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
    lua_pushcfunction(L, messageHandler);
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
int LuaEngine::luaImageLoad(lua_State* L)
{
    size_t pathLength;
    const char* path = luaL_checklstring(L, 1, &pathLength);
    const LuaFolderContent::Image* image = findImage(getScript(L).engine->folderContent.get(), path, pathLength);

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
int LuaEngine::luaImageNew(lua_State* L)
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

int LuaEngine::luaImageSize(lua_State* L)
{
    auto* image = static_cast<const LuaImage*>(luaL_checkudata(L, 1, kImageType));
    lua_pushinteger(L, image->width);
    lua_pushinteger(L, image->height);
    return 2;
}

/**
 * The key at index 1. Only real strings: numbers aren't turned into keys
 */
static std::string_view checkStorageKey(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);
    size_t length;
    const char* key = lua_tolstring(L, 1, &length);
    if (length > LuaStorage::kMaxKeyLength)
        luaL_error(L, "storage keys are limited to 256 bytes");
    return { key, length };
}

static size_t getStorageEntrySize(std::string_view key, std::string_view value)
{
    return LuaStorage::kBlobEntryOverhead + key.size() + value.size();
}

// Lua errors are longjmps, which skip C++ destructors, so these functions keep no C++ object with memory of its own across a call that can
// raise one: encoding goes into the store's scratch string, and the error is raised once nothing is left to clean up

int LuaEngine::luaStorageGet(lua_State* L)
{
    std::string_view key = checkStorageKey(L);
    LuaStorage::Store& store = getScript(L).storage;
    auto entry = store.entries.find(key);
    if (entry == store.entries.end())
    {
        lua_pushnil(L);
        return 1;
    }

    // A fresh copy each time, so changing it doesn't change what's stored
    melonade_charge_work(L, (lua_Integer) entry->second.size() + 1);
    LuaStorage::pushValue(L, entry->second);
    return 1;
}

int LuaEngine::luaStorageSet(lua_State* L)
{
    std::string_view key = checkStorageKey(L);
    LuaStorage::Store& store = getScript(L).storage;
    auto entry = store.entries.find(key);

    if (lua_isnoneornil(L, 2))
    {
        if (entry != store.entries.end())
        {
            store.size -= getStorageEntrySize(entry->first, entry->second);
            store.entries.erase(entry);
            store.isDirty = true;
        }
        return 0;
    }

    // The room left for the value once the key's old value is gone
    size_t sizeWithoutEntry = store.size - (entry != store.entries.end() ? getStorageEntrySize(entry->first, entry->second) : 0);
    size_t sizeWithEmptyEntry = sizeWithoutEntry + getStorageEntrySize(key, {});
    size_t limit = sizeWithEmptyEntry < LuaStorage::kMaxSize ? LuaStorage::kMaxSize - sizeWithEmptyEntry : 0;

    int badType = 0;
    LuaStorage::EncodeError error = LuaStorage::encodeValue(L, 2, store.scratch, limit, badType);
    size_t encodedSize = store.scratch.size();
    if (error != LuaStorage::EncodeError::None)
        store.scratch.clear();

    // Charged before the value is kept. If the budget runs out here, the storage stays as it was
    melonade_charge_work(L, (lua_Integer) encodedSize + 1);
    if (error == LuaStorage::EncodeError::ValueType)
        return luaL_error(L, LuaStorage::getErrorMessage(error), lua_typename(L, badType));
    if (error != LuaStorage::EncodeError::None)
        return luaL_error(L, "%s", LuaStorage::getErrorMessage(error));

    if (entry != store.entries.end())
    {
        store.size -= getStorageEntrySize(entry->first, entry->second);
        entry->second.swap(store.scratch);
        store.size += getStorageEntrySize(entry->first, entry->second);
    }
    else
    {
        store.size += getStorageEntrySize(key, store.scratch);
        store.entries.emplace(std::string(key), std::move(store.scratch));
    }
    store.scratch.clear();
    // Don't keep a big buffer around for one big value
    if (store.scratch.capacity() > 16 * 1024)
        std::string().swap(store.scratch);
    store.isDirty = true;
    return 0;
}

int LuaEngine::luaStorageKeys(lua_State* L)
{
    LuaStorage::Store& store = getScript(L).storage;
    lua_Integer work = (lua_Integer) store.entries.size() + 1;
    for (const auto& entry : store.entries)
        work += (lua_Integer) entry.first.size();
    melonade_charge_work(L, work);

    // In key order
    lua_createtable(L, (int) store.entries.size(), 0);
    lua_Integer index = 1;
    for (const auto& entry : store.entries)
    {
        lua_pushlstring(L, entry.first.data(), entry.first.size());
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaEngine::luaStorageClear(lua_State* L)
{
    LuaStorage::Store& store = getScript(L).storage;
    store.clear();
    // Also when it was empty already: data that couldn't be read is replaced too
    store.isDirty = true;
    return 0;
}

int LuaEngine::luaStorageFlush(lua_State* L)
{
    // Saved at the end of the frame, if anything changed
    getScript(L).storage.isFlushRequested = true;
    return 0;
}

int LuaEngine::luaStorageSize(lua_State* L)
{
    const LuaStorage::Store& store = getScript(L).storage;
    lua_pushinteger(L, store.entries.empty() ? 0 : (lua_Integer) store.size);
    lua_pushinteger(L, (lua_Integer) LuaStorage::kMaxSize);
    return 2;
}

}
