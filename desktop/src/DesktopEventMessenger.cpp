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
    Post([=] { Platform::Log(Platform::LogLevel::Info, "Lua script %d stopped: %s\n", scriptId, error.c_str()); });
}

void DesktopEventMessenger::onLuaSwapScreens(int scriptId, bool swapped) { POST_LOG("Lua script %d: swap screens %d\n", scriptId, swapped); }
void DesktopEventMessenger::onLuaShowControls(int scriptId, bool visible) { POST_LOG("Lua script %d: show controls %d\n", scriptId, visible); }
void DesktopEventMessenger::onLuaFastForward(int scriptId, bool enabled) { POST_LOG("Lua script %d: fast forward %d\n", scriptId, enabled); }

void DesktopEventMessenger::onLuaSetLayout(int scriptId, const std::string& optionKey)
{
    POST_LOG("Lua script %d: layout %s\n", scriptId, optionKey.c_str());
}

void DesktopEventMessenger::onLuaTiltShift(int scriptId, float top, float bottom, float center, float width, float radius, float saturation, float contrast, float color)
{
    POST_LOG("Lua script %d: tilt shift %.2f %.2f\n", scriptId, top, bottom);
}

void DesktopEventMessenger::onLuaBloom(int scriptId, float strength, float threshold, float radius, float vignette, float vignetteSize)
{
    POST_LOG("Lua script %d: bloom %.2f vignette %.2f\n", scriptId, strength, vignette);
}

void DesktopEventMessenger::onLuaWideCamera(int scriptId, float zoom, bool hasPan, float pan, bool holdNative, bool hasPanY, float panY)
{
    POST_LOG("Lua script %d: wide camera zoom %.2f\n", scriptId, zoom);
}

void DesktopEventMessenger::onLuaStateLoaded() { POST_LOG("Lua script loaded a state\n"); }
void DesktopEventMessenger::onLuaStorageSaved() { POST_LOG("Lua storage saved\n"); }

}
