#include "DesktopEventMessenger.h"

#include "Platform.h"

using namespace melonDS;

namespace Frontend
{

void DesktopEventMessenger::Post(std::function<void()> task)
{
    std::lock_guard guard(Lock);
    Pending.push_back(std::move(task));
}

void DesktopEventMessenger::PostLua(std::function<void()> task)
{
    unsigned session = LuaSession.load();
    Post([this, session, task = std::move(task)]
    {
        if (session == LuaSession.load())
            task();
    });
}

void DesktopEventMessenger::Drain()
{
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard guard(Lock);
        tasks.swap(Pending);
    }
    for (auto& task : tasks)
        task();
}

// what's only logged for now, on the UI thread like the rest
#define POST_LOG(...) Post([=] { Platform::Log(Platform::LogLevel::Debug, __VA_ARGS__); })

void DesktopEventMessenger::onRumbleStart(int durationMs)
{
    Post([this, durationMs]
    {
        Platform::Log(Platform::LogLevel::Debug, "rumble start, %d ms\n", durationMs);
        if (OnRumble)
            OnRumble(durationMs);
    });
}

void DesktopEventMessenger::onRumbleStop()
{
    Post([this]
    {
        Platform::Log(Platform::LogLevel::Debug, "rumble stop\n");
        if (OnRumbleStop)
            OnRumbleStop();
    });
}

void DesktopEventMessenger::onEmulatorStop(Platform::StopReason reason)
{
    Post([this, reason] { if (OnStop) OnStop(reason); });
}

void DesktopEventMessenger::onAchievementPrimed(long achievementId) { POST_LOG("achievement %ld primed\n", achievementId); }
void DesktopEventMessenger::onAchievementTriggered(long achievementId) { POST_LOG("achievement %ld triggered\n", achievementId); }
void DesktopEventMessenger::onAchievementUnprimed(long achievementId) { POST_LOG("achievement %ld unprimed\n", achievementId); }

void DesktopEventMessenger::onAchievementProgressUpdated(long achievementId, unsigned int current, unsigned int target, std::string progress)
{
    POST_LOG("achievement %ld progress %u/%u %s\n", achievementId, current, target, progress.c_str());
}

void DesktopEventMessenger::onLeaderboardAttemptStarted(long leaderboardId) { POST_LOG("leaderboard %ld started\n", leaderboardId); }

void DesktopEventMessenger::onLeaderboardAttemptUpdated(long leaderboardId, std::string formattedValue)
{
    POST_LOG("leaderboard %ld at %s\n", leaderboardId, formattedValue.c_str());
}

void DesktopEventMessenger::onLeaderboardAttemptCanceled(long leaderboardId) { POST_LOG("leaderboard %ld canceled\n", leaderboardId); }

void DesktopEventMessenger::onLeaderboardAttemptCompleted(long leaderboardId, int value, std::string formattedValue)
{
    POST_LOG("leaderboard %ld completed: %d (%s)\n", leaderboardId, value, formattedValue.c_str());
}

void DesktopEventMessenger::onLuaScriptStopped(int scriptId, const std::string& error)
{
    PostLua([=, this]
    {
        Platform::Log(Platform::LogLevel::Info, "Lua script %d stopped: %s\n", scriptId, error.c_str());
        if (OnLuaScriptStopped)
            OnLuaScriptStopped(scriptId, error);
    });
}

void DesktopEventMessenger::onLuaSwapScreens(int scriptId, bool swapped)
{
    PostLua([=, this] { if (OnLuaSwapScreens) OnLuaSwapScreens(scriptId, swapped); });
}

void DesktopEventMessenger::onLuaShowControls(int scriptId, bool visible)
{
    PostLua([=, this] { if (OnLuaShowControls) OnLuaShowControls(scriptId, visible); });
}

void DesktopEventMessenger::onLuaFastForward(int scriptId, bool enabled)
{
    PostLua([=, this] { if (OnLuaFastForward) OnLuaFastForward(scriptId, enabled); });
}

void DesktopEventMessenger::onLuaSetLayout(int scriptId, const std::string& optionKey)
{
    // an empty key is set_layout(nil), the player's layout back
    std::optional<std::string> key = optionKey.empty() ? std::nullopt : std::optional<std::string>(optionKey);
    PostLua([=, this] { if (OnLuaSetLayout) OnLuaSetLayout(scriptId, key); });
}

void DesktopEventMessenger::onLuaTiltShift(int scriptId, float top, float bottom, float center, float width, float radius, float saturation,
    float contrast, float color)
{
    // the color is negative when the script didn't pass it
    TiltShift tiltShift {top, bottom, center, width, radius, saturation, contrast, color >= 0.f ? std::optional<float>(color) : std::nullopt};
    PostLua([=, this] { if (OnLuaTiltShift) OnLuaTiltShift(scriptId, tiltShift); });
}

void DesktopEventMessenger::onLuaBloom(int scriptId, float strength, float threshold, float radius, float vignette, float vignetteSize,
    bool onlyThreeD, float haze, int hazeColor, float hazeStart, float hazeFalloff, bool lights, float lightThreshold, float lightStrength)
{
    Bloom bloom {strength, threshold, radius, onlyThreeD, vignette, vignetteSize, haze, (u32)hazeColor, hazeStart, hazeFalloff, lights,
        lightThreshold, lightStrength};
    PostLua([=, this] { if (OnLuaBloom) OnLuaBloom(scriptId, bloom); });
}

void DesktopEventMessenger::onLuaDepthOfField(int scriptId, float strength, float radius, float focusX, float focusY, float focusDepth,
    float range, float falloff)
{
    DepthOfField depthOfField {strength, radius, focusX, focusY, focusDepth, range, falloff};
    PostLua([=, this] { if (OnLuaDepthOfField) OnLuaDepthOfField(scriptId, depthOfField); });
}

void DesktopEventMessenger::onLuaShade(int scriptId, float strength, float radius, float range, int color)
{
    Shade shade {strength, radius, range, (u32)color};
    PostLua([=, this] { if (OnLuaShade) OnLuaShade(scriptId, shade); });
}

void DesktopEventMessenger::onLuaSun(int scriptId, float strength, const std::array<float, 3>& direction, const std::array<float, 3>& up,
    float length, int steps, float bias, float softness, int color, float nearLimit)
{
    Sun sun {strength, direction, up, length, steps, bias, softness, (u32)color, nearLimit};
    PostLua([=, this] { if (OnLuaSun) OnLuaSun(scriptId, sun); });
}

void DesktopEventMessenger::onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY)
{
    // as AndroidEmulatorManager reads EventLuaWideCamera: a zoom of 0 or less is none, and each pan only when the script gave it
    WideCameraRequest request;
    if (zoom > 0.f)
        request.Zoom = zoom;
    if (hasPan)
        request.Pan = pan;
    request.HoldNative = holdNative;
    if (hasPanY)
        request.PanY = panY;
    PostLua([this, scriptId, request]
    {
        Platform::Log(Platform::LogLevel::Debug, "Lua script %d: wide camera zoom %.2f\n", scriptId, request.Zoom.value_or(0.f));
        if (OnLuaWideCamera)
            OnLuaWideCamera(scriptId, request);
    });
}

void DesktopEventMessenger::onLuaStateLoaded()
{
    PostLua([this]
    {
        Platform::Log(Platform::LogLevel::Debug, "Lua script loaded a state\n");
        if (OnLuaStateLoaded)
            OnLuaStateLoaded();
    });
}

void DesktopEventMessenger::onLuaStorageSaved()
{
    if (OnLuaStorageSaved)
        OnLuaStorageSaved();
}

}
