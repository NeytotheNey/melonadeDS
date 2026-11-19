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
#ifdef MELONADE_DEBUG_TOOLS
    /**
     * GPU::GLSeams, the OpenGL floor seams fix, for the running game from any thread, applied before the next frame (see
     * MelonInstance::setGLSeams); 0 while no game runs
     */
    extern void setGLSeams(int value);
    extern int getGLSeams();
#endif
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
     * Debug builds only: the capture readback stats summed since the last call (compute renderer only): readback maps, ns waited in the
     * map, ns spent polling the capspin fence first, and the ns from each RenderFrame start to its map's return
     */
    struct CaptureStats {
        int64_t maps = 0;
        int64_t waitNs = 0;
        int64_t spinNs = 0;
        int64_t spanNs = 0;
    };
    /**
     * Fills in and zeroes the capture readback stats; emulator thread only, after loop()
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
     * GPU::Comp1x: whether every surface presenting frames can draw a screen the compositor drew at 1x (Frame::halvesAt1x), as the render thread
     * last found after presenting; read by the emulator thread before each frame (GPU::Comp1xFrontend). Off until a presentation says so
     */
    extern void setHalvesAt1xSupported(bool supported);
    extern bool areHalvesAt1xSupported();
    /**
     * GPU::OutputCap: how many times the native size the screens are shown at, at most, over every surface presenting frames (0 for unknown),
     * as the render thread last worked it out (FrameRenderCoordinator); read by the emulator thread before each frame (MelonInstance::runFrame)
     */
    extern void setOutputScaleNeed(int need);
    extern int getOutputScaleNeed();
    /**
     * GPU::OutputCapDeband: whether any surface presenting frames debands them (its renderer configuration's deband), as the render thread
     * last saw it (FrameRenderCoordinator); read by the emulator thread before each frame along with the need (MelonInstance::runFrame)
     */
    extern void setOutputDeband(bool deband);
    extern bool getOutputDeband();
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
