#include "lua/requests/LuaLayoutRequests.h"

#include <algorithm>

namespace Frontend
{

std::optional<std::string> LuaLayoutRequests::LayoutId() const
{
    if (Requests.empty())
        return std::nullopt;
    return Requests.back().second;
}

void LuaLayoutRequests::Remove(int scriptId)
{
    Requests.erase(std::remove_if(Requests.begin(), Requests.end(), [&](const auto& entry) { return entry.first == scriptId; }),
        Requests.end());
}

std::optional<std::string> LuaLayoutRequests::OnScriptRequest(int scriptId, const LuaLayoutRequest& request)
{
    if (!StoppedScripts.contains(scriptId) && request.Type != LuaLayoutRequest::Kind::None)
    {
        // removed first, so it moves to the end as the most recent request
        Remove(scriptId);
        if (request.Type == LuaLayoutRequest::Kind::Layout)
            Requests.emplace_back(scriptId, request.Id);
    }
    return LayoutId();
}

std::optional<std::string> LuaLayoutRequests::OnScriptStopped(int scriptId)
{
    StoppedScripts.insert(scriptId);
    Remove(scriptId);
    return LayoutId();
}

void LuaLayoutRequests::OnScriptStarted(int scriptId)
{
    StoppedScripts.erase(scriptId);
}

std::optional<std::string> LuaLayoutRequests::OnPlayerChanged()
{
    Requests.clear();
    return LayoutId();
}

std::optional<std::string> LuaLayoutRequests::OnSessionEnded()
{
    Requests.clear();
    StoppedScripts.clear();
    return LayoutId();
}

std::optional<std::string> GetLuaLayoutOptionLayoutId(const std::string* value)
{
    return LuaScriptOption::GetLayoutId(value ? std::optional<std::string>(*value) : std::nullopt);
}

LuaLayoutRequest GetLuaLayoutRequest(const std::vector<LuaScriptOption>& options, const std::map<std::string, std::string>& values,
    const std::optional<std::string>& optionKey, const std::function<bool(const std::string&)>& layoutExists)
{
    if (!optionKey)
        return LuaLayoutRequest::Player();
    auto option = std::find_if(options.begin(), options.end(), [&](const LuaScriptOption& o) { return o.Key == *optionKey; });
    if (option == options.end() || option->OptionKind != LuaScriptOption::Kind::Layout)
        return LuaLayoutRequest::Nothing();
    auto stored = values.find(*optionKey);
    std::optional<std::string> layoutId = LuaScriptOption::GetLayoutId(option->Resolve(stored == values.end() ? std::nullopt
                                                                                                             : std::optional<std::string>(stored->second)));
    if (!layoutId)
        return LuaLayoutRequest::Nothing();
    return layoutExists(*layoutId) ? LuaLayoutRequest::Of(*layoutId) : LuaLayoutRequest::Nothing();
}

}
