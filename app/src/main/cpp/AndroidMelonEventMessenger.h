#ifndef ANDROIDMELONEVENTMESSENGER_H
#define ANDROIDMELONEVENTMESSENGER_H

#include <MelonEventMessenger.h>

class AndroidMelonEventMessenger : public MelonDSAndroid::MelonEventMessenger
{
public:
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
    void onLuaStorageSaved() override;
    void onLuaSetLayout(int scriptId, const std::string& optionKey) override;
    void onLuaTiltShift(int scriptId, float top, float bottom, float center, float width, float radius, float saturation, float contrast, float color) override;
    void onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY) override;
    void onLuaStateLoaded() override;

private:
    // Event type constants
    static constexpr int EVENT_RUMBLE_START = 100;
    static constexpr int EVENT_RUMBLE_STOP = 101;
    static constexpr int EVENT_EMULATOR_STOP = 102;

    static constexpr int EVENT_RA_ACHIEVEMENT_PRIMED = 200;
    static constexpr int EVENT_RA_ACHIEVEMENT_TRIGGERED = 201;
    static constexpr int EVENT_RA_ACHIEVEMENT_UNPRIMED = 202;
    static constexpr int EVENT_RA_ACHIEVEMENT_PROGRESS_UPDATED = 203;
    static constexpr int EVENT_RA_LBOARD_ATTEMPT_STARTED = 210;
    static constexpr int EVENT_RA_LBOARD_ATTEMPT_UPDATED = 211;
    static constexpr int EVENT_RA_LBOARD_ATTEMPT_CANCELED = 212;
    static constexpr int EVENT_RA_LBOARD_ATTEMPT_COMPLETED = 213;

    static constexpr int EVENT_LUA_SCRIPT_STOPPED = 300;
    static constexpr int EVENT_LUA_SWAP_SCREENS = 301;
    static constexpr int EVENT_LUA_SHOW_CONTROLS = 302;
    static constexpr int EVENT_LUA_FAST_FORWARD = 303;
    static constexpr int EVENT_LUA_STORAGE_SAVED = 304;
    static constexpr int EVENT_LUA_SET_LAYOUT = 305;
    static constexpr int EVENT_LUA_STATE_LOADED = 306;
    static constexpr int EVENT_LUA_TILT_SHIFT = 307;
    static constexpr int EVENT_LUA_WIDE_CAMERA = 308;
};

#endif // ANDROIDMELONEVENTMESSENGER_H
