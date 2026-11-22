#ifndef MELONEVENTMESSENGER_H
#define MELONEVENTMESSENGER_H

#include <array>
#include <string>
#include <Platform.h>

namespace MelonDSAndroid
{

class MelonEventMessenger
{
public:
    virtual void onRumbleStart(int durationMs) = 0;
    virtual void onRumbleStop() = 0;
    virtual void onEmulatorStop(melonDS::Platform::StopReason reason) = 0;

    virtual void onAchievementPrimed(long achievementId) = 0;
    virtual void onAchievementTriggered(long achievementId) = 0;
    virtual void onAchievementUnprimed(long achievementId) = 0;
    virtual void onAchievementProgressUpdated(long achievementId, unsigned int current, unsigned int target, std::string progress) = 0;
    virtual void onLeaderboardAttemptStarted(long leaderboardId) = 0;
    virtual void onLeaderboardAttemptUpdated(long leaderboardId, std::string formattedValue) = 0;
    virtual void onLeaderboardAttemptCanceled(long leaderboardId) = 0;
    virtual void onLeaderboardAttemptCompleted(long leaderboardId, int value, std::string formattedValue) = 0;

    // Lua scripts, called on the emulator thread, where [scriptId] is the script's position in the list the app sent
    virtual void onLuaScriptStopped(int scriptId, const std::string& error) = 0;
    virtual void onLuaSwapScreens(int scriptId, bool swapped) = 0;
    virtual void onLuaShowControls(int scriptId, bool visible) = 0;
    virtual void onLuaFastForward(int scriptId, bool enabled) = 0;
    // [optionKey] is one of the script's layout options, or empty to go back to the player's layout
    virtual void onLuaSetLayout(int scriptId, const std::string& optionKey) = 0;
    // melon.screen.tilt_shift()'s values, clamped; both strengths are 0 and the color is negative (not passed) when the script no longer wants the effect
    virtual void onLuaTiltShift(int scriptId, float top, float bottom, float center, float width, float radius, float saturation, float contrast, float color) = 0;
    // melon.screen.bloom()'s, vignette()'s and haze()'s values, clamped; a strength of 0 is that effect off (its other values then the
    // defaults). [hazeColor] is 0xRRGGBB, and [hazeStart] and [hazeFalloff] are raw depth
    virtual void onLuaBloom(int scriptId, float strength, float threshold, float radius, float vignette, float vignetteSize, bool onlyThreeD,
        float haze, int hazeColor, float hazeStart, float hazeFalloff) = 0;
    // melon.screen.depth_of_field()'s values, clamped; a strength of 0 is off (the rest then the defaults), and a negative focusDepth means the
    // depth at (focusX, focusY)
    virtual void onLuaDepthOfField(int scriptId, float strength, float radius, float focusX, float focusY, float focusDepth, float range, float falloff) = 0;
    // melon.screen.shade()'s values, clamped; a strength of 0 is off (the rest then the defaults). [range] is raw depth, and [color] is
    // 0xRRGGBB
    virtual void onLuaShade(int scriptId, float strength, float radius, float range, int color) = 0;
    // melon.screen.sun()'s values, clamped, with [direction] and [up] normalized (view space); a strength of 0 is off (the rest then the
    // defaults). [length] and [bias] are view-space units, [softness] DS pixels, and [color] is 0xRRGGBB
    virtual void onLuaSun(int scriptId, float strength, const std::array<float, 3>& direction, const std::array<float, 3>& up, float length,
        int steps, float bias, float softness, int color) = 0;
    // melon.wide's requests, clamped: the zoom (negative for the player's), the pan if [hasPan], whether native is held, and the vertical pan if
    // [hasPanY]; all off when the script no longer asks for anything
    virtual void onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY) = 0;
    // A script loaded one of its savestates
    virtual void onLuaStateLoaded() = 0;
    // Snapshots of the scripts' storage are waiting in LuaStorage's outbox (a wake-up only, without data)
    virtual void onLuaStorageSaved() = 0;
};

}

#endif // MELONEVENTMESSENGER_H
