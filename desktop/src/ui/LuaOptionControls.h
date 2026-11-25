// The controls for a Lua script's options, the app's LuaScriptOptionControl.kt, shared by a game's settings (inline under the script) and
// the pause menu's Options panel: a switch, a slider, a choice and a layout, under their section headings
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lua/LuaScriptOption.h"

namespace Ui
{

class App;
struct Host;

// what the rows report: [key]'s new canonical value, from the row itself, or from the list a choice or layout row opens once a value is
// picked there (a later frame, after the list closed)
using LuaOptionChanged = std::function<void(App& app, const std::string& key, const std::string& value)>;

// the layouts a layout option offers after "Don't change", in the layouts' list order, and the name one shows as (the app's
// layoutDisplayName)
std::vector<Frontend::LuaLayoutChoice> LuaLayoutChoices(Host& host);
std::string LuaLayoutName(const Frontend::LuaLayoutChoice& choice);

// [options] as rows, each with its value from [values] (resolved values; a missing one shows its default), with a section heading before
// the first option of each section. True when the focus is on a slider or a choice, whose value Left / Right change (for the footer hints)
bool LuaOptionRows(App& app, const std::vector<Frontend::LuaScriptOption>& options, const std::map<std::string, std::string>& values,
    const LuaOptionChanged& changed);

}
