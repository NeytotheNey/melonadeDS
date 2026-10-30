#ifndef MELONDS_MELONDS_H
#define MELONDS_MELONDS_H

#include <list>
#include <mutex>
#include <vector>
#include "AndroidFileHandler.h"
#include "AndroidCameraHandler.h"
#include "Configuration.h"
#include "MelonEventMessenger.h"
#include "RewindManager.h"
#include "RomGbaSlotConfig.h"
#include "retroachievements/RAAchievement.h"
#include "retroachievements/RALeaderboard.h"
#include "renderer/FrameQueue.h"
#include "scripting/LuaEngine.h"
#include "types.h"
#include "../GPU.h"
#include <android/asset_manager.h>

using namespace melonDS;

namespace MelonDSAndroid {
    typedef struct {
        std::vector<u32> code;
    } Cheat;

    typedef enum {
        ROM,
        FIRMWARE
    } RunMode;

    extern OpenGLContext *openGlContext;
    extern AndroidFileHandler* fileHandler;
    extern AndroidCameraHandler* cameraHandler;
    extern std::string internalFilesDir;
    extern std::shared_ptr<MelonEventMessenger> eventMessenger;
    /**
     * Held while the instance is replaced or destroyed, and by any thread other than the emulator thread that uses it (the render thread
     * presenting a frame, savestates and RetroAchievements calls from coroutines), so the instance can't be destroyed under them. Recursive
     * because the frame render callback can call back into the native code (isSleeping()) while presentFrame holds it.
     */
    extern std::recursive_mutex presentationMutex;

    extern void setConfiguration(EmulatorConfiguration emulatorConfiguration);
    extern void setup(AndroidCameraHandler* androidCameraHandler, std::shared_ptr<MelonEventMessenger> androidEventMessenger, u32* screenshotBufferPointer, int instanceId);
    extern void setCodeList(std::list<Cheat> cheats);
    extern void setupAchievements(std::list<RetroAchievements::RAAchievement> achievements, std::list<RetroAchievements::RALeaderboard> leaderboards, std::optional<std::string> richPresenceScript);
    extern void unloadRetroAchievementsData();
    extern std::string getRichPresenceStatus();
    extern std::vector<RetroAchievements::RARuntimeAchievement> getRuntimeAchievements();
    extern void updateEmulatorConfiguration(std::unique_ptr<EmulatorConfiguration> emulatorConfiguration);

    /**
     * Loads the NDS ROM and, optionally, the GBA ROM.
     *
     * @param romPath The path to the NDS rom
     * @param sramPath The path to the rom's SRAM file
     * @param gbaSlotConfig The config to be used for the GBA slot
     * @return The load result. 0 if everything was loaded successfully, 1 if the NDS ROM was loaded but the GBA ROM
     * failed to load, 2 if the NDS ROM failed to load
     */
    extern int loadRom(std::string romPath, std::string sramPath, RomGbaSlotConfig* gbaSlotConfig);
    extern int bootFirmware();
    extern void touchScreen(u16 x, u16 y);
    extern void releaseScreen();
    extern void pressKey(u32 key);
    extern void releaseKey(u32 key);
    extern void updateMotionData(float ax, float ay, float az, float rx, float ry, float rz);
    /**
     * A new width for the top screen's 3D view during play (see MelonInstance::setWideViewWidth)
     */
    extern void setWideViewWidth(int width);
    /**
     * Gives the instance the game's Lua scripts, and the modules and images they can use. Must be called after setup() and before the emulator starts.
     * [areStateLoadsAllowed] is false when savestates can't be loaded (RetroAchievements hardcore), which melon.state.load() checks too
     */
    extern void setLuaScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed);
    /**
     * Switches a Lua script off until the player switches it back on. Any thread
     */
    extern void disableLuaScript(int scriptId);
    /**
     * Switches a stopped Lua script back on. Any thread
     */
    extern void enableLuaScript(int scriptId);
    /**
     * Restarts a Lua script with new option values (it's started if it was off). Any thread
     */
    extern void restartLuaScript(int scriptId, std::vector<LuaOptionValue> options);
    /**
     * Snapshots the Lua scripts' changed storage, for the app to save. Only on the emulator thread, between frames
     */
    extern void flushLuaStorage();
    extern void updateLuaSticks(float leftX, float leftY, float rightX, float rightY);
    extern void start();
    /**
     * Runs one frame. [presentDueTimeNs] is the vsync (CLOCK_MONOTONIC, ns) the frame is meant to be presented on, or 0 to present it as soon as
     * possible
     */
    extern u32 loop(int64_t presentDueTimeNs);
    /**
     * Logs the CPU heartbeat and exception checks for the debug log. Must only be called by the emulator thread, after loop(), while the debug
     * log is on
     */
    extern void logDebugFrameState(bool isFastForward, float fps, int64_t lastFrameWorkNs);
    /**
     * The kernel thread ID of the core's 2D render thread, or 0 while threaded 2D is off. Must only be called by the emulator thread, after
     * loop()
     */
    extern pid_t getGpu2DThreadId();
    /**
     * Frames waiting in the SPU output ring, for the audio diagnostics. Must only be called by the emulator thread, between frames
     */
    extern int getAudioOutputBufferFill();
    extern Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs);
    extern bool isSleeping();
    extern void pause();
    extern void resume();
    extern void syncDateTimeToDevice();
    extern void reset();
    extern bool saveState(const char* path);
    extern bool loadState(const char* path);
    extern bool loadRewindState(melonDS::RewindSaveState rewindSaveState);
    extern RewindWindow getRewindWindow();
    extern bool takeScreenshot();
    extern void stop();
    extern void cleanup();
}

#endif //MELONDS_MELONDS_H
