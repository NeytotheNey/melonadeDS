#include "lua/requests/LuaUiOverrides.h"

#include <algorithm>

namespace Frontend
{

namespace
{

// whether [requests] held one of [scriptId]'s, which is removed
bool RemoveRequest(std::vector<std::pair<int, bool>>& requests, int scriptId)
{
    auto it = std::find_if(requests.begin(), requests.end(), [&](const auto& entry) { return entry.first == scriptId; });
    if (it == requests.end())
        return false;
    requests.erase(it);
    return true;
}

}

LuaUiOverrides::LuaUiOverrides()
{
    ResetFastForwardBaseline();
}

bool LuaUiOverrides::OnScriptRequest(LuaUiSetting setting, int scriptId, bool value, bool currentValue)
{
    if (StoppedScripts.contains(scriptId))
        return currentValue;

    SettingState& state = State(setting);
    if (state.Requests.empty())
    {
        state.Baseline = state.KnownBaseline.value_or(currentValue);
        state.KnownBaseline.reset();
    }
    // removed first, so it moves to the end as the most recent request
    RemoveRequest(state.Requests, scriptId);
    state.Requests.emplace_back(scriptId, value);
    return value;
}

std::map<LuaUiSetting, bool> LuaUiOverrides::OnScriptStopped(int scriptId)
{
    StoppedScripts.insert(scriptId);
    std::map<LuaUiSetting, bool> newValues;
    for (int i = 0; i < SETTING_COUNT; i++)
    {
        SettingState& state = Settings[i];
        if (RemoveRequest(state.Requests, scriptId))
            newValues[static_cast<LuaUiSetting>(i)] = state.Requests.empty() ? state.Baseline : state.Requests.back().second;
    }
    return newValues;
}

void LuaUiOverrides::OnScriptStarted(int scriptId)
{
    StoppedScripts.erase(scriptId);
}

void LuaUiOverrides::OnPlayerChanged(LuaUiSetting setting)
{
    SettingState& state = State(setting);
    state.Requests.clear();
    // the UI now shows the player's own value
    state.KnownBaseline.reset();
}

void LuaUiOverrides::OnSessionStarted(const std::map<LuaUiSetting, bool>& playerValues)
{
    for (const auto& [setting, value] : playerValues)
        State(setting).KnownBaseline = value;
}

std::optional<bool> LuaUiOverrides::ScriptValue(LuaUiSetting setting) const
{
    const SettingState& state = State(setting);
    if (state.Requests.empty())
        return std::nullopt;
    return state.Requests.back().second;
}

std::map<LuaUiSetting, bool> LuaUiOverrides::OnSessionEnded()
{
    std::map<LuaUiSetting, bool> newValues;
    for (int i = 0; i < SETTING_COUNT; i++)
    {
        SettingState& state = Settings[i];
        if (!state.Requests.empty())
        {
            newValues[static_cast<LuaUiSetting>(i)] = state.Baseline;
            state.Requests.clear();
        }
    }
    StoppedScripts.clear();
    ResetFastForwardBaseline();
    return newValues;
}

void LuaUiOverrides::ResetFastForwardBaseline()
{
    State(LuaUiSetting::FastForward).KnownBaseline = false;
}

}
