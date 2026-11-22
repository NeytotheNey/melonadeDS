// The desktop's MelonEventMessenger (the app's is AndroidMelonEventMessenger, which calls into Kotlin): the emulator's events, which
// come from the emulator thread, are posted to the UI thread and run there at its next Drain(). Rumble goes to OnRumble; RetroAchievements and the Lua
// scripts' requests are only logged for now; the stop is handed to OnStop
#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "MelonEventMessenger.h"

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

    // Runs [task] on the UI thread at its next Drain(); any thread
    void Post(std::function<void()> task);
    // UI thread: runs what was posted since the last call, in order
    void Drain();

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
        int hazeColor, float hazeStart, float hazeFalloff) override;
    void onLuaDepthOfField(int scriptId, float strength, float radius, float focusX, float focusY, float focusDepth, float range, float falloff) override;
    void onLuaShade(int scriptId, float strength, float radius, float range, int color) override;
    void onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY) override;
    void onLuaStateLoaded() override;
    void onLuaStorageSaved() override;

private:
    std::mutex Lock;
    std::vector<std::function<void()>> Pending;
};

}
