#ifndef LUAENGINE_H
#define LUAENGINE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "MelonEventMessenger.h"
#include "NDS.h"

// From lua.h, which is only included by LuaEngine.cpp
struct lua_State;
typedef int (*lua_CFunction)(lua_State* L);

namespace MelonDSAndroid
{

struct LuaScriptSource
{
    std::string name;
    std::string source;
};

/**
 * Buttons as scripts see them: pressed = bit set. The bits match the DS key mask (A = bit 0 ... Y = bit 11), and TOUCH is the touchscreen
 */
namespace LuaKey
{
constexpr melonDS::u32 BUTTONS = 0xFFF;
constexpr melonDS::u32 TOUCH = 1 << 12;
}

/**
 * The player's input before the scripts change it. [held] uses the LuaKey bits
 */
struct LuaUserInput
{
    melonDS::u32 held = 0;
    melonDS::u16 touchX = 0;
    melonDS::u16 touchY = 0;
};

/**
 * What the scripts want for the next frame: the final buttons are (held & ~release) | press, with LuaKey bits. [touchX]/[touchY] replace the
 * player's touch position while [hasTouchPosition] is set
 */
struct LuaInputOverride
{
    melonDS::u32 press = 0;
    melonDS::u32 release = 0;
    bool hasTouchPosition = false;
    melonDS::u16 touchX = 0;
    melonDS::u16 touchY = 0;
};

/**
 * Runs the game's Lua scripts (API v1, the `melon` module). Each script gets its own sandboxed Lua state, so one that fails can be switched off
 * without touching the others. A script that throws an error or runs for too long in a callback is switched off for the session, and the app
 * is told why. Scripts are told apart by their position in the list given to [setScripts] (their id).
 *
 * Callbacks, all optional: on_load(game_code) once the game starts, on_frame() just before each frame runs (the only place where input and
 * memory changes line up with the frame), on_state_loaded() after a savestate or rewind state is loaded, and on_unload() when the emulator stops.
 * When the console is reset, every running script is reloaded from scratch (on_unload(), then the main chunk and on_load() again).
 * melon.input.press()/release() only last for the next frame, so on_frame() calls them every frame. melon.input.set_touch() keeps its position
 * until clear_touch(), and moves the touch (the player's, or one pressed with KEY.TOUCH). held() and touch() are the player's own input.
 * melon.screen.* and melon.emu.* requests are sent to the app once the callback returns (the last value of each kind wins), and are ignored
 * during on_unload().
 *
 * Everything runs on the emulator thread, except [setScripts] (before the emulator thread starts), [notifyStateLoaded], [notifyReset],
 * [disableScript] and [updateSticks] (any thread).
 */
class LuaEngine
{
public:
    struct Script;

    LuaEngine();
    ~LuaEngine();

    void setScripts(std::vector<LuaScriptSource> scripts);
    /**
     * Loads the scripts given to [setScripts] and calls their on_load(game). Called once the game has started
     */
    void start(melonDS::NDS* nds, std::shared_ptr<MelonEventMessenger> eventMessenger);
    /**
     * Whether any script is still running. While none is, the emulator doesn't need to call [runFrame]
     */
    bool isActive() const { return activeScriptCount > 0; }
    /**
     * Reloads the scripts if the console was reset, calls on_state_loaded() if a state was loaded since the last frame, then on_frame(), and
     * returns how the scripts want to change the input of the frame that is about to run. Called just before the frame runs
     */
    LuaInputOverride runFrame(int frame, const LuaUserInput& userInput);
    /**
     * Calls on_unload() and closes every script
     */
    void stop();

    /**
     * Calls on_state_loaded() before the next frame. Savestates and rewind load on other threads, so the call is only queued
     */
    void notifyStateLoaded() { isStateLoadPending.store(true, std::memory_order_relaxed); }
    /**
     * Reloads the running scripts before the next frame. Reset runs on another thread, so it's only queued
     */
    void notifyReset() { isResetPending.store(true, std::memory_order_relaxed); }
    /**
     * Switches a script off for the rest of the session because the player asked to: on_unload() runs, and the app isn't told (it already
     * knows). The app runs on another thread, so it's only queued and applied before the next frame
     */
    void disableScript(int id);
    /**
     * Controller sticks from the app, from -1 to 1 (down and right are positive). Only sent while a script runs
     */
    void updateSticks(float leftX, float leftY, float rightX, float rightY);

private:
    bool loadScript(Script& script);
    void reloadScripts();
    void applyPendingDisables();
    bool callCallback(Script& script, const char* name, const char* argument, int instructionLimit);
    bool runProtected(Script& script, lua_CFunction function, void* data, int instructionLimit);
    void sendRequests(Script& script);
    void switchOff(Script& script, const std::string& error);
    void closeScript(Script& script);

    static int setupScript(lua_State* L);
    static int runCallback(lua_State* L);

    // API functions
    static int luaGameCode(lua_State* L);
    static int luaGameTitle(lua_State* L);
    static int luaFrame(lua_State* L);
    static int luaLog(lua_State* L);
    static int luaInputHeld(lua_State* L);
    static int luaInputPress(lua_State* L);
    static int luaInputRelease(lua_State* L);
    static int luaInputTouch(lua_State* L);
    static int luaInputSetTouch(lua_State* L);
    static int luaInputClearTouch(lua_State* L);
    static int luaInputStick(lua_State* L);
    template <typename T, bool isSigned, bool isArm9> static int luaMemRead(lua_State* L);
    template <typename T, bool isArm9> static int luaMemWrite(lua_State* L);
    static int luaScreenSwap(lua_State* L);
    static int luaScreenShowControls(lua_State* L);
    static int luaEmuFastForward(lua_State* L);

    std::vector<LuaScriptSource> pendingScripts;
    std::vector<std::unique_ptr<Script>> scripts;
    int activeScriptCount = 0;
    // Set while on_unload() runs: requests to the app are ignored, and errors are only logged
    bool isUnloading = false;

    melonDS::NDS* nds = nullptr;
    std::shared_ptr<MelonEventMessenger> eventMessenger;
    std::string gameCode;
    int currentFrame = 0;
    LuaUserInput currentUserInput;

    std::atomic_bool isStateLoadPending = false;
    std::atomic_bool isResetPending = false;
    // Script ids from disableScript(). The flag saves locking the mutex every frame
    std::mutex pendingDisableMutex;
    std::vector<int> pendingDisabledScripts;
    std::atomic_bool isDisablePending = false;
    std::atomic<float> sticks[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

}

#endif //LUAENGINE_H
