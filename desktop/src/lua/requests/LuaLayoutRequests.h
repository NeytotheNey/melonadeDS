// The layout scripts ask for with melon.screen.set_layout(optionKey), ported from the app's LuaLayoutRequests.kt: the same rules as
// LuaUiOverrides, with no baseline because none means the player's layout. Layouts are named by their ids (Layouts.h's Id(), the app's UUID
// text form). One thread only (the one the scripts' requests and the player's picks arrive on)
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "lua/LuaScriptOption.h"

namespace Frontend
{

// what a script's melon.screen.set_layout(optionKey) asks for
struct LuaLayoutRequest
{
    enum class Kind
    {
        // set_layout(nil): the player's layout back
        PlayerLayout,
        // the layout with the id [Id]
        Layout,
        // nothing to switch to (the option is "Don't change", its layout was deleted, or the key isn't a layout option), so the earlier
        // request stays
        None,
    };

    Kind Type = Kind::None;
    std::string Id;

    bool operator==(const LuaLayoutRequest&) const = default;

    static LuaLayoutRequest Player() { return {Kind::PlayerLayout, {}}; }
    static LuaLayoutRequest Of(const std::string& layoutId) { return {Kind::Layout, layoutId}; }
    static LuaLayoutRequest Nothing() { return {Kind::None, {}}; }
};

class LuaLayoutRequests
{
public:
    // the layout scripts want shown now, or none for the player's own
    std::optional<std::string> LayoutId() const;

    // a script asked for a layout, or for the player's layout back; Kind::None keeps its earlier one. Returns the layout scripts want shown now
    std::optional<std::string> OnScriptRequest(int scriptId, const LuaLayoutRequest& request);
    std::optional<std::string> OnScriptStopped(int scriptId);
    void OnScriptStarted(int scriptId);
    // the player picked a layout by hand, so no script holds the layout anymore
    std::optional<std::string> OnPlayerChanged();
    std::optional<std::string> OnSessionEnded();

private:
    // ordered from oldest to most recent request, one per script
    std::vector<std::pair<int, std::string>> Requests;
    // like LuaUiOverrides: a request still on its way from a stopped script is ignored
    std::set<int> StoppedScripts;

    void Remove(int scriptId);
};

// the layout a layout option's stored value names (lowercase), or none for "Don't change" or anything that isn't a UUID
// (LuaScriptOption.Layout.getLayoutId)
std::optional<std::string> GetLuaLayoutOptionLayoutId(const std::string* value);

// the request a script's melon.screen.set_layout(optionKey) makes: the layout the player picked for that option in [options] and [values];
// none for [optionKey] is set_layout(nil)
LuaLayoutRequest GetLuaLayoutRequest(const std::vector<LuaScriptOption>& options, const std::map<std::string, std::string>& values,
    const std::optional<std::string>& optionKey, const std::function<bool(const std::string&)>& layoutExists);

}
