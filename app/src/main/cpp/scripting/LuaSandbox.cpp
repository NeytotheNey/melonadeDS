#include <cstdlib>
#include <cstring>
#include "LuaEngineInternal.h"

namespace MelonDSAndroid
{

// Limits keep a buggy script from freezing the emulator; they aren't a security boundary

// C functions don't count instructions, so the work they do (pattern matching and searches in lua/patched/lstrlib.c, bytes allocated, table
// elements moved or sorted) is charged to a budget that scales with each call's instruction limit
static const lua_Integer kWorkPerInstruction = 20;
// Steps per table element that table.move/insert/remove/sort go through
static const lua_Integer kTableElementCost = 4;
// Bytes allocated per step
static const size_t kBytesPerStep = 16;
static const size_t kMemoryLimit = 32 * 1024 * 1024;
// Most elements table.move/insert/remove/sort may go through in one call, more than the memory limit could hold anyway
static const lua_Integer kMaxTableLoop = 1 << 22;
static const char* const kInstructionLimitError = "ran for too long (over the instruction limit)";
static const char* const kWorkLimitError = "did too much work in one call (over the work limit)";

static void instructionLimitHook(lua_State* L, lua_Debug*);

/**
 * Stops the current call once a limit is hit: every instruction fails from now on, so that the script can't catch the error with pcall and
 * keep going
 */
static void failRestOfCall(LuaScript& script, const char* error)
{
    if (!script.limitError)
        script.limitError = error;
    lua_sethook(script.L, instructionLimitHook, LUA_MASKCOUNT, 1);
}

void LuaSandbox::raiseLimitError(lua_State* L, LuaScript& script, const char* error)
{
    failRestOfCall(script, error);
    luaL_error(L, "%s", script.limitError);
    std::abort();
}

void* LuaSandbox::allocate(void* userData, void* pointer, size_t oldSize, size_t newSize)
{
    auto& script = *static_cast<LuaScript*>(userData);
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

        // Allocating and filling memory is work too (string.rep, concatenation in a loop), once the state is created (L is set)
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
    LuaSandbox::raiseLimitError(L, getScript(L), kInstructionLimitError);
}

extern "C" void melonade_charge_work(lua_State* L, lua_Integer amount)
{
    LuaScript& script = getScript(L);
    script.workLeft -= amount;
    if (script.workLeft < 0) [[unlikely]]
        LuaSandbox::raiseLimitError(L, script, kWorkLimitError);
}

int LuaSandbox::messageHandler(lua_State* L)
{
    const char* message = lua_tostring(L, 1);
    if (message == nullptr)
    {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            message = lua_tostring(L, -1);
        else
            message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    // Already has one: a module's error that require() passed on, whose traceback already has every level this one would
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
 * load() that only takes text chunks, since binary chunks can crash the VM
 */
static int loadTextOnly(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    // Pad to two arguments so index 2 is the chunk name (or nil), not the original load pushed below, and pass env only if the caller gave
    // one, since load treats an explicit nil env as "set _ENV to nil"
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
 * setmetatable() that refuses __gc, since finalizers run with hooks off and the instruction limit couldn't stop one that loops forever
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
 * collectgarbage() without the options that change how the collector runs, like "stop", which would let memory run into the limit
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
 * The length that table.insert/remove/sort go up to, capped and without __len, since a sparse table or __len can make #t huge without the table
 * holding anything
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

void LuaSandbox::open(lua_State* L)
{
    // Only these libraries: no io, os, package, debug or coroutine
    static const struct { const char* name; lua_CFunction open; } libraries[] = {
        { LUA_GNAME, luaopen_base },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    for (const auto& library : libraries)
    {
        luaL_requiref(L, library.name, library.open, 1);
        lua_pop(L, 1);
    }

    // The base library functions that read files or write to stderr
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushnil(L);
    lua_setglobal(L, "warn");

    wrapFunction(L, nullptr, "load", loadTextOnly);
    wrapFunction(L, nullptr, "setmetatable", setMetatableWithoutGc);
    wrapFunction(L, nullptr, "collectgarbage", safeCollectGarbage);
    wrapFunction(L, LUA_STRLIBNAME, "rep", safeStringRep);
    wrapFunction(L, LUA_TABLIBNAME, "move", safeTableMove);
    wrapFunction(L, LUA_TABLIBNAME, "insert", safeTableShift);
    wrapFunction(L, LUA_TABLIBNAME, "remove", safeTableShift);
    wrapFunction(L, LUA_TABLIBNAME, "sort", safeTableSort);
}

void LuaSandbox::beginCall(LuaScript& script, int instructionLimit)
{
    lua_sethook(script.L, instructionLimitHook, LUA_MASKCOUNT, instructionLimit);
    script.workLeft = instructionLimit * kWorkPerInstruction;
    script.limitError = nullptr;
}

}
