#ifndef MELONDS_MELONDS_H
#define MELONDS_MELONDS_H

#include <list>
#include <mutex>
#include <vector>
#include "AndroidFileHandler.h"
#include "AndroidCameraHandler.h"
#include "Configuration.h"
#include "FrontendContext.h"
#include "MelonEventMessenger.h"
#include "MelonTypes.h"
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
    typedef enum {
        ROM,
        FIRMWARE
    } RunMode;

    extern OpenGLContext *openGlContext;
    extern AndroidFileHandler* fileHandler;
    extern AndroidCameraHandler* cameraHandler;
    /**
     * Held while the instance is replaced or destroyed and by any other thread that uses it (the render thread presenting a frame, savestate and
     * RetroAchievements calls from coroutines), so it can't be destroyed under them; recursive because the frame render callback can call back into
     * the native code (isSleeping()) while presentFrame holds it.
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
     * A new width and margin for the top screen's 3D view during play (see MelonInstance::setWideView)
     */
    extern void setWideView(int width, int margin);
    /**
     * Gives the instance the game's Lua scripts and the modules and images they can use, after setup() and before the emulator starts;
     * [areStateLoadsAllowed] is false when savestates can't be loaded (RetroAchievements hardcore), which melon.state.load() checks too
     */
    extern void setLuaScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed);
    /**
     * Switches a Lua script off until the player switches it back on, from any thread
     */
    extern void disableLuaScript(int scriptId);
    /**
     * Switches a stopped Lua script back on, from any thread
     */
    extern void enableLuaScript(int scriptId);
    /**
     * Restarts a Lua script with new option values (starting it if it was off), from any thread
     */
    extern void restartLuaScript(int scriptId, std::vector<LuaOptionValue> options);
    /**
     * Snapshots the Lua scripts' changed storage for the app to save; emulator thread only, between frames
     */
    extern void flushLuaStorage();
    /**
     * Copies the last rewind capture's screenshot, read back from the GPU after the capture, into its state; emulator thread only, between frames
     */
    extern void finishRewindScreenshot();
    extern void updateLuaSticks(float leftX, float leftY, float rightX, float rightY);
    // The WideMelon camera's window for melon.wide.camera() (LuaEngine::updateWideCamera)
    extern void updateWideCamera(float x, float zoom, float y);
    extern void start();
    /**
     * Runs one frame, to be presented on the vsync at [presentDueTimeNs] (CLOCK_MONOTONIC, ns), or as soon as possible if that's 0
     */
    extern u32 loop(int64_t presentDueTimeNs);
    /**
     * Logs the CPU heartbeat and exception checks for the debug log; emulator thread only, after loop(), while the debug log is on
     */
    extern void logDebugFrameState(bool isFastForward, float fps, int64_t lastFrameWorkNs);
    /**
     * The kernel thread ID of the core's 2D render thread, or 0 while threaded 2D is off; emulator thread only, after loop()
     */
    extern pid_t getGpu2DThreadId();
#ifdef MELONADE_DEBUG_TOOLS
    /**
     * Fills in and zeroes the capture readback stats (MelonTypes.h); emulator thread only, after loop()
     */
    extern void takeCaptureStats(CaptureStats& stats);
#endif
    /**
     * Frames waiting in the SPU output ring, for the audio diagnostics; emulator thread only, between frames
     */
    extern int getAudioOutputBufferFill();
    extern Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs);
    extern bool isSleeping();
    /**
     * The known Pokemon game's scene for the WideMelon camera (GameState::Scene), -1 for other games or no game; asking keeps it updated
     * (GameState::pollScene); any thread
     */
    extern int getGameScene();
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
