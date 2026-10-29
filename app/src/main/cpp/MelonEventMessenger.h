#ifndef MELONEVENTMESSENGER_H
#define MELONEVENTMESSENGER_H

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

    // Lua scripts. Called on the emulator thread
    // [scriptId] is the script's position in the list the app sent
    virtual void onLuaScriptStopped(int scriptId, const std::string& error) = 0;
    virtual void onLuaSwapScreens(int scriptId, bool swapped) = 0;
    virtual void onLuaShowControls(int scriptId, bool visible) = 0;
    virtual void onLuaFastForward(int scriptId, bool enabled) = 0;
};

}

#endif // MELONEVENTMESSENGER_H
