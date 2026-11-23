#ifndef MELONINSTANCE_H
#define MELONINSTANCE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include "Args.h"
#include "Configuration.h"
#include "NDS.h"
#include "WideMelon.h"
#include "MelonTypes.h"
#include "SaveManager.h"
#include "RewindManager.h"
#include "renderer/FrameQueue.h"
#include "renderer/Renderer.h"
#include "renderer/ScreenshotRenderer.h"
#include "retroachievements/RetroAchievementsManager.h"
#include "net/Net.h"
#include "debug/DebugProperties.h"
#include "scripting/LuaEngine.h"
#include "gamestate/GameState.h"

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
    // Moves the RTC forward to the device's time if it has fallen behind; must only be called while the emulator thread is parked
    void syncDateTimeToDevice();

    void updateMotionData(float ax, float ay, float az, float rx, float ry, float rz);
    /**
     * A new width for the top screen's 3D view (even, 256..768, anything else means 256) and its margin (the WideMelon camera's rows above and
     * below the top screen, 0..128), applied together before the next frame on the OpenGL and compute renderers; safe to call from any thread
     */
    void setWideView(int width, int margin) { requestedWideView.store(packWideView(width, margin), std::memory_order_relaxed); }
    // Whether the frontend wants each frame's depth (Frame::depthTexture) whatever the scripts ask for (the desktop's --dof); any thread
    void setDepthWanted(bool wanted) { isFrontendDepthWanted.store(wanted, std::memory_order_relaxed); }
    // The characters' sun shadows the OpenGL renderer draws while no script asks for its own (the desktop's --sun); emulator thread, between
    // frames
    void setFrontendSun(const melonDS::SunShadows& sun) { frontendSun = sun; }
    // The guard band that goes with it (melon.screen.sun's guard, the desktop's --sun-guard), in DS pixels; emulator thread, between frames
    void setFrontendSunGuard(int guard) { frontendSunGuard = std::clamp(guard, 0, WideMelon::MaxMargin); }
    // The texture hashes the OpenGL renderer leaves out while no script hides any (the desktop's --hide-textures); emulator thread, between
    // frames
    void setFrontendHiddenTextures(std::vector<u32> hashes) { frontendHiddenTextures = std::move(hashes); }
    // The texture hashes it flags as lights while no script lights any (the desktop's --light-textures); emulator thread, between frames
    void setFrontendLightTextures(std::vector<u32> hashes) { frontendLightTextures = std::move(hashes); }
    // The texture hashes whose polygons the depth hands over as 2D while no script marks any (the desktop's --ui-textures); emulator
    // thread, between frames
    void setFrontendUiTextures(std::vector<u32> hashes) { frontendUiTextures = std::move(hashes); }
    // The texture hashes the depth of field focuses on while no script lists any (the desktop's --focus-textures); emulator thread, between
    // frames
    void setFrontendFocusTextures(std::vector<u32> hashes) { frontendFocusTextures = std::move(hashes); }
    float getMotionData(MotionQueryType type);
    void touchScreen(u16 x, u16 y);
    void releaseScreen();
    void pressKey(u32 key);
    void releaseKey(u32 key);
    int readAudioOutput(s16* buffer, int length);
    void setAudioOutputSkew(double skew);
    /**
     * Drops the oldest output samples if the SPU output ring holds more than [thresholdFrames], keeping the newest [keepFrames], and returns how many
     * it dropped; it holds the SPU's audio lock once for a constant-time update, so the output callback may call it too.
     */
    int trimAudioOutput(int thresholdFrames, int keepFrames);
    // Frames waiting in the SPU output ring, for the audio diagnostics (takes the SPU's audio lock)
    int getAudioOutputBufferFill();
    bool takeScreenshot();
    void loadCheats(std::list<Cheat> cheats);
    int sendNetPacket(u8* data, int length);
    int receiveNetPacket(u8* data);

    Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs);
    void abortPresentationWait() { frameQueue.abortPresentWait(); }
    // Whether the emulated console was asleep (lid closed) during the last frame, in which case no frames are presented
    bool isSleeping() { return sleeping.load(std::memory_order_relaxed); }
    // Logs the CPU heartbeat and exception checks for the debug log; emulator thread, between frames
    void logDebugFrameState(bool isFastForward, float fps, int64_t lastFrameWorkNs);
    // The kernel thread ID of the core's 2D render thread, or 0 while threaded 2D is off or the thread hasn't started; emulator thread only
    pid_t getGpu2DThreadId();
#ifdef MELONADE_DEBUG_TOOLS
    // Takes the capture readback stats the 3D renderer summed since the last call (compute renderer only) and zeroes them; emulator thread only
    void takeCaptureStats(CaptureStats& stats);
#endif

    void updateConfiguration(std::shared_ptr<EmulatorConfiguration> newConfiguration);
    void requestNdsSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength);
    void requestGbaSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength);
    void syncNdsSaveFromState(const u8* saveData, u32 saveLength);
    void syncGbaSaveFromState(const u8* saveData, u32 saveLength);
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
    // Must be called before the emulator thread starts (the scripts load when the game starts)
    void setLuaScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed) { luaEngine.setScripts(std::move(scripts), std::move(folderContent), areStateLoadsAllowed); }
    void disableLuaScript(int scriptId) { luaEngine.disableScript(scriptId); }
    void enableLuaScript(int scriptId) { luaEngine.enableScript(scriptId); }
    void restartLuaScript(int scriptId, std::vector<LuaOptionValue> options) { luaEngine.restartScript(scriptId, std::move(options)); }
    // Emulator thread, between frames
    void flushLuaStorage() { luaEngine.flushStorage(); }
    /**
     * Emulator thread, between frames: copies the last rewind capture's screenshot into its state, waiting for the GPU if [wait], before the thread
     * parks so the rewind window is complete while it's paused
     */
    void finishRewindScreenshot(bool wait);
    void updateLuaSticks(float leftX, float leftY, float rightX, float rightY) { luaEngine.updateSticks(leftX, leftY, rightX, rightY); }
    void updateWideCamera(float x, float zoom, float y) { luaEngine.updateWideCamera(x, zoom, y); }
    // The known Pokemon game's scene for the WideMelon camera (GameState::pollScene), GameState::Unknown for other games; any thread
    int pollGameScene() { return gameState.pollScene(); }

#ifndef __ANDROID__
    // The desktop's switches over the configuration (its command line), before the first frame; debug builds on Android read them in the constructor
    void setDebugOverrides(const DebugProperties::Overrides& overrides) { debugOverrides = overrides; isRenderConfigurationDirty = true; }
    // The console and the renderer in use, for the desktop's scripted runs and title; emulator thread, between frames
    melonDS::NDS* getNds() { return nds; }
    Renderer getCurrentRenderer() const { return currentRenderer; }
#endif

    // LuaSavestateHost, for melon.state; emulator thread, between frames
    bool saveLuaState(std::vector<melonDS::u8>& data) override;
    bool loadLuaState(const std::vector<melonDS::u8>& data) override;

private:
    void updateRenderer();
    // Before each frame: the GPU 2D shadow's mode from what updateRenderer() worked out, the top screen's width and whether the last frame was
    // a Gen 4 battle (a change only), then that battle's wide modes (GameState::applyWideBattle); emulator thread
    void applyShadow2DMode();
    void setBatteryLevels();
    void setDateTime();
    void saveRewindState(RewindSaveState* rewindSaveState);
    // Emulator thread, before a frame: hands the rewind settings from updateConfiguration() to the rewind manager
    void applyRewindSettings();
    // loadState() without telling the scripts, and without a backup
    bool restoreState(Savestate* state);
    // restoreState(), going back to the state from before if the load fails, without telling the scripts
    bool loadStateWithBackup(Savestate* state);
    // Whether LuaEngine::runFrame() ran, which LuaEngine::endFrame() must then follow
    bool runLuaFrame();
    // Before each frame, before updateRenderer(): the sun's guard band from the newest sun request (sunGuard), then the render view from it
    // and the shown view, told to the GL or Compute renderer on a change; emulator thread
    void updateSunGuard();
    void updateRenderView();
    // After the OpenGL renderer was given a view or render settings: notes the view it resolved them to with the guard it was asked for, for
    // getFrameGuard()
    void noteRenderView(int guard);
    // The guard band included in a frame the OpenGL renderer draws [width] wide with [margin] rows (its output, which lags the view), the one
    // the newest noted view with that size was asked for, kept to what that size has room for
    int getFrameGuard(int width, int margin) const;

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
    // The player's touch, packed by PackedTouch (MelonInstance.cpp), or 0 when not touching
    std::atomic<u32> userTouch = 0;
    // While set, the emulator thread applies the input before each frame (the player's, as changed by the scripts) instead of the UI thread
    std::atomic_bool isLuaInputActive = false;
    // Before luaEngine, which uses it until the scripts stop (melon.game.state())
    GameState gameState;
    LuaEngine luaEngine;
    // EmulatorConfiguration::luaDrawingsInScreenshots for the emulator thread, since the configuration can be replaced on another thread
    std::atomic_bool luaDrawingsInScreenshots = false;
    // setDepthWanted(), read by the emulator thread before each frame
    std::atomic_bool isFrontendDepthWanted = false;
    // setFrontendSun(), emulator thread only
    melonDS::SunShadows frontendSun {};
    // setFrontendSunGuard(), emulator thread only
    int frontendSunGuard = 0;
    // The sun's guard band in DS pixels the render view is widened by (updateSunGuard), emulator thread only: a higher request is taken at
    // once, a lower one once it held for SUN_GUARD_HOLD_FRAMES frames (pendingSunGuard, pendingSunGuardFrames), so a sun fading out during
    // a text box or a script reload doesn't resize the render. Always 0 off OpenGL
    int sunGuard = 0;
    int pendingSunGuard = 0;
    int pendingSunGuardFrames = 0;
    // The views the OpenGL renderer resolved, newest at renderViewNotes[(renderViewNoteNext - 1) % size], with the guard each was asked for
    // (noteRenderView), so a frame drawn at an older one crops by its guard; emulator thread only, cleared with a new renderer
    struct RenderViewNote
    {
        int width;
        int margin;
        int guard;
    };
    std::array<RenderViewNote, 16> renderViewNotes {};
    int renderViewNoteCount = 0;
    int renderViewNoteNext = 0;
    // setFrontendHiddenTextures(), emulator thread only
    std::vector<u32> frontendHiddenTextures;
    // setFrontendLightTextures(), emulator thread only
    std::vector<u32> frontendLightTextures;
    // setFrontendUiTextures(), emulator thread only
    std::vector<u32> frontendUiTextures;
    // setFrontendFocusTextures(), emulator thread only
    std::vector<u32> frontendFocusTextures;
    // The depth of field's focus on the focus textures (Frame::depthFocus) as it eases from scene to scene (updateDepthFocus), emulator
    // thread only: the view distance shown, negative for none, whether it's W-buffered, the scene it last moved for (DepthFocus::Scene), and
    // the frames in a row without one
    float focusDistance = -1.0f;
    bool isFocusWBuffer = false;
    u32 focusScene = 0;
    int focusMissingFrames = 0;
    // A new layout's focus (the renderer's DepthFocus) eased into the state above; returns Frame::depthFocus
    float updateDepthFocus(const melonDS::DepthCamera& camera);
    // The width and margin of the top screen's 3D view as shown, for the emulator thread: they start at EmulatorConfiguration::wideViewWidth
    // and wideViewMargin and then follow setWideView() (configuration updates don't change them)
    int wideViewWidth;
    int wideViewMargin;
    // The view the GL or Compute renderer is told to draw (updateRenderView): the shown one, widened by the sun's guard band on OpenGL
    int renderViewWidth;
    int renderViewMargin;
    // The guard band in it, sunGuard as far as it fits
    int renderViewGuard = 0;
    // The width and margin setWideView() asked for (packWideView), from any thread, picked up by the emulator thread before the next frame; one
    // value so a frame never gets the new width with the old margin
    std::atomic<u32> requestedWideView;
    static u32 packWideView(int width, int margin) { return ((u32) width & 0xFFFF) | (((u32) margin & 0xFFFF) << 16); }
    // GPU::OutputCap, emulator thread only: the frontend's need (MelonDSAndroid::getOutputScaleNeed) the renderer is given, and the last one
    // seen with the frames it has held for, as a new one only replaces it once it held for OUTPUT_NEED_STABLE_FRAMES frames. The same for
    // whether the frontend debands (MelonDSAndroid::getOutputDeband, GPU::OutputCapDeband), which holds along with the need: a change to
    // either starts the count again
    int outputScaleNeed = 0;
    int pendingOutputScaleNeed = 0;
    int pendingOutputScaleNeedFrames = 0;
    bool outputDeband = false;
    bool pendingOutputDeband = false;

    // Replaced by updateConfiguration() from other threads, so only used through loadConfiguration() and storeConfiguration() (MelonInstance.cpp)
    std::shared_ptr<EmulatorConfiguration> currentConfiguration;
    FrameQueue frameQueue;
    std::unique_ptr<ScreenshotRenderer> screenshotRenderer;
    RewindManager rewindManager;
    Renderer currentRenderer;
    // The requested renderer whose New() failed, so the software fallback is in use, only tried again once a different renderer is asked for
    std::optional<Renderer> failedRenderer;
    // Set by updateConfiguration() from other threads, after it replaced the configuration
    std::atomic_bool isRenderConfigurationDirty;
    // The GPU 2D shadow's mode (GPU::SetShadow2D) the "Sharper rotated 2D" setting wants (0 or 1), whether WideMelon's Gen 4 battles can have
    // mode 3 without it, whether debug.melonade.gpu2d decides instead, and gpu2dwide; from updateRenderer(), for applyShadow2DMode()
    int shadow2DSettingMode = 0;
    bool canShadow2DWideBattles = false;
    bool isShadow2DOverridden = false;
    int shadow2DWide = 0;
#ifdef MELONADE_DEBUG_OVERRIDES
    // The debug.melonade.* switches that override settings (see DebugProperties.h), or on the desktop its command line's
    DebugProperties::Overrides debugOverrides;
#endif
    int frame;
    // The frame of the rewind state whose screenshot is still being read back, or -1
    int pendingRewindScreenshotFrame = -1;
    // Rewind settings from updateConfiguration() (other threads), applied by the emulator thread before its next frame since a change can free the
    // states it's using
    struct RewindSettings
    {
        bool enabled;
        int lengthSeconds;
        int captureSpacingSeconds;
    };
    std::mutex rewindSettingsMutex;
    std::optional<RewindSettings> pendingRewindSettings;
    std::atomic_bool hasPendingRewindSettings = false;
    // CRC32 of the loaded ROM file, only worked out while the debug log is on
    bool hasRomCrc32 = false;
    melonDS::u32 romCrc32 = 0;
};

}

#endif
