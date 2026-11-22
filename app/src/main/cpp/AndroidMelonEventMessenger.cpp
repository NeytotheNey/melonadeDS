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
    // The error is cut short to fit the event's data, which is enough for a toast (the full error goes to the log), and before a UTF-8 continuation
    // byte so no character is split
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

void AndroidMelonEventMessenger::onLuaSetLayout(int scriptId, const std::string& optionKey)
{
    // Option keys are at most 32 bytes (the app checks them), so the key always fits
    struct {
        int32_t scriptId;
        int32_t keySize;
        char key[32];
    } data;
    size_t keySize = std::min(optionKey.size(), sizeof(data.key));

    data.scriptId = (int32_t) scriptId;
    data.keySize = (int32_t) keySize;
    memcpy(data.key, optionKey.data(), keySize);

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_SET_LAYOUT, 2 * sizeof(int32_t) + keySize, &data);
}

void AndroidMelonEventMessenger::onLuaTiltShift(int scriptId, float top, float bottom, float center, float width, float radius, float saturation, float contrast, float color)
{
    struct {
        int32_t scriptId;
        float top;
        float bottom;
        float center;
        float width;
        float radius;
        float saturation;
        float contrast;
        float color;
    } data = { (int32_t) scriptId, top, bottom, center, width, radius, saturation, contrast, color };

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_TILT_SHIFT, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaBloom(int scriptId, float strength, float threshold, float radius, float vignette, float vignetteSize,
    bool onlyThreeD, float haze, int hazeColor, float hazeStart, float hazeFalloff)
{
    // Read in this order by AndroidEmulatorManager (EventLuaBloom)
    struct {
        int32_t scriptId;
        float strength;
        float threshold;
        float radius;
        float vignette;
        float vignetteSize;
        int32_t onlyThreeD;
        float haze;
        int32_t hazeColor;
        float hazeStart;
        float hazeFalloff;
    } data = {
        (int32_t) scriptId, strength, threshold, radius, vignette, vignetteSize, onlyThreeD ? 1 : 0, haze, (int32_t) hazeColor, hazeStart,
        hazeFalloff,
    };

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_BLOOM, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaDepthOfField(int scriptId, float strength, float radius, float focusX, float focusY, float focusDepth, float range, float falloff)
{
    // Read in this order by AndroidEmulatorManager (EventLuaDepthOfField)
    struct {
        int32_t scriptId;
        float strength;
        float radius;
        float focusX;
        float focusY;
        float focusDepth;
        float range;
        float falloff;
    } data = { (int32_t) scriptId, strength, radius, focusX, focusY, focusDepth, range, falloff };

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_DEPTH_OF_FIELD, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaShade(int scriptId, float strength, float radius, float range, int color)
{
    // Read in this order by AndroidEmulatorManager (EventLuaShade)
    struct {
        int32_t scriptId;
        float strength;
        float radius;
        float range;
        int32_t color;
    } data = { (int32_t) scriptId, strength, radius, range, (int32_t) color };

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_SHADE, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaSun(int scriptId, float strength, const std::array<float, 3>& direction, const std::array<float, 3>& up,
    float length, int steps, float bias, float softness, int color)
{
    // Read in this order by AndroidEmulatorManager (EventLuaSun)
    struct {
        int32_t scriptId;
        float strength;
        float direction[3];
        float up[3];
        float length;
        int32_t steps;
        float bias;
        float softness;
        int32_t color;
    } data = {
        (int32_t) scriptId, strength, { direction[0], direction[1], direction[2] }, { up[0], up[1], up[2] }, length, (int32_t) steps, bias,
        softness, (int32_t) color,
    };

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_SUN, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY)
{
    // Read in this order by AndroidEmulatorManager (EventLuaWideCamera)
    struct {
        int32_t scriptId;
        float zoom;
        int32_t hasPan;
        float pan;
        int32_t holdNative;
        int32_t hasPanY;
        float panY;
    } data = { (int32_t) scriptId, zoom, hasPan ? 1 : 0, pan, holdNative ? 1 : 0, hasPanY ? 1 : 0, panY };

    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_WIDE_CAMERA, sizeof(data), &data);
}

void AndroidMelonEventMessenger::onLuaStateLoaded()
{
    MelonDSAndroid::fireEmulatorEvent(EVENT_LUA_STATE_LOADED);
}
