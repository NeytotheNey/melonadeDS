#include "LuaEngine.h"
#include <algorithm>
#include <cstring>
#include <string_view>
#include "LuaEngineInternal.h"
#include "debug/DebugLog.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// VM instructions a script may run per call. The main chunk and on_load() get more room, since they may build tables
static const int kLoadInstructionLimit = 10'000'000;
static const int kCallbackInstructionLimit = 1'000'000;
// melon.storage: changed data is snapshotted at least this often, in frames (about 10 seconds)
static const int kStorageSaveInterval = 600;

/**
 * A new table on the stack with [functions] in it, like luaL_newlib() for a table whose size isn't known here
 */
static void newLibrary(lua_State* L, const luaL_Reg* functions)
{
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
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
        auto script = std::make_unique<LuaScript>();
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
        LuaScript& loadedScript = *scripts.emplace_back(std::move(script));

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
        LuaDrawApi::clearCommands(*script);

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
    LuaScript& script = *scripts[id];
    if (!script.L || script.stateSlots[slot].empty())
        return;

    bool isLoaded = callSavestateHost([&] { return savestateHost->loadLuaState(script.stateSlots[slot]); });
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

void LuaEngine::freeStateSlots(LuaScript& script)
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

void LuaEngine::saveStorage(LuaScript& script)
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

bool LuaEngine::loadScript(LuaScript& script)
{
    script.L = lua_newstate(LuaSandbox::allocate, &script);
    script.drawnImageCount = 0;
    if (!script.L)
    {
        switchOff(script, "not enough memory");
        return false;
    }

    *static_cast<LuaScript**>(lua_getextraspace(script.L)) = &script;
    activeScriptCount++;

    if (!runProtected(script, setupScript, &script.source, kLoadInstructionLimit))
        return false;

    LOG_DEBUGLOG(TAG, "%s: loaded (%zu bytes)", script.source.name.c_str(), script.source.source.size());

    return true;
}

void LuaEngine::reloadScripts()
{
    // Scripts that were switched off before the reset stay off. An error in on_unload() doesn't stop the reload
    std::vector<LuaScript*> runningScripts;
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

    for (LuaScript* script : runningScripts)
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

        LuaScript& script = *scripts[scriptSwitch.id];
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

bool LuaEngine::callCallback(LuaScript& script, const char* name, const char* argument, int instructionLimit)
{
    if (!script.L)
        return false;

    CallbackArguments arguments { name, argument };
    return runProtected(script, runCallback, &arguments, instructionLimit);
}

bool LuaEngine::runProtected(LuaScript& script, lua_CFunction function, void* data, int instructionLimit)
{
    lua_State* L = script.L;
    lua_settop(L, 0);
    // None of these allocate, so they can't fail outside the protected call
    lua_pushcfunction(L, LuaSandbox::messageHandler);
    lua_pushcfunction(L, function);
    lua_pushlightuserdata(L, data);

    LuaSandbox::beginCall(script, instructionLimit);
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

void LuaEngine::sendRequests(LuaScript& script)
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
    LuaScript& script = getScript(L);

    LuaSandbox::open(L);
    // print() would go to stdout, which nothing reads
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "print");
    LuaFolderApi::install(L);
    LuaDrawApi::install(L, script);

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
    static const luaL_Reg screenFunctions[] = {
        { "swap", luaSetBoolRequest<&LuaScript::swapScreens> },
        { "show_controls", luaSetBoolRequest<&LuaScript::showControls> },
        { "set_layout", luaScreenSetLayout },
        { nullptr, nullptr },
    };
    static const luaL_Reg emuFunctions[] = {
        { "fast_forward", luaSetBoolRequest<&LuaScript::fastForward> },
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
    newLibrary(L, LuaMemoryApi::arm9Functions);
    newLibrary(L, LuaMemoryApi::arm7Functions);
    lua_setfield(L, -2, "arm7");
    lua_setfield(L, -2, "mem");
    luaL_newlib(L, screenFunctions);
    lua_setfield(L, -2, "screen");
    newLibrary(L, LuaDrawApi::functions);
    lua_setfield(L, -2, "draw");
    newLibrary(L, LuaFolderApi::imageFunctions);
    lua_setfield(L, -2, "image");
    luaL_newlib(L, emuFunctions);
    lua_setfield(L, -2, "emu");
    newLibrary(L, LuaStorageApi::functions);
    lua_setfield(L, -2, "storage");
    newLibrary(L, LuaStateApi::functions);
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
        if (script->L)
            LuaDrawApi::rasterize(*script, overlay);
    }
}

void LuaEngine::switchOff(LuaScript& script, const std::string& error)
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

void LuaEngine::closeScript(LuaScript& script)
{
    script.clearRequests();
    script.press = 0;
    script.release = 0;
    LuaDrawApi::clearCommands(script);

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
    LuaScript& script = getScript(L);
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

template <std::optional<bool> LuaScript::* request>
int LuaEngine::luaSetBoolRequest(lua_State* L)
{
    LuaScript& script = getScript(L);
    if (!script.engine->isUnloading)
        script.*request = lua_toboolean(L, 1) != 0;
    return 0;
}

int LuaEngine::luaScreenSetLayout(lua_State* L)
{
    LuaScript& script = getScript(L);
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

}
