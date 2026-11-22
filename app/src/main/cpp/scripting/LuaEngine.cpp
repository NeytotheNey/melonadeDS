#include "LuaEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include "LuaEngineInternal.h"
#include "debug/DebugLog.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// VM instructions a script may run per call, with more room for the main chunk and on_load(), which may build tables
static const int kLoadInstructionLimit = 10'000'000;
static const int kCallbackInstructionLimit = 1'000'000;
// melon.storage: changed data is snapshotted at least this often, in frames (about 10 seconds)
static const int kStorageSaveInterval = 600;
// melon.screen.tilt_shift(): the largest blur radius in DS pixels, where the blur's samples are 2 texels apart at 1x, the most bilinear filtering can bridge
static const lua_Number kMaxTiltShiftRadius = 12.0;
// melon.screen.tilt_shift(): the largest saturation and contrast multipliers
static const lua_Number kMaxTiltShiftBoost = 2.0;
// melon.screen.bloom(): the strongest glow, and its widest radius in DS pixels, where the blur's samples are 2 texels apart, the most bilinear
// filtering can bridge
static const lua_Number kMaxBloomStrength = 2.0;
static const lua_Number kMaxBloomRadius = 8.0;
// melon.screen.depth_of_field(): the widest blur in DS pixels, and how far outside the 256 x 192 view its focus point can be, a screen's size
// either way, which covers WideMelon's wings and margins
static const lua_Number kMaxDepthOfFieldRadius = 12.0;
static const lua_Number kMinDepthOfFieldFocusX = -256.0;
static const lua_Number kMaxDepthOfFieldFocusX = 512.0;
static const lua_Number kMinDepthOfFieldFocusY = -192.0;
static const lua_Number kMaxDepthOfFieldFocusY = 384.0;
// melon.screen.shade(): the depth blur's radius in DS pixels, as many taps on each side of its passes
static const lua_Number kMinShadeRadius = 1.0;
static const lua_Number kMaxShadeRadius = 16.0;
// melon.screen.sun(): the march's longest reach in view-space units (White's far plane is 1025), its most steps (the shader's loop bound),
// the largest bias in units, the widest softening blur in DS pixels (the blur passes' tap bound), and the largest near distance in units
static const lua_Number kMinSunLength = 1.0;
static const lua_Number kMaxSunLength = 1024.0;
static const lua_Number kMaxSunSteps = 128.0;
static const lua_Number kMaxSunBias = 64.0;
static const lua_Number kMaxSunSoftness = 8.0;
static const lua_Number kMaxSunNear = 1024.0;

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

void LuaEngine::start(NDS* newNds, std::shared_ptr<MelonEventMessenger> newEventMessenger, LuaSavestateHost* newSavestateHost, GameState* newGameState)
{
    nds = newNds;
    eventMessenger = std::move(newEventMessenger);
    savestateHost = newSavestateHost;
    gameState = newGameState;

    if (auto cart = nds->NDSCartSlot.GetCart())
        gameCode.assign(cart->GetHeader().GameCode, strnlen(cart->GetHeader().GameCode, 4));

    for (auto& source : pendingScripts)
    {
        auto script = std::make_unique<LuaScript>();
        script->engine = this;
        script->id = (int) scripts.size();
        // Lua cuts chunk names to 59 bytes, which could split a UTF-8 character in the error the player sees, so cut it here
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

    // A load from melon.state.load(), now that every script's on_frame() has run, after which the frame runs with the player's own input
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
    // press()/release() calls here are kept for the next frame's input, since runFrame() only clears them once they've been applied
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
    // Only a wake-up, needed only when the outbox was empty, since the app drains it itself
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

void LuaEngine::updateWideCamera(float x, float zoom, float y)
{
    wideCamera[0].store(x, std::memory_order_relaxed);
    wideCamera[1].store(zoom, std::memory_order_relaxed);
    wideCamera[2].store(y, std::memory_order_relaxed);
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
    // Scripts that were switched off before the reset stay off, and an error in on_unload() doesn't stop the reload
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
        // Ids are positions in [scripts]; switching off one that is already off (after an error, say), or on one that runs, does nothing
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
            // Like the reset reload, so a load or on_load() error switches it off again and tells the app
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
        if (script.tiltShift && *script.tiltShift != script.sentTiltShift)
        {
            script.sentTiltShift = *script.tiltShift;
            const LuaTiltShift& tiltShift = script.sentTiltShift;
            eventMessenger->onLuaTiltShift(script.id, tiltShift.top, tiltShift.bottom, tiltShift.center, tiltShift.width, tiltShift.radius, tiltShift.saturation, tiltShift.contrast, tiltShift.color);
        }
        if (script.bloom != script.sentBloom)
        {
            script.sentBloom = script.bloom;
            const LuaBloom& bloom = script.sentBloom;
            eventMessenger->onLuaBloom(script.id, bloom.strength, bloom.threshold, bloom.radius, bloom.vignette, bloom.vignetteSize, bloom.onlyThreeD,
                bloom.haze, (int) bloom.hazeColor, bloom.hazeStart, bloom.hazeFalloff);
        }
        if (script.depthOfField != script.sentDepthOfField)
        {
            script.sentDepthOfField = script.depthOfField;
            const LuaDepthOfField& dof = script.sentDepthOfField;
            eventMessenger->onLuaDepthOfField(script.id, dof.strength, dof.radius, dof.focusX, dof.focusY, dof.focusDepth, dof.range, dof.falloff);
        }
        if (script.shade != script.sentShade)
        {
            script.sentShade = script.shade;
            const LuaShade& shade = script.sentShade;
            eventMessenger->onLuaShade(script.id, shade.strength, shade.radius, shade.range, (int) shade.color);
        }
        if (script.sun != script.sentSun)
        {
            script.sentSun = script.sun;
            script.sentSunOrder = ++sentSunRequests;
            const LuaSun& sun = script.sentSun;
            eventMessenger->onLuaSun(script.id, sun.strength, sun.direction, sun.up, sun.length, sun.steps, sun.bias, sun.softness, (int) sun.color,
                sun.nearLimit);
        }
        if (script.wideCamera != script.sentWideCamera)
        {
            script.sentWideCamera = script.wideCamera;
            const LuaWideCamera& camera = script.sentWideCamera;
            eventMessenger->onLuaWideCamera(script.id, camera.zoom, camera.hasPan, camera.pan, camera.holdNative, camera.hasPanY, camera.panY);
        }
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
        { "state", LuaGameApi::state },
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
        { "tilt_shift", luaScreenTiltShift },
        { "bloom", luaScreenBloom },
        { "vignette", luaScreenVignette },
        { "haze", luaScreenHaze },
        { "depth_of_field", luaScreenDepthOfField },
        { "shade", luaScreenShade },
        { "sun", luaScreenSun },
        { nullptr, nullptr },
    };
    static const luaL_Reg gpuFunctions[] = {
        { "hide_textures", luaGpuHideTextures },
        { "light_textures", luaGpuLightTextures },
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
    luaL_newlib(L, gpuFunctions);
    lua_setfield(L, -2, "gpu");
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
    newLibrary(L, LuaWideApi::functions);
    lua_setfield(L, -2, "wide");
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

    // The app only shows the first line, without the traceback, and isn't told about errors in on_unload()
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

    // Always sent while the blur or the color boost is on, even from on_unload() (an error there closes the script during a reset, and the app isn't told about either), or
    // the old run's blur would stay and the new run's tilt_shift(0, 0) would count as no change. It changes nothing for a script the app knows has
    // stopped, or once the session has ended, since it only drops that script's own request
    if (script.sentTiltShift != LuaTiltShift())
    {
        LuaTiltShift off;
        if (eventMessenger)
            eventMessenger->onLuaTiltShift(script.id, off.top, off.bottom, off.center, off.width, off.radius, off.saturation, off.contrast, off.color);
        script.sentTiltShift = off;
    }
    // The same for the bloom, vignette and haze, which a reloaded script starts without, and the haze and a 3D-only glow also stop the frames
    // keeping their depth
    script.bloom = LuaBloom();
    if (script.sentBloom != LuaBloom())
    {
        LuaBloom off;
        if (eventMessenger)
            eventMessenger->onLuaBloom(script.id, off.strength, off.threshold, off.radius, off.vignette, off.vignetteSize, off.onlyThreeD, off.haze,
                (int) off.hazeColor, off.hazeStart, off.hazeFalloff);
        script.sentBloom = off;
    }
    // The same for the depth of field, which also stops the frames keeping their depth
    script.depthOfField = LuaDepthOfField();
    if (script.sentDepthOfField != LuaDepthOfField())
    {
        LuaDepthOfField off;
        if (eventMessenger)
            eventMessenger->onLuaDepthOfField(script.id, off.strength, off.radius, off.focusX, off.focusY, off.focusDepth, off.range, off.falloff);
        script.sentDepthOfField = off;
    }
    // The same for the shading, which also stops the frames keeping their depth
    script.shade = LuaShade();
    if (script.sentShade != LuaShade())
    {
        LuaShade off;
        if (eventMessenger)
            eventMessenger->onLuaShade(script.id, off.strength, off.radius, off.range, (int) off.color);
        script.sentShade = off;
    }
    // The same for the sun shadows
    script.sun = LuaSun();
    if (script.sentSun != LuaSun())
    {
        LuaSun off;
        if (eventMessenger)
            eventMessenger->onLuaSun(script.id, off.strength, off.direction, off.up, off.length, off.steps, off.bias, off.softness, (int) off.color,
                off.nearLimit);
        script.sentSun = off;
    }
    // A reloaded script starts hiding and lighting nothing
    script.hiddenTextures.clear();
    script.lightTextures.clear();
    // The same for the camera: a reloaded script starts asking for nothing
    script.wideCamera = LuaWideCamera();
    if (script.sentWideCamera != LuaWideCamera())
    {
        LuaWideCamera off;
        if (eventMessenger)
            eventMessenger->onLuaWideCamera(script.id, off.zoom, off.hasPan, off.pan, off.holdNative, off.hasPanY, off.panY);
        script.sentWideCamera = off;
    }

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
    // Only goes anywhere while the debug log is on, and within its budget, which is checked before the line is built
    if (!DebugLog::isEnabled() || !DebugLog::takeLuaLogBudget())
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

    LOG_INFO_QUEUED(TAG, "%s: %s", getScript(L).source.name.c_str(), lua_tostring(L, -1));
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

/**
 * Argument [argument] clamped to 0..[max], or an error if it's not a number
 */
static float checkStrength(lua_State* L, int argument, lua_Number max)
{
    lua_Number value = luaL_checknumber(L, argument);
    if (std::isnan(value))
        luaL_argerror(L, argument, "number expected, got NaN");
    return (float) std::clamp<lua_Number>(value, 0.0, max);
}

/**
 * The [name] field of the options table at index [table] clamped to [min]..[max], or [defaultValue] if it's nil
 */
static float getNumberOptionInRange(lua_State* L, int table, const char* name, float defaultValue, lua_Number min, lua_Number max)
{
    float result = defaultValue;
    if (lua_getfield(L, table, name) != LUA_TNIL)
    {
        int isNumber;
        lua_Number value = lua_tonumberx(L, -1, &isNumber);
        if (!isNumber || std::isnan(value))
            luaL_error(L, "%s must be a number", name);
        result = (float) std::clamp<lua_Number>(value, min, max);
    }
    lua_pop(L, 1);
    return result;
}

/**
 * The [name] field of the options table at index [table] clamped to 0..[max], or [defaultValue] if it's nil
 */
static float getNumberOption(lua_State* L, int table, const char* name, float defaultValue, lua_Number max)
{
    return getNumberOptionInRange(L, table, name, defaultValue, 0.0, max);
}

/**
 * The [name] field of the options table at index [table], or [defaultValue] if it's nil
 */
static bool getBooleanOption(lua_State* L, int table, const char* name, bool defaultValue)
{
    bool result = defaultValue;
    if (lua_getfield(L, table, name) != LUA_TNIL)
    {
        if (!lua_isboolean(L, -1))
            luaL_error(L, "%s must be a boolean", name);
        result = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    return result;
}

/**
 * The low 24 bits (RRGGBB) of the [name] field of the options table at index [table], so that an ARGB color works too, or [defaultValue] if
 * it's nil
 */
static u32 getColorOption(lua_State* L, int table, const char* name, u32 defaultValue)
{
    u32 result = defaultValue;
    if (lua_getfield(L, table, name) != LUA_TNIL)
    {
        int isInteger;
        lua_Integer value = lua_tointegerx(L, -1, &isInteger);
        if (!isInteger)
            luaL_error(L, "%s must be a whole number (0xRRGGBB)", name);
        result = (u32) value & 0xFFFFFF;
    }
    lua_pop(L, 1);
    return result;
}

int LuaEngine::luaScreenTiltShift(lua_State* L)
{
    LuaScript& script = getScript(L);
    LuaTiltShift tiltShift;
    tiltShift.top = checkStrength(L, 1, 1.0);
    tiltShift.bottom = checkStrength(L, 2, 1.0);
    if (!lua_isnoneornil(L, 3))
    {
        luaL_checktype(L, 3, LUA_TTABLE);
        tiltShift.center = getNumberOption(L, 3, "center", tiltShift.center, 1.0);
        tiltShift.width = getNumberOption(L, 3, "width", tiltShift.width, 1.0);
        tiltShift.radius = getNumberOption(L, 3, "radius", tiltShift.radius, kMaxTiltShiftRadius);
        tiltShift.saturation = getNumberOption(L, 3, "saturation", tiltShift.saturation, kMaxTiltShiftBoost);
        tiltShift.contrast = getNumberOption(L, 3, "contrast", tiltShift.contrast, kMaxTiltShiftBoost);
        tiltShift.color = getNumberOption(L, 3, "color", tiltShift.color, 1.0);
    }

    // Off is off, whatever the options are, so going over them while it's off costs no events. It's off with neither a blur nor a color boost to draw
    float strongerBand = std::max(tiltShift.top, tiltShift.bottom);
    float colorStrength = tiltShift.color < 0.0f ? strongerBand : tiltShift.color;
    bool hasBlur = strongerBand > 0.0f && tiltShift.radius > 0.0f;
    bool hasColorBoost = colorStrength > 0.0f && (tiltShift.saturation != 1.0f || tiltShift.contrast != 1.0f);
    if (!hasBlur && !hasColorBoost)
        tiltShift = LuaTiltShift();

    if (!script.engine->isUnloading)
        script.tiltShift = tiltShift;
    return 0;
}

int LuaEngine::luaScreenBloom(lua_State* L)
{
    LuaScript& script = getScript(L);
    const LuaBloom defaults;
    float strength = checkStrength(L, 1, kMaxBloomStrength);
    float threshold = defaults.threshold;
    float radius = defaults.radius;
    bool onlyThreeD = defaults.onlyThreeD;
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        threshold = getNumberOption(L, 2, "threshold", threshold, 1.0);
        radius = getNumberOption(L, 2, "radius", radius, kMaxBloomRadius);
        onlyThreeD = getBooleanOption(L, 2, "only_3d", onlyThreeD);
    }

    // A glow that draws nothing is the default part, so going over the options while it's off costs no events
    if (strength <= 0.0f || radius <= 0.0f)
    {
        strength = defaults.strength;
        threshold = defaults.threshold;
        radius = defaults.radius;
        onlyThreeD = defaults.onlyThreeD;
    }

    if (!script.engine->isUnloading)
    {
        script.bloom.strength = strength;
        script.bloom.threshold = threshold;
        script.bloom.radius = radius;
        script.bloom.onlyThreeD = onlyThreeD;
    }
    return 0;
}

int LuaEngine::luaScreenVignette(lua_State* L)
{
    LuaScript& script = getScript(L);
    const LuaBloom defaults;
    float vignette = checkStrength(L, 1, 1.0);
    float size = defaults.vignetteSize;
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        size = getNumberOption(L, 2, "size", size, 1.0);
    }

    // The same for the vignette's half
    if (vignette <= 0.0f)
    {
        vignette = defaults.vignette;
        size = defaults.vignetteSize;
    }

    if (!script.engine->isUnloading)
    {
        script.bloom.vignette = vignette;
        script.bloom.vignetteSize = size;
    }
    return 0;
}

int LuaEngine::luaScreenHaze(lua_State* L)
{
    LuaScript& script = getScript(L);
    const LuaBloom defaults;
    float haze = checkStrength(L, 1, 1.0);
    u32 color = defaults.hazeColor;
    float start = defaults.hazeStart;
    float falloff = defaults.hazeFalloff;
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        color = getColorOption(L, 2, "color", color);
        start = getNumberOption(L, 2, "start", start, 1.0);
        falloff = getNumberOption(L, 2, "falloff", falloff, 1.0);
    }

    // The same for the haze's part
    if (haze <= 0.0f)
    {
        haze = defaults.haze;
        color = defaults.hazeColor;
        start = defaults.hazeStart;
        falloff = defaults.hazeFalloff;
    }

    if (!script.engine->isUnloading)
    {
        script.bloom.haze = haze;
        script.bloom.hazeColor = color;
        script.bloom.hazeStart = start;
        script.bloom.hazeFalloff = falloff;
    }
    return 0;
}

int LuaEngine::luaScreenDepthOfField(lua_State* L)
{
    LuaScript& script = getScript(L);
    LuaDepthOfField depthOfField;
    depthOfField.strength = checkStrength(L, 1, 1.0);
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        depthOfField.radius = getNumberOption(L, 2, "radius", depthOfField.radius, kMaxDepthOfFieldRadius);
        depthOfField.focusX = getNumberOptionInRange(L, 2, "focus_x", depthOfField.focusX, kMinDepthOfFieldFocusX, kMaxDepthOfFieldFocusX);
        depthOfField.focusY = getNumberOptionInRange(L, 2, "focus_y", depthOfField.focusY, kMinDepthOfFieldFocusY, kMaxDepthOfFieldFocusY);
        // Absent, the focus is the depth at the focus point
        depthOfField.focusDepth = getNumberOption(L, 2, "focus_depth", depthOfField.focusDepth, 1.0);
        depthOfField.range = getNumberOption(L, 2, "range", depthOfField.range, 1.0);
        depthOfField.falloff = getNumberOption(L, 2, "falloff", depthOfField.falloff, 1.0);
    }

    // Off is off, whatever the options are, so going over them while it's off costs no events
    if (depthOfField.strength <= 0.0f || depthOfField.radius <= 0.0f)
        depthOfField = LuaDepthOfField();

    if (!script.engine->isUnloading)
        script.depthOfField = depthOfField;
    return 0;
}

int LuaEngine::luaScreenShade(lua_State* L)
{
    LuaScript& script = getScript(L);
    LuaShade shade;
    shade.strength = checkStrength(L, 1, 1.0);
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        shade.radius = getNumberOptionInRange(L, 2, "radius", shade.radius, kMinShadeRadius, kMaxShadeRadius);
        shade.range = getNumberOption(L, 2, "range", shade.range, 1.0);
        shade.color = getColorOption(L, 2, "color", shade.color);
    }

    // Off is off, whatever the options are, so going over them while it's off costs no events
    if (shade.strength <= 0.0f)
        shade = LuaShade();

    if (!script.engine->isUnloading)
        script.shade = shade;
    return 0;
}

/**
 * The [name] field of the options table at index [table] as a direction: a table of three numbers, normalized, or [defaultValue] if it's nil.
 * Anything else, or a vector too short to have a direction, is an error
 */
static std::array<float, 3> getDirectionOption(lua_State* L, int table, const char* name, const std::array<float, 3>& defaultValue)
{
    std::array<float, 3> result = defaultValue;
    if (lua_getfield(L, table, name) != LUA_TNIL)
    {
        if (!lua_istable(L, -1))
            luaL_error(L, "%s must be a table of three numbers", name);
        lua_Number vector[3];
        for (int i = 0; i < 3; i++)
        {
            lua_rawgeti(L, -1, i + 1);
            int isNumber;
            vector[i] = lua_tonumberx(L, -1, &isNumber);
            if (!isNumber || !std::isfinite(vector[i]))
                luaL_error(L, "%s must be a table of three numbers", name);
            lua_pop(L, 1);
        }
        lua_Number length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        if (!(length > 1e-6) || !std::isfinite(length))
            luaL_error(L, "%s must not be zero", name);
        for (int i = 0; i < 3; i++)
            result[i] = (float) (vector[i] / length);
    }
    lua_pop(L, 1);
    return result;
}

int LuaEngine::luaScreenSun(lua_State* L)
{
    LuaScript& script = getScript(L);
    LuaSun sun;
    sun.strength = checkStrength(L, 1, 1.0);
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        sun.direction = getDirectionOption(L, 2, "direction", sun.direction);
        sun.up = getDirectionOption(L, 2, "up", sun.up);
        sun.length = getNumberOptionInRange(L, 2, "length", sun.length, kMinSunLength, kMaxSunLength);
        sun.steps = (int) std::lround(getNumberOptionInRange(L, 2, "steps", (float) sun.steps, 1.0, kMaxSunSteps));
        sun.bias = getNumberOption(L, 2, "bias", sun.bias, kMaxSunBias);
        sun.softness = getNumberOption(L, 2, "softness", sun.softness, kMaxSunSoftness);
        sun.color = getColorOption(L, 2, "color", sun.color);
        sun.nearLimit = getNumberOption(L, 2, "near", sun.nearLimit, kMaxSunNear);
    }

    // Off is off, whatever the options are, so going over them while it's off costs no events
    if (sun.strength <= 0.0f)
        sun = LuaSun();

    if (!script.engine->isUnloading)
        script.sun = sun;
    return 0;
}

melonDS::SunShadows LuaEngine::sunShadows() const
{
    // The newest request wins, as in the app (LuaSunRequests). A stopped script's was set back to off when it closed
    const LuaScript* newest = nullptr;
    for (const auto& script : scripts)
    {
        if (script->sentSun.strength > 0.0f && (!newest || script->sentSunOrder > newest->sentSunOrder))
            newest = script.get();
    }
    melonDS::SunShadows sun;
    if (newest)
    {
        sun.On = true;
        sun.Direction = newest->sentSun.direction;
        sun.Up = newest->sentSun.up;
    }
    return sun;
}

int LuaEngine::luaGpuHideTextures(lua_State* L)
{
    return setTextureList(L, "hide_textures", melonDS::MaxHiddenTextures, &LuaScript::hiddenTextures, &LuaScript::hiddenTexturesOrder,
                          &LuaEngine::hiddenTextureRequests);
}

int LuaEngine::luaGpuLightTextures(lua_State* L)
{
    return setTextureList(L, "light_textures", melonDS::MaxLightTextures, &LuaScript::lightTextures, &LuaScript::lightTexturesOrder,
                          &LuaEngine::lightTextureRequests);
}

int LuaEngine::setTextureList(lua_State* L, const char* name, size_t maxHashes, std::vector<melonDS::u32> LuaScript::* list,
                              unsigned int LuaScript::* order, unsigned int LuaEngine::* requests)
{
    LuaScript& script = getScript(L);
    std::array<melonDS::u32, std::max(melonDS::MaxHiddenTextures, melonDS::MaxLightTextures)> hashes;
    maxHashes = std::min(maxHashes, hashes.size());
    size_t count = 0;
    // nil or {} lists nothing
    if (!lua_isnoneornil(L, 1))
    {
        luaL_checktype(L, 1, LUA_TTABLE);
        const lua_Unsigned length = lua_rawlen(L, 1);
        if (length > maxHashes)
            luaL_error(L, "%s takes at most %d hashes", name, (int) maxHashes);
        for (lua_Unsigned i = 1; i <= length; i++)
        {
            lua_rawgeti(L, 1, (lua_Integer) i);
            int isInteger;
            const lua_Integer hash = lua_tointegerx(L, -1, &isInteger);
            if (!isInteger || hash < 1 || hash > 0xFFFFFFFF)
                luaL_error(L, "a texture hash must be a whole number from 1 to 0xFFFFFFFF");
            lua_pop(L, 1);
            hashes[count++] = (melonDS::u32) hash;
        }
    }

    std::vector<melonDS::u32>& current = script.*list;
    if (script.engine->isUnloading || std::equal(hashes.begin(), hashes.begin() + count, current.begin(), current.end()))
        return 0;
    current.assign(hashes.begin(), hashes.begin() + count);
    if (count)
        script.*order = ++(script.engine->*requests);
    return 0;
}

const std::vector<melonDS::u32>& LuaEngine::newestTextureList(std::vector<melonDS::u32> LuaScript::* list, unsigned int LuaScript::* order) const
{
    // The newest list wins, as for the sun. A stopped script's was cleared when it closed
    static const std::vector<melonDS::u32> none;
    const LuaScript* newest = nullptr;
    for (const auto& script : scripts)
    {
        if (!((*script).*list).empty() && (!newest || (*script).*order > newest->*order))
            newest = script.get();
    }
    return newest ? newest->*list : none;
}

const std::vector<melonDS::u32>& LuaEngine::hiddenTextures() const
{
    return newestTextureList(&LuaScript::hiddenTextures, &LuaScript::hiddenTexturesOrder);
}

const std::vector<melonDS::u32>& LuaEngine::lightTextures() const
{
    return newestTextureList(&LuaScript::lightTextures, &LuaScript::lightTexturesOrder);
}

bool LuaEngine::wantsDepth() const
{
    for (const auto& script : scripts)
    {
        // A stopped script's were set back to off when it closed. only_3d is on by default, so it only counts while the glow is on
        const LuaBloom& bloom = script->sentBloom;
        if (script->sentDepthOfField.strength > 0.0f || script->sentShade.strength > 0.0f || script->sentSun.strength > 0.0f ||
            bloom.haze > 0.0f || (bloom.strength > 0.0f && bloom.onlyThreeD))
            return true;
    }
    return false;
}

}
