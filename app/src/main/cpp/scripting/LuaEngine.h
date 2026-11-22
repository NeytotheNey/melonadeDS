#ifndef LUAENGINE_H
#define LUAENGINE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "LuaDraw.h"
#include "LuaStorage.h"
#include "MelonEventMessenger.h"
#include "NDS.h"
#include "gamestate/GameState.h"

// From lua.h, which is only included by LuaEngine.cpp
struct lua_State;
typedef int (*lua_CFunction)(lua_State* L);

namespace MelonDSAndroid
{

/**
 * A value of one of the script's options (melon.options), already checked against its declaration by the app
 */
struct LuaOptionValue
{
    enum class Type
    {
        Boolean,
        Integer,
        Number,
        String,
        // A layout the player picked (or "Don't change"), which melon.screen.set_layout() names by its key; scripts only see [boolean]
        Layout,
    };

    std::string key;
    Type type = Type::Boolean;
    bool boolean = false;
    int64_t integer = 0;
    double number = 0.0;
    std::string string;
};

struct LuaScriptSource
{
    std::string name;
    std::string source;
    // The script's saved melon.storage blob (see LuaStorage::decodeBlob()), empty if it has none and dropped once decoded
    std::string savedData;
    // Where the app keeps the script's data, sent back with each snapshot; empty means storage isn't saved
    std::string dataGameKey;
    std::vector<LuaOptionValue> options;
};

/**
 * What the scripts can use from their folder besides themselves: modules for require(), by module name, and images for melon.image.load(), by
 * path; read once at launch and never changed, so every script's state shares it
 */
struct LuaFolderContent
{
    struct Module
    {
        // In the folder, like "lib/util.lua"
        std::string path;
        std::optional<std::string> source;
        std::string leftOutReason;
    };

    struct Image
    {
        int width = 0;
        int height = 0;
        // 0xAARRGGBB, premultiplied, empty when left out
        std::vector<melonDS::u32> pixels;
        std::string leftOutReason;
    };

    std::unordered_map<std::string, Module> modules;
    std::unordered_map<std::string, Image> images;
};

/**
 * Buttons as scripts see them, a set bit meaning pressed: the DS key mask bits (A = bit 0 ... Y = bit 11) plus TOUCH for the touchscreen
 */
namespace LuaKey
{
constexpr melonDS::u32 BUTTONS = 0xFFF;
constexpr melonDS::u32 TOUCH = 1 << 12;
}

/**
 * The player's input before the scripts change it, [held] in LuaKey bits
 */
struct LuaUserInput
{
    melonDS::u32 held = 0;
    melonDS::u16 touchX = 0;
    melonDS::u16 touchY = 0;
};

/**
 * What the scripts want for the next frame: the final buttons are (held & ~release) | press, with LuaKey bits
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
 * Saves and loads the emulator's state for melon.state, so that LuaEngine never touches Savestate. Implemented by MelonInstance and only called
 * on the emulator thread, between frames
 */
class LuaSavestateHost
{
public:
    virtual ~LuaSavestateHost() = default;
    /**
     * Saves the whole state into [data] (resized to fit), returning false and leaving [data] as it was if it couldn't
     */
    virtual bool saveLuaState(std::vector<melonDS::u8>& data) = 0;
    /**
     * Loads a state [saveLuaState] made and returns whether it did, going back to a backup of the current state if the load fails
     */
    virtual bool loadLuaState(const std::vector<melonDS::u8>& data) = 0;
};

struct LuaScript;

/**
 * Runs the game's Lua scripts: one sandboxed Lua state per script, their callbacks around each frame, and the `melon` module they see
 * (MODS.md, "Lua runtime" and "API"). Everything runs on the emulator thread, except [setScripts] (before the emulator thread starts),
 * [notifyStateLoaded], [notifyReset], [disableScript], [enableScript], [restartScript], [updateSticks] and [updateWideCamera] (any thread).
 */
class LuaEngine
{
public:
    LuaEngine();
    ~LuaEngine();

    /**
     * [areStateLoadsAllowed] is false when savestates can't be loaded in this session (RetroAchievements hardcore), so melon.state.load()
     * refuses too
     */
    void setScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed);
    /**
     * Loads the scripts given to [setScripts] and calls their on_load(game) once the game has started; [savestateHost] and [gameState]
     * (melon.game.state()) must outlive the scripts (until [stop])
     */
    void start(melonDS::NDS* nds, std::shared_ptr<MelonEventMessenger> eventMessenger, LuaSavestateHost* savestateHost, GameState* gameState);
    /**
     * Whether any script is still running, or the player switched one on or off since the last frame (which [runFrame] applies)
     */
    bool isActive() const { return activeScriptCount > 0 || isScriptSwitchPending.load(std::memory_order_relaxed); }
    /**
     * Reloads the scripts if the console was reset, calls on_state_loaded() if a state was loaded since the last frame, then on_frame(), and
     * returns how the scripts want to change the input of the frame that is about to run
     */
    LuaInputOverride runFrame(int frame, const LuaUserInput& userInput);
    /**
     * Calls on_frame_end(), then draws the frame's drawings into [getOverlay]; must follow every [runFrame], also once no script is left
     * running, so that the last drawings go away
     */
    void endFrame();
    /**
     * Calls on_unload() and closes every script
     */
    void stop();

    /**
     * Calls on_state_loaded() before the next frame
     */
    void notifyStateLoaded() { isStateLoadPending.store(true, std::memory_order_relaxed); }
    /**
     * Reloads the running scripts before the next frame
     */
    void notifyReset() { isResetPending.store(true, std::memory_order_relaxed); }
    /**
     * Switches a script off because the player asked to: on_unload() runs, and the app isn't told (it already knows)
     */
    void disableScript(int id);
    /**
     * Switches a stopped script back on because the player asked to, the way a reset reloads it: a fresh state, its main chunk, then
     * on_load(game)
     */
    void enableScript(int id);
    /**
     * Restarts a script, or starts a stopped one, with new option values: on_unload() if it runs, then a fresh state, its main chunk and
     * on_load(game)
     */
    void restartScript(int id, std::vector<LuaOptionValue> options);
    /**
     * Snapshots the storage of every script whose storage changed since its last snapshot, when the emulator thread parks for a pause
     */
    void flushStorage();
    /**
     * Controller sticks from the app, from -1 to 1 (down and right are positive)
     */
    void updateSticks(float leftX, float leftY, float rightX, float rightY);
    /**
     * The WideMelon camera's window as melon.wide.camera() returns it: its offset from center in DS pixels, its zoom (1 = native) and its
     * offset down from center in DS pixels
     */
    void updateWideCamera(float x, float zoom, float y);
    /**
     * What the scripts drew for the frame [endFrame] was last called for, empty while no script runs
     */
    const LuaOverlay& getOverlay() const { return overlay; }
    /**
     * Whether a running script's depth of field, haze, 3D-only glow, shading or sun shadows are on, as the app was last told
     * (melon.screen.depth_of_field, haze, bloom's only_3d, shade and sun), so the frames should keep their depth; emulator thread only
     */
    bool wantsDepth() const;
    /**
     * The sun shadows of the running script that asked for them last (melon.screen.sun), as the app was last told, for the OpenGL renderer to
     * draw the characters' own; off while none asks. Emulator thread only
     */
    melonDS::SunShadows sunShadows() const;

private:
    // The API files (LuaEngineInternal.h) read the engine's state through the running script's LuaScript::engine
    friend struct LuaMemoryApi;
    friend struct LuaDrawApi;
    friend struct LuaFolderApi;
    friend struct LuaStateApi;
    friend struct LuaWideApi;
    friend struct LuaGameApi;

    // From disableScript(), enableScript() and restartScript(), applied in the order they were asked for
    struct ScriptSwitch
    {
        enum class Action
        {
            Disable,
            Enable,
            Restart,
        };

        int id;
        Action action;
        // For Restart
        std::vector<LuaOptionValue> options;
    };

    bool loadScript(LuaScript& script);
    void reloadScripts();
    void queueScriptSwitch(int id, ScriptSwitch::Action action, std::vector<LuaOptionValue> options = {});
    void applyPendingScriptSwitches();
    bool callCallback(LuaScript& script, const char* name, const char* argument, int instructionLimit);
    bool runProtected(LuaScript& script, lua_CFunction function, void* data, int instructionLimit);
    void sendRequests(LuaScript& script);
    void switchOff(LuaScript& script, const std::string& error);
    void closeScript(LuaScript& script);
    void saveStorage(LuaScript& script);
    void freeStateSlots(LuaScript& script);
    void runPendingStateLoad();
    void rasterizeOverlay();

    static int setupScript(lua_State* L);
    static int runCallback(lua_State* L);

    // melon.game, melon.input, melon.screen, melon.emu and melon.log, the parts of the module not in the Lua*Api.cpp files
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
    // melon.screen.swap(), melon.screen.show_controls() and melon.emu.fast_forward(): a boolean request to the app, in [request]
    template <std::optional<bool> LuaScript::* request> static int luaSetBoolRequest(lua_State* L);
    static int luaScreenSetLayout(lua_State* L);
    static int luaScreenTiltShift(lua_State* L);
    static int luaScreenBloom(lua_State* L);
    static int luaScreenVignette(lua_State* L);
    static int luaScreenHaze(lua_State* L);
    static int luaScreenDepthOfField(lua_State* L);
    static int luaScreenShade(lua_State* L);
    static int luaScreenSun(lua_State* L);

    std::vector<LuaScriptSource> pendingScripts;
    // Outlives every script's state, which may point into its images
    std::shared_ptr<const LuaFolderContent> folderContent;
    std::vector<std::unique_ptr<LuaScript>> scripts;
    int activeScriptCount = 0;
    // The sun shadow requests sent so far, which orders them (LuaScript::sentSunOrder)
    unsigned int sentSunRequests = 0;
    // Set while on_unload() runs: requests to the app are ignored, and errors are only logged
    bool isUnloading = false;
    // Set while on_frame() or on_frame_end() runs, the only callbacks whose draw calls are recorded
    bool isDrawingAllowed = false;
    LuaOverlay overlay;

    melonDS::NDS* nds = nullptr;
    std::shared_ptr<MelonEventMessenger> eventMessenger;
    LuaSavestateHost* savestateHost = nullptr;
    GameState* gameState = nullptr;
    bool areStateLoadsAllowed = true;
    // Bytes held by every script's state slots together, capped at kMaxStateSlotMemory
    size_t stateSlotMemory = 0;
    // The load melon.state.load() queued, run once every script's on_frame() has run (the last one asked for wins), or -1
    int pendingStateLoadScript = -1;
    int pendingStateLoadSlot = 0;
    // A script's load ran, so on_state_loaded() is due before the next on_frame()
    bool isScriptStateLoaded = false;
    // Counts runFrame() calls for the save cooldown, since melon.frame() can stand still or go back (rewind)
    int64_t framePass = 0;
    std::string gameCode;
    int currentFrame = 0;
    LuaUserInput currentUserInput;
    // Frames since changed storage was last snapshotted on a timer
    int framesSinceStorageSave = 0;

    std::atomic_bool isStateLoadPending = false;
    std::atomic_bool isResetPending = false;
    std::mutex pendingScriptSwitchMutex;
    std::vector<ScriptSwitch> pendingScriptSwitches;
    std::atomic_bool isScriptSwitchPending = false;
    std::atomic<float> sticks[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    // updateWideCamera()'s x, zoom and y
    std::atomic<float> wideCamera[3] = { 0.0f, 1.0f, 0.0f };
};

}

#endif //LUAENGINE_H
