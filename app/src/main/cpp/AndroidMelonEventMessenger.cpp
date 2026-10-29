#include "AndroidMelonEventMessenger.h"
#include "EmulatorMessageQueueJNI.h"
#include <algorithm>
#include <cstring>

void AndroidMelonEventMessenger::onRumbleStart(int durationMs)
{
    MelonDSAndroid::fireEmulatorEvent(EVENT_RUMBLE_START, sizeof(durationMs), &durationMs);
}

void AndroidMelonEventMessenger::onRumbleStop()
{
    MelonDSAndroid::fireEmulatorEvent(EVENT_RUMBLE_STOP);
}

void AndroidMelonEventMessenger::onEmulatorStop(melonDS::Platform::StopReason reason)
{
    int32_t reasonInt = (int32_t) reason;
    MelonDSAndroid::fireEmulatorEvent(EVENT_EMULATOR_STOP, sizeof(reasonInt), &reasonInt);
}

void AndroidMelonEventMessenger::onAchievementPrimed(long achievementId)
{
    int64_t achievementIdLong = (int64_t) achievementId;
    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_ACHIEVEMENT_PRIMED, sizeof(achievementIdLong), &achievementIdLong);
}

void AndroidMelonEventMessenger::onAchievementTriggered(long achievementId)
{
    int64_t achievementIdLong = (int64_t) achievementId;
    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_ACHIEVEMENT_TRIGGERED, sizeof(achievementIdLong), &achievementIdLong);
}

void AndroidMelonEventMessenger::onAchievementUnprimed(long achievementId)
{
    int64_t achievementIdLong = (int64_t) achievementId;
    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_ACHIEVEMENT_UNPRIMED, sizeof(achievementIdLong), &achievementIdLong);
}

void AndroidMelonEventMessenger::onAchievementProgressUpdated(long achievementId, unsigned int current, unsigned int target, std::string progress)
{
    struct {
        int64_t achievementId;
        int32_t current;
        int32_t target;
        int32_t progressSize;
        char progress[32];
    } data = {
        .achievementId = (int64_t) achievementId,
        .current = (int32_t) current,
        .target = (int32_t) target,
        .progressSize = (int32_t) progress.size(),
    };
    strncpy(data.progress, progress.c_str(), sizeof(data.progress));

    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_ACHIEVEMENT_PROGRESS_UPDATED, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLeaderboardAttemptStarted(long leaderboardId)
{
    int64_t leaderboardIdLong = (int64_t) leaderboardId;
    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_LBOARD_ATTEMPT_STARTED, sizeof(leaderboardIdLong), &leaderboardIdLong);
}

void AndroidMelonEventMessenger::onLeaderboardAttemptUpdated(long leaderboardId, std::string formattedValue)
{
    struct {
        int64_t leaderboardId;
        int32_t formattedValueSize;
        char formattedValue[32];
    } data = {
        .leaderboardId = (int64_t) leaderboardId,
        .formattedValueSize = (int32_t) formattedValue.size(),
    };
    strncpy(data.formattedValue, formattedValue.c_str(), sizeof(data.formattedValue));

    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_LBOARD_ATTEMPT_UPDATED, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLeaderboardAttemptCanceled(long leaderboardId)
{
    int64_t leaderboardIdLong = (int64_t) leaderboardId;
    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_LBOARD_ATTEMPT_CANCELED, sizeof(leaderboardIdLong), &leaderboardIdLong);
}

void AndroidMelonEventMessenger::onLeaderboardAttemptCompleted(long leaderboardId, int value, std::string formattedValue)
{
    struct {
        int64_t leaderboardId;
        int32_t value;
        int32_t formattedValueSize;
        char formattedValue[32];
    } data = {
        .leaderboardId = (int64_t) leaderboardId,
        .value = (int32_t) value,
        .formattedValueSize = (int32_t) formattedValue.size(),
    };
    strncpy(data.formattedValue, formattedValue.c_str(), sizeof(data.formattedValue));

    MelonDSAndroid::fireEmulatorEvent(EVENT_RA_LBOARD_ATTEMPT_COMPLETED, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaScriptStopped(int scriptId, const std::string& error)
{
    // The error is cut short to fit the event's data, which is enough for a toast (the full error goes to the log). It's cut before a UTF-8
    // continuation byte, so that no character is split
    struct {
        int32_t scriptId;
        int32_t errorSize;
        char error[504];
    } data;
    size_t errorSize = std::min(error.size(), sizeof(data.error));
    while (errorSize > 0 && errorSize < error.size() && (error[errorSize] & 0xC0) == 0x80)
        errorSize--;

    data.scriptId = (int32_t) scriptId;
    data.errorSize = (int32_t) errorSize;
    memcpy(data.error, error.data(), errorSize);

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_SCRIPT_STOPPED, 2 * sizeof(int32_t) + errorSize, &data);
}

// The Lua UI requests carry the script's id, so the app can undo a script's requests once it stops
namespace
{
struct LuaRequestData
{
    int32_t scriptId;
    int32_t value;
};
}

void AndroidMelonEventMessenger::onLuaSwapScreens(int scriptId, bool swapped)
{
    LuaRequestData data { scriptId, swapped ? 1 : 0 };
    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_SWAP_SCREENS, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaShowControls(int scriptId, bool visible)
{
    LuaRequestData data { scriptId, visible ? 1 : 0 };
    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_SHOW_CONTROLS, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaFastForward(int scriptId, bool enabled)
{
    LuaRequestData data { scriptId, enabled ? 1 : 0 };
    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_FAST_FORWARD, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaStorageSaved()
{
    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_STORAGE_SAVED);
}
