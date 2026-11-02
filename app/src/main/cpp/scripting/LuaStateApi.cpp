#include <vector>
#include "LuaEngineInternal.h"
#include "debug/DebugLog.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// melon.state: a save copies the whole emulator state, which takes milliseconds, so each script can only save once every kStateSaveInterval
// frames, and saves and loads are charged kStateWork steps
static const int kStateSaveInterval = 30;
static const lua_Integer kStateWork = 1'000'000;
static const size_t kMaxStateSlotMemory = 64 * 1024 * 1024;

/**
 * The slot (0 to kStateSlotCount - 1) of a melon.state call, raising an error for one that isn't 1 to 4
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

int LuaStateApi::save(lua_State* L)
{
    int slot = checkStateSlot(L);
    LuaScript& script = getScript(L);
    LuaEngine* engine = script.engine;

    if (engine->isUnloading)
        return pushStateFailure(L, "can't save a state while the script unloads");
    if (!engine->savestateHost)
        return pushStateFailure(L, "savestates aren't available");
    if (script.lastStateSaveFramePass && engine->framePass - *script.lastStateSaveFramePass < kStateSaveInterval)
        return pushStateFailure(L, "saved a state too recently (a script can save once every 30 frames)");

    // Charged first, since the block below must raise nothing: Lua errors are longjmps, which would skip its C++ destructors
    melonade_charge_work(L, kStateWork);
    script.lastStateSaveFramePass = engine->framePass;

    const char* error = nullptr;
    {
        std::vector<u8> data;
        bool isSaved = callSavestateHost([&] { return engine->savestateHost->saveLuaState(data); });

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

int LuaStateApi::load(lua_State* L)
{
    int slot = checkStateSlot(L);
    LuaScript& script = getScript(L);
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

    // The load itself (with its backup) runs once every script's on_frame() has run, and the last one asked for wins
    melonade_charge_work(L, kStateWork);
    engine->pendingStateLoadScript = script.id;
    engine->pendingStateLoadSlot = slot;
    lua_pushboolean(L, 1);
    return 1;
}

int LuaStateApi::has(lua_State* L)
{
    int slot = checkStateSlot(L);
    lua_pushboolean(L, !getScript(L).stateSlots[slot].empty());
    return 1;
}

const luaL_Reg LuaStateApi::functions[] = {
    { "save", save },
    { "load", load },
    { "has", has },
    { nullptr, nullptr },
};

}
