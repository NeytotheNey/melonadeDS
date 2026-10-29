#include "LuaEngine.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include "MelonLog.h"
#include "lua.hpp"
#include "debug/DebugLog.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// Threat model: scripts are files the player picked and installed. The limits below are there so that a buggy or careless script can't freeze
// the emulator thread or the app (it gets switched off instead). They aren't meant to stop a script written to hang its own player's game.

static const char* const TAG = "melonDS-lua";

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
};

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

void LuaEngine::setScripts(std::vector<LuaScriptSource> newScripts)
{
    pendingScripts = std::move(newScripts);
}

void LuaEngine::start(NDS* newNds, std::shared_ptr<MelonEventMessenger> newEventMessenger)
{
    nds = newNds;
    eventMessenger = std::move(newEventMessenger);

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

    // Before a pending reset, so a script the player switched off isn't loaded again
    if (isDisablePending.exchange(false, std::memory_order_acquire))
        applyPendingDisables();

    if (isResetPending.exchange(false, std::memory_order_relaxed))
        reloadScripts();

    if (isStateLoadPending.exchange(false, std::memory_order_relaxed))
    {
        for (auto& script : scripts)
            callCallback(*script, "on_state_loaded", nullptr, kCallbackInstructionLimit);
    }

    for (auto& script : scripts)
        callCallback(*script, "on_frame", nullptr, kCallbackInstructionLimit);

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

    return inputOverride;
}

void LuaEngine::stop()
{
    isUnloading = true;
    for (auto& script : scripts)
        callCallback(*script, "on_unload", nullptr, kCallbackInstructionLimit);

    for (auto& script : scripts)
        closeScript(*script);
    scripts.clear();
    isUnloading = false;

    // The ids belong to this session's scripts
    std::lock_guard lock(pendingDisableMutex);
    pendingDisabledScripts.clear();
    isDisablePending.store(false, std::memory_order_relaxed);
}

void LuaEngine::disableScript(int id)
{
    std::lock_guard lock(pendingDisableMutex);
    pendingDisabledScripts.push_back(id);
    isDisablePending.store(true, std::memory_order_release);
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
    if (!script.L)
    {
        switchOff(script, "not enough memory");
        return false;
    }

    *static_cast<Script**>(lua_getextraspace(script.L)) = &script;
    activeScriptCount++;

    if (!runProtected(script, setupScript, &script.source, kLoadInstructionLimit))
        return false;

    if (DebugLog::isEnabled()) [[unlikely]]
        LOG_INFO(TAG, "%s: loaded (%zu bytes)", script.source.name.c_str(), script.source.source.size());

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

void LuaEngine::applyPendingDisables()
{
    std::vector<int> ids;
    {
        std::lock_guard lock(pendingDisableMutex);
        ids.swap(pendingDisabledScripts);
    }

    for (int id : ids)
    {
        // Ids are positions in [scripts]. One that is already off (after an error, say) is skipped
        if (id < 0 || id >= (int) scripts.size() || !scripts[id]->L)
            continue;

        Script& script = *scripts[id];
        // Like when the emulator stops: requests to the app are ignored, and an error is only logged
        isUnloading = true;
        callCallback(script, "on_unload", nullptr, kCallbackInstructionLimit);
        isUnloading = false;
        closeScript(script);

        if (DebugLog::isEnabled()) [[unlikely]]
            LOG_INFO(TAG, "%s: switched off by the player", script.source.name.c_str());
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
            eventMessenger->onLuaSwapScreens(*script.swapScreens);
        if (script.showControls)
            eventMessenger->onLuaShowControls(*script.showControls);
        if (script.fastForward)
            eventMessenger->onLuaFastForward(*script.fastForward);
    }

    script.swapScreens.reset();
    script.showControls.reset();
    script.fastForward.reset();
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
        { "swap", luaScreenSwap },
        { "show_controls", luaScreenShowControls },
        { nullptr, nullptr },
    };
    static const luaL_Reg emuFunctions[] = {
        { "fast_forward", luaEmuFastForward },
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
    luaL_newlib(L, emuFunctions);
    lua_setfield(L, -2, "emu");
    lua_createtable(L, 0, sizeof(keys) / sizeof(keys[0]));
    for (const auto& key : keys)
    {
        lua_pushinteger(L, key.value);
        lua_setfield(L, -2, key.name);
    }
    lua_setfield(L, -2, "KEY");
    lua_setglobal(L, "melon");

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

void LuaEngine::switchOff(Script& script, const std::string& error)
{
    LOG_WARN(TAG, "%s was switched off: %s", script.source.name.c_str(), error.c_str());

    // The app only shows the first line, without the traceback. Errors in on_unload() are only logged, since the session is ending (or the
    // script is about to be reloaded)
    if (eventMessenger && !isUnloading)
        eventMessenger->onLuaScriptStopped(script.id, error.substr(0, error.find('\n')));

    closeScript(script);
}

void LuaEngine::closeScript(Script& script)
{
    script.swapScreens.reset();
    script.showControls.reset();
    script.fastForward.reset();
    script.press = 0;
    script.release = 0;

    if (!script.L)
        return;

    // Scripts can't register __gc finalizers (setmetatable() refuses them), so closing the state runs no script code
    lua_close(script.L);
    script.L = nullptr;
    activeScriptCount--;
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

int LuaEngine::luaScreenSwap(lua_State* L)
{
    Script& script = getScript(L);
    if (!script.engine->isUnloading)
        script.swapScreens = lua_toboolean(L, 1) != 0;
    return 0;
}

int LuaEngine::luaScreenShowControls(lua_State* L)
{
    Script& script = getScript(L);
    if (!script.engine->isUnloading)
        script.showControls = lua_toboolean(L, 1) != 0;
    return 0;
}

int LuaEngine::luaEmuFastForward(lua_State* L)
{
    Script& script = getScript(L);
    if (!script.engine->isUnloading)
        script.fastForward = lua_toboolean(L, 1) != 0;
    return 0;
}

}
