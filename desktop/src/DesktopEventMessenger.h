// The desktop's MelonEventMessenger (the app's is AndroidMelonEventMessenger, which calls into Kotlin): the emulator's events, which
// come from the emulator thread, are posted to the UI thread and run there at its next Drain(). Rumble goes to OnRumble, the Lua scripts'
// requests to the OnLua callbacks with their values read as AndroidEmulatorManager reads the app's events, and the stop to OnStop;
// RetroAchievements are only logged for now
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "MelonEventMessenger.h"
#include "camera/WideCamera.h"
#include "render/PresentFrame.h"

namespace Frontend
{

class DesktopEventMessenger : public MelonDSAndroid::MelonEventMessenger
{
public:
    // UI thread: what to do when the emulator stops (the core's SignalStop), set before the emulator thread starts
    std::function<void(melonDS::Platform::StopReason)> OnStop;
    // UI thread: the Rumble Pak's motor on (for up to [durationMs]) and off
    std::function<void(int durationMs)> OnRumble;
    std::function<void()> OnRumbleStop;
    // UI thread: what the scripts ask for, as the app's EmulatorEvent.Lua* events carry it: a script that stopped with its error, the swap,
    // controls and fast forward, a layout option's key (none: set_layout(nil)), the screen effects, the WideMelon camera, and a state a
    // script loaded
    std::function<void(int scriptId, const std::string& error)> OnLuaScriptStopped;
    std::function<void(int scriptId, bool swapped)> OnLuaSwapScreens;
    std::function<void(int scriptId, bool visible)> OnLuaShowControls;
    std::function<void(int scriptId, bool enabled)> OnLuaFastForward;
    std::function<void(int scriptId, const std::optional<std::string>& optionKey)> OnLuaSetLayout;
    std::function<void(int scriptId, const TiltShift& tiltShift)> OnLuaTiltShift;
    std::function<void(int scriptId, const Bloom& bloom)> OnLuaBloom;
    std::function<void(int scriptId, const DepthOfField& depthOfField)> OnLuaDepthOfField;
    std::function<void(int scriptId, const Shade& shade)> OnLuaShade;
    std::function<void(int scriptId, const Sun& sun)> OnLuaSun;
    std::function<void(int scriptId, const WideCameraRequest& request)> OnLuaWideCamera;
    std::function<void()> OnLuaStateLoaded;
    // Any thread (the emulator thread, called there and not posted): storage snapshots are waiting in LuaStorage's outbox, so the app's
    // IO-thread save can drain them
    std::function<void()> OnLuaStorageSaved;

    // Runs [task] on the UI thread at its next Drain(); any thread
    void Post(std::function<void()> task);
    // UI thread: runs what was posted since the last call, in order
    void Drain();
    // UI thread: the Lua events of the session that ended (its instance stopped) are dropped from here on, also those still waiting for a
    // Drain(), so an old script's request can't reach the next session's (the app's luaUiEventSession)
    void EndLuaSession() { LuaSession++; }

    void onRumbleStart(int durationMs) override;
    void onRumbleStop() override;
    void onEmulatorStop(melonDS::Platform::StopReason reason) override;

    void onAchievementPrimed(long achievementId) override;
    void onAchievementTriggered(long achievementId) override;
    void onAchievementUnprimed(long achievementId) override;
    void onAchievementProgressUpdated(long achievementId, unsigned int current, unsigned int target, std::string progress) override;
    void onLeaderboardAttemptStarted(long leaderboardId) override;
    void onLeaderboardAttemptUpdated(long leaderboardId, std::string formattedValue) override;
    void onLeaderboardAttemptCanceled(long leaderboardId) override;
    void onLeaderboardAttemptCompleted(long leaderboardId, int value, std::string formattedValue) override;

    void onLuaScriptStopped(int scriptId, const std::string& error) override;
    void onLuaSwapScreens(int scriptId, bool swapped) override;
    void onLuaShowControls(int scriptId, bool visible) override;
    void onLuaFastForward(int scriptId, bool enabled) override;
    void onLuaSetLayout(int scriptId, const std::string& optionKey) override;
    void onLuaTiltShift(int scriptId, float top, float bottom, float center, float width, float radius, float saturation, float contrast, float color) override;
    void onLuaBloom(int scriptId, float strength, float threshold, float radius, float vignette, float vignetteSize, bool onlyThreeD, float haze,
        int hazeColor, float hazeStart, float hazeFalloff, bool lights, float lightThreshold, float lightStrength) override;
    void onLuaDepthOfField(int scriptId, float strength, float radius, float focusX, float focusY, float focusDepth, float range, float falloff) override;
    void onLuaShade(int scriptId, float strength, float radius, float range, int color) override;
    void onLuaSun(int scriptId, float strength, const std::array<float, 3>& direction, const std::array<float, 3>& up, float length, int steps,
        float bias, float softness, int color, float nearLimit) override;
    void onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY) override;
    void onLuaStateLoaded() override;
    void onLuaStorageSaved() override;

private:
    // Post, for a Lua event: dropped if its session ended before it runs
    void PostLua(std::function<void()> task);

    std::mutex Lock;
    std::vector<std::function<void()>> Pending;
    // the Lua events' session, bumped by the UI thread and read by the emulator thread as it posts
    std::atomic<unsigned> LuaSession = 0;
};

}
