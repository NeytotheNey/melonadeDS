#ifndef MELONINSTANCE_H
#define MELONINSTANCE_H

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include "Args.h"
#include "Configuration.h"
#include "NDS.h"
#include "MelonDS.h"
#include "SaveManager.h"
#include "RewindManager.h"
#include "renderer/FrameQueue.h"
#include "renderer/Renderer.h"
#include "renderer/ScreenshotRenderer.h"
#include "retroachievements/RetroAchievementsManager.h"
#include "net/Net.h"
#include "debug/DebugLog.h"
#include "debug/DebugProperties.h"
#include "scripting/LuaEngine.h"

using namespace melonDS;

namespace MelonDSAndroid
{

class MelonInstance : public LuaSavestateHost
{

public:
    MelonInstance(int instanceId, std::shared_ptr<EmulatorConfiguration> configuration, std::unique_ptr<melonDS::NDSArgs> args, std::shared_ptr<Net> net, std::unique_ptr<ScreenshotRenderer> screenshotRenderer, int consoleType);
    ~MelonInstance();

    int getInstanceId() { return instanceId; };

    bool loadRom(std::string romPath, std::string sramPath);
    bool loadGbaRom(std::string romPath, std::string sramPath);
    void loadRumblePak();
    void loadGbaMemoryExpansion();
    void loadMotionPakHomebrew();
    void loadMotionPakRetail();
    bool bootFirmware();
    void start();
    void reset();
    // [presentDueTimeNs] is the vsync the frame is meant to be presented on, or 0 to present it as soon as possible
    melonDS::u32 runFrame(int64_t presentDueTimeNs);
    void stop();
    // Moves the RTC forward to the device's time if it has fallen behind. Must only be called while the emulator thread is parked
    void syncDateTimeToDevice();

    void updateMotionData(float ax, float ay, float az, float rx, float ry, float rz);
    /**
     * A new width for the top screen's 3D view (even, 256..768, anything else means 256), applied before the next frame on the OpenGL
     * and compute renderers. The core starts drawing at it once the polygons being drawn were built for it. Safe to call from any thread
     */
    void setWideViewWidth(int width) { requestedWideViewWidth.store(width, std::memory_order_relaxed); }
    float getMotionData(MotionQueryType type);
    void touchScreen(u16 x, u16 y);
    void releaseScreen();
    void pressKey(u32 key);
    void releaseKey(u32 key);
    int readAudioOutput(s16* buffer, int length);
    void setAudioOutputSkew(double skew);
    /**
     * Drops the oldest output samples if the SPU output ring holds more than [thresholdFrames], keeping the newest [keepFrames]. Returns the
     * number of frames dropped. Holds the SPU's audio lock once for a constant-time update, like readAudioOutput(), so the output callback may
     * call it too.
     */
    int trimAudioOutput(int thresholdFrames, int keepFrames);
    // Frames waiting in the SPU output ring, for the audio diagnostics. Takes the SPU's audio lock
    int getAudioOutputBufferFill();
    bool takeScreenshot();
    void loadCheats(std::list<Cheat> cheats);
    int sendNetPacket(u8* data, int length);
    int receiveNetPacket(u8* data);

    Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs);
    void abortPresentationWait() { frameQueue.abortPresentWait(); }
    // Whether the emulated console was asleep (lid closed) during the last frame, in which case no frames are presented
    bool isSleeping() { return sleeping.load(std::memory_order_relaxed); }
    // Logs the CPU heartbeat and exception checks for the debug log. Must be called on the emulator thread, between frames
    void logDebugFrameState(bool isFastForward, float fps, int64_t lastFrameWorkNs) { DebugLog::onFrame(*nds, isFastForward, fps, lastFrameWorkNs); }
    // The kernel thread ID of the core's 2D render thread, or 0 while threaded 2D is off (or the thread hasn't started yet). Emulator thread only
    pid_t getGpu2DThreadId();
#ifdef MELONADE_DEBUG_TOOLS
    // The capture readback stats the 3D renderer summed since the last call (compute renderer only), then zeroes them. Emulator thread only
    void takeCaptureStats(CaptureStats& stats);
#endif

    void updateConfiguration(std::shared_ptr<EmulatorConfiguration> newConfiguration);
    void requestNdsSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength);
    void requestGbaSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength);
    void requestFirmwareSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength);
    bool saveState(Savestate* state);
    bool loadState(Savestate* state);
    RewindWindow getRewindWindow();
    bool loadRewindState(RewindSaveState rewindSaveState);
    void setupAchievements(
        std::list<RetroAchievements::RAAchievement> achievements,
        std::list<RetroAchievements::RALeaderboard> leaderboards,
        std::optional<std::string> richPresenceScript
    );
    void unloadRetroAchievementsData();
    std::string getRichPresenceStatus();
    std::vector<RetroAchievements::RARuntimeAchievement> getRuntimeAchievements();
    // Must be called before the emulator thread starts. The scripts are loaded when the game starts
    void setLuaScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed) { luaEngine.setScripts(std::move(scripts), std::move(folderContent), areStateLoadsAllowed); }
    void disableLuaScript(int scriptId) { luaEngine.disableScript(scriptId); }
    void enableLuaScript(int scriptId) { luaEngine.enableScript(scriptId); }
    void restartLuaScript(int scriptId, std::vector<LuaOptionValue> options) { luaEngine.restartScript(scriptId, std::move(options)); }
    // Emulator thread, between frames
    void flushLuaStorage() { luaEngine.flushStorage(); }
    /**
     * Emulator thread, between frames. Copies the last rewind capture's screenshot into its state, waiting for the GPU if [wait]. Done before
     * the thread parks, so the rewind window is complete while it's paused
     */
    void finishRewindScreenshot(bool wait);
    void updateLuaSticks(float leftX, float leftY, float rightX, float rightY) { luaEngine.updateSticks(leftX, leftY, rightX, rightY); }

    // LuaSavestateHost, for melon.state. Emulator thread, between frames
    bool saveLuaState(std::vector<melonDS::u8>& data) override;
    bool loadLuaState(const std::vector<melonDS::u8>& data) override;

private:
    void updateRenderer();
    void setBatteryLevels();
    void setDateTime();
    void saveRewindState(RewindSaveState* rewindSaveState);
    // Emulator thread, before a frame. Hands the rewind settings from updateConfiguration() to the rewind manager
    void applyRewindSettings();
    // loadState() without telling the scripts, and without a backup
    bool restoreState(Savestate* state);
    // restoreState(), going back to the state from before if the load fails. Doesn't tell the scripts
    bool loadStateWithBackup(Savestate* state);
    // Whether LuaEngine::runFrame() ran, which LuaEngine::endFrame() must then follow
    bool runLuaFrame();

private:
    int instanceId;
    int consoleType;
    NDS* nds;
    std::shared_ptr<Net> net;

    std::atomic_bool sleeping = false;
    std::atomic<float> motionData[6] = { 0.0f, 0.0f, 9.80665f, 0.0f, 0.0f, 0.0f };

    std::unique_ptr<RetroAchievements::RetroAchievementsManager> retroAchievementsManager;
    std::unique_ptr<SaveManager> ndsSave;
    std::unique_ptr<SaveManager> gbaSave;
    std::unique_ptr<SaveManager> firmwareSave;
    // Written by the UI thread, and read by the emulator thread while a Lua script changes the input
    std::atomic<u32> inputMask;
    // The player's touch: kUserTouchDown | x << 16 | y, or 0 when not touching
    std::atomic<u32> userTouch = 0;
    // While set, the emulator thread applies the input before each frame (the player's, as changed by the scripts) instead of the UI thread
    std::atomic_bool isLuaInputActive = false;
    LuaEngine luaEngine;
    // EmulatorConfiguration::luaDrawingsInScreenshots, for the emulator thread. The configuration can be replaced on another thread
    std::atomic_bool luaDrawingsInScreenshots = false;
    // The width of the top screen's 3D view, for the emulator thread. Starts at EmulatorConfiguration::wideViewWidth from the launch and then
    // follows setWideViewWidth() (configuration updates don't change it). Applied to every OpenGL renderer that updateRenderer() sets up,
    // since a new one starts at 256
    int wideViewWidth;
    // The width setWideViewWidth() asked for, from any thread. Picked up by the emulator thread before the next frame
    std::atomic_int requestedWideViewWidth;

    std::shared_ptr<EmulatorConfiguration> currentConfiguration;
    FrameQueue frameQueue;
    std::unique_ptr<ScreenshotRenderer> screenshotRenderer;
    RewindManager rewindManager;
    Renderer currentRenderer;
    // The requested renderer whose New() failed, so the software fallback is in use. Only tried again once a different renderer is asked for
    std::optional<Renderer> failedRenderer;
    bool isRenderConfigurationDirty;
#ifdef MELONADE_DEBUG_TOOLS
    // The debug.melonade.* switches that override settings (see DebugProperties.h)
    DebugProperties::Overrides debugOverrides;
#endif
    int frame;
    // The frame of the rewind state whose screenshot is still being read back, or -1
    int pendingRewindScreenshotFrame = -1;
    // Rewind settings from updateConfiguration(), which runs on other threads. The emulator thread applies them before its next frame, since
    // a change can free the states it's using
    struct RewindSettings
    {
        bool enabled;
        int lengthSeconds;
        int captureSpacingSeconds;
    };
    std::mutex rewindSettingsMutex;
    std::optional<RewindSettings> pendingRewindSettings;
    std::atomic_bool hasPendingRewindSettings = false;
    // CRC32 of the loaded ROM file. Only worked out while the debug log is on
    bool hasRomCrc32 = false;
    melonDS::u32 romCrc32 = 0;
};

}

#endif
