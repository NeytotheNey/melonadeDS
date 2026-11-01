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
        // A layout the player picked (or "Don't change"), which melon.screen.set_layout() names by its key. Scripts see [boolean]: whether
        // the player picked one. The layout itself is only known to the app
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
    // The script's saved melon.storage blob (see LuaStorage::decodeBlob()), empty if it has none. Dropped once it's decoded
    std::string savedData;
    // Where the app keeps the script's data, sent back with each snapshot. Empty: storage isn't saved
    std::string dataGameKey;
    std::vector<LuaOptionValue> options;
};

/**
 * What the scripts can use from their folder besides themselves: modules for require(), by module name, and images for melon.image.load(), by
 * path. Read by the app at launch and never changed during the session, so every script's state shares it. An entry that was left out (over a
 * cap, or unreadable) has no content, and [leftOutReason] says why
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
        // 0xAARRGGBB, premultiplied. Empty when left out
        std::vector<melonDS::u32> pixels;
        std::string leftOutReason;
    };

    std::unordered_map<std::string, Module> modules;
    std::unordered_map<std::string, Image> images;
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
 * Saves and loads the emulator's state for melon.state, so that LuaEngine never touches Savestate. Implemented by MelonInstance. Only called on
 * the emulator thread, between frames
 */
class LuaSavestateHost
{
public:
    virtual ~LuaSavestateHost() = default;
    /**
     * Saves the whole state into [data] (resized to the state's size). False if it couldn't, in which case [data] is left as it was
     */
    virtual bool saveLuaState(std::vector<melonDS::u8>& data) = 0;
    /**
     * Loads a state [saveLuaState] made. Backs up the current state first and goes back to it if the load fails, like a player's load.
     * Returns whether the state was loaded
     */
    virtual bool loadLuaState(const std::vector<melonDS::u8>& data) = 0;
};

/**
 * Runs the game's Lua scripts (API v1, the `melon` module). Each script gets its own sandboxed Lua state, so one that fails can be switched off
 * without touching the others. A script that throws an error or runs for too long in a callback is switched off for the session, and the app
 * is told why. Scripts are told apart by their position in the list given to [setScripts] (their id).
 *
 * Callbacks, all optional: on_load(game_code) once the game starts, on_frame() just before each frame runs (the only place where input and
 * memory changes line up with the frame), on_frame_end() just after it has run (melon.frame() is still that frame, and press()/release() there
 * apply to the next one), on_state_loaded() after a savestate or rewind state is loaded, and on_unload() when the emulator stops.
 * When the console is reset, every running script is reloaded from scratch (on_unload(), then the main chunk and on_load() again).
 * melon.input.press()/release() only last for the next frame, so on_frame() calls them every frame. melon.input.set_touch() keeps its position
 * until clear_touch(), and moves the touch (the player's, or one pressed with KEY.TOUCH). held() and touch() are the player's own input.
 * require(name) loads a module from the script folder's subfolders (see LuaFolderContent), once per state. melon.image.load(path) gives an
 * image from the folder, and melon.image.new(w, h, argb) one made from a string.
 * melon.options holds the script's option values, built at each load. melon.storage keeps data between sessions (see LuaStorage). It lives
 * outside the Lua state, so it survives reloads, and is snapshotted into LuaStorage's outbox when it changed: on flush() (at the end of the
 * frame), every 600 frames, when the emulator pauses ([flushStorage]), when the script stops, and at [stop]. The app saves the snapshots.
 * melon.screen.* and melon.emu.* requests are sent to the app once the callback returns (the last value of each kind wins), and are ignored
 * during on_unload(). melon.screen.set_layout(key) names one of the script's layout options, and the app works out which layout that is.
 * melon.state.save/load/has(slot) use four savestate slots per script, kept in memory (outside the Lua state, so they survive reloads) until
 * the script is switched off or the session ends, and never the player's slots. save() runs right away through the LuaSavestateHost, at most
 * once every kStateSaveInterval frames per script. load() is queued and runs once every script's on_frame() has run: that frame's input
 * changes are dropped, and on_state_loaded() runs before the next on_frame(). melon.draw.* calls are only recorded during on_frame() and on_frame_end() (they do nothing anywhere else,
 * on_state_loaded() included), and drawn into [getOverlay] once every script's on_frame_end() has run, in script order (each script's on_frame()
 * drawings, then its on_frame_end() ones). Each frame shows only what was drawn for it, and a script that stops during the frame shows nothing.
 *
 * Everything runs on the emulator thread, except [setScripts] (before the emulator thread starts), [notifyStateLoaded], [notifyReset],
 * [disableScript], [enableScript], [restartScript] and [updateSticks] (any thread).
 */
class LuaEngine
{
public:
    struct Script;
    struct DrawCommand;

    LuaEngine();
    ~LuaEngine();

    /**
     * [areStateLoadsAllowed] is false when savestates can't be loaded in this session (RetroAchievements hardcore), so melon.state.load()
     * refuses too. Scripts don't run in hardcore anyway, so this is only a second check
     */
    void setScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed);
    /**
     * Loads the scripts given to [setScripts] and calls their on_load(game). Called once the game has started. [savestateHost] must outlive
     * the scripts (until [stop])
     */
    void start(melonDS::NDS* nds, std::shared_ptr<MelonEventMessenger> eventMessenger, LuaSavestateHost* savestateHost);
    /**
     * Whether any script is still running, or the player switched one on or off since the last frame (which [runFrame] applies). While
     * neither is true, the emulator doesn't need to call [runFrame]
     */
    bool isActive() const { return activeScriptCount > 0 || isScriptSwitchPending.load(std::memory_order_relaxed); }
    /**
     * Reloads the scripts if the console was reset, calls on_state_loaded() if a state was loaded since the last frame, then on_frame(), and
     * returns how the scripts want to change the input of the frame that is about to run. Called just before the frame runs
     */
    LuaInputOverride runFrame(int frame, const LuaUserInput& userInput);
    /**
     * Calls on_frame_end(), then draws the frame's drawings into [getOverlay]. Called once the frame has run, after every [runFrame] (also when
     * no script is left running, so that the last drawings go away)
     */
    void endFrame();
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
     * Switches a script off because the player asked to: on_unload() runs, and the app isn't told (it already knows). The app runs on another
     * thread, so it's only queued and applied before the next frame
     */
    void disableScript(int id);
    /**
     * Switches a stopped script back on because the player asked to, the way a reset reloads it: a fresh state, its main chunk, then
     * on_load(game). If that fails, it's switched off again and the app is told, like any error. Queued like [disableScript], in the same queue
     */
    void enableScript(int id);
    /**
     * Restarts a script with new option values: on_unload() if it runs, then a fresh state, its main chunk and on_load(game), like
     * [enableScript]. Queued like [disableScript], in the same queue. Also starts a stopped script
     */
    void restartScript(int id, std::vector<LuaOptionValue> options);
    /**
     * Snapshots the storage of every script whose storage changed since its last snapshot. Called on the emulator thread when it parks for
     * a pause
     */
    void flushStorage();
    /**
     * Controller sticks from the app, from -1 to 1 (down and right are positive). Only sent while a script runs
     */
    void updateSticks(float leftX, float leftY, float rightX, float rightY);
    /**
     * What the scripts drew for the frame [endFrame] was last called for. Empty while no script runs
     */
    const LuaOverlay& getOverlay() const { return overlay; }

private:
    // From disableScript(), enableScript() and restartScript(), applied in the order they were asked for. The flag saves locking the mutex every frame
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

    bool loadScript(Script& script);
    void reloadScripts();
    void queueScriptSwitch(int id, ScriptSwitch::Action action, std::vector<LuaOptionValue> options = {});
    void applyPendingScriptSwitches();
    bool callCallback(Script& script, const char* name, const char* argument, int instructionLimit);
    bool runProtected(Script& script, lua_CFunction function, void* data, int instructionLimit);
    void sendRequests(Script& script);
    void switchOff(Script& script, const std::string& error);
    void closeScript(Script& script);
    void saveStorage(Script& script);
    void freeStateSlots(Script& script);
    void runPendingStateLoad();
    void rasterizeOverlay();

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
    // melon.screen.swap(), melon.screen.show_controls() and melon.emu.fast_forward(): a boolean request to the app, in [request]
    template <std::optional<bool> Script::* request> static int luaSetBoolRequest(lua_State* L);
    static int luaScreenSetLayout(lua_State* L);
    static int luaStateSave(lua_State* L);
    static int luaStateLoad(lua_State* L);
    static int luaStateHas(lua_State* L);
    static DrawCommand* addDrawCommand(lua_State* L, int64_t pixelCount, size_t textLength = 0);
    static int luaDrawPixel(lua_State* L);
    static int luaDrawLine(lua_State* L);
    static int luaDrawRect(lua_State* L);
    static int luaDrawText(lua_State* L);
    static int luaDrawCircle(lua_State* L);
    static int luaDrawImage(lua_State* L);
    static int luaRequire(lua_State* L);
    static int luaImageLoad(lua_State* L);
    static int luaImageNew(lua_State* L);
    static int luaImageSize(lua_State* L);
    static int luaStorageGet(lua_State* L);
    static int luaStorageSet(lua_State* L);
    static int luaStorageKeys(lua_State* L);
    static int luaStorageClear(lua_State* L);
    static int luaStorageFlush(lua_State* L);
    static int luaStorageSize(lua_State* L);

    std::vector<LuaScriptSource> pendingScripts;
    // Outlives every script's state, which may point into its images
    std::shared_ptr<const LuaFolderContent> folderContent;
    std::vector<std::unique_ptr<Script>> scripts;
    int activeScriptCount = 0;
    // Set while on_unload() runs: requests to the app are ignored, and errors are only logged
    bool isUnloading = false;
    // Set while on_frame() or on_frame_end() runs, the only callbacks whose draw calls are recorded
    bool isDrawingAllowed = false;
    LuaOverlay overlay;

    melonDS::NDS* nds = nullptr;
    std::shared_ptr<MelonEventMessenger> eventMessenger;
    LuaSavestateHost* savestateHost = nullptr;
    bool areStateLoadsAllowed = true;
    // Bytes held by every script's state slots together, capped at kMaxStateSlotMemory
    size_t stateSlotMemory = 0;
    // The load melon.state.load() queued, run once every script's on_frame() has run (the last one asked for wins). -1 when there's none
    int pendingStateLoadScript = -1;
    int pendingStateLoadSlot = 0;
    // A script's load ran, so on_state_loaded() is due before the next on_frame()
    bool isScriptStateLoaded = false;
    // Counts runFrame() calls, for the save cooldown. melon.frame() can't be used, since it can stand still or go back (rewind)
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
};

}

#endif //LUAENGINE_H
