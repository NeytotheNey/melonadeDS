// What scripts ask of the UI's swap, controls and fast-forward settings, ported from the app's LuaUiOverrides.kt: per setting, a stack of
// script requests over the player's baseline, where the newest running script wins and the player's own change clears them. One thread only
// (the one the scripts' requests and the player's changes arrive on)
#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace Frontend
{

// the UI settings Lua scripts can change (the app's LuaUiSetting.kt)
enum class LuaUiSetting
{
    SwapScreens,
    ShowControls,
    FastForward,
};

class LuaUiOverrides
{
public:
    LuaUiOverrides();

    // a script asked for [value]; [currentValue] becomes the baseline if no script holds the setting yet. Returns the value the UI should show
    bool OnScriptRequest(LuaUiSetting setting, int scriptId, bool value, bool currentValue);
    // a script was switched off; returns the value the UI should show for each setting the script held
    std::map<LuaUiSetting, bool> OnScriptStopped(int scriptId);
    // the player switched a stopped script back on, so its new requests count again
    void OnScriptStarted(int scriptId);
    // the player changed [setting] by hand, so no script holds it anymore
    void OnPlayerChanged(LuaUiSetting setting);
    // a session starts with the player's own values for some settings, which become the baselines of its first requests
    void OnSessionStarted(const std::map<LuaUiSetting, bool>& playerValues);
    // the value scripts hold for [setting], or none if no script does
    std::optional<bool> ScriptValue(LuaUiSetting setting) const;
    // the session ended, so every script stopped and everything is forgotten; returns the baseline of each setting a script still held
    std::map<LuaUiSetting, bool> OnSessionEnded();

private:
    struct SettingState
    {
        bool Baseline = false;
        // used as the baseline of the next first request instead of the UI's value, when that value can't be trusted yet
        std::optional<bool> KnownBaseline;
        // ordered from oldest to most recent request, one per script
        std::vector<std::pair<int, bool>> Requests;
    };

    static constexpr int SETTING_COUNT = 3;

    std::array<SettingState, SETTING_COUNT> Settings;
    // a stopped script stays off until the player switches it back on, so a request of its still on its way is ignored
    std::set<int> StoppedScripts;

    SettingState& State(LuaUiSetting setting) { return Settings[static_cast<int>(setting)]; }
    const SettingState& State(LuaUiSetting setting) const { return Settings[static_cast<int>(setting)]; }
    // every session starts with fast-forward off, so the toggle's possibly stale value must not become the baseline
    void ResetFastForwardBaseline();
};

}
