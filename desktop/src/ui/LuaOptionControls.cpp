#include "LuaOptionControls.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

#include "Host.h"
#include "Icons.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"
#include "layout/Layouts.h"
#include "lua/LuaText.h"

namespace Ui
{

namespace
{

using Frontend::LuaLayoutChoice;
using Frontend::LuaScriptOption;

// the app's lua_script_option_layout_dont_change and lua_script_option_layout_missing
constexpr const char* kDontChange = "Don't change";
constexpr const char* kLayoutMissing = "Layout not found";

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

// a list over the screen to pick one of [labels] from, the one at [current] marked (none for -1); [picked] gets the index once the list
// has closed
void OpenPickList(App& app, const std::string& title, std::vector<std::string> labels, int current, std::function<void(App&, int)> picked)
{
    app.Push(MakePanelScreen("lua-option/pick", title, "", [labels = std::move(labels), current, picked = std::move(picked)](App& app)
    {
        for (int i = 0; i < (int)labels.size(); i++)
        {
            std::string id = "##pick" + std::to_string(i);
            if (MenuRow(app, id.c_str(), i == current ? Icon::Check : 0, labels[i].c_str(), nullptr, i == current))
            {
                app.Pop();
                picked(app, i);
                return;
            }
        }
    }));
}

void ToggleControl(App& app, const LuaScriptOption& option, const std::string& value, const LuaOptionChanged& changed)
{
    bool on = value == "true";
    if (ToggleRow(app, "##toggle", 0, option.Label.c_str(), nullptr, on))
        changed(app, option.Key, on ? "false" : "true");
}

// a slider over the option's steps, its value shown as the header's decimals give it
bool NumberControl(App& app, const LuaScriptOption& option, const std::string& value, const LuaOptionChanged& changed)
{
    int step = option.StepOf(Frontend::LuaText::ParseDouble(value).value_or(option.NumberDefault)).value_or(0);
    int to = SliderRow(app, "##number", 0, option.Label.c_str(), [&option](int s) { return option.Format(option.ValueAt(s)); }, step,
        option.StepCount);
    bool focused = ImGui::IsItemFocused();
    if (to >= 0 && to != step)
        changed(app, option.Key, option.Format(option.ValueAt(to)));
    return focused;
}

// Left / Right go through the values, confirm opens the list of them
bool ChoiceControl(App& app, const LuaScriptOption& option, const std::string& value, const LuaOptionChanged& changed)
{
    int count = (int)option.Values.size();
    int index = (int)(std::find(option.Values.begin(), option.Values.end(), value) - option.Values.begin());
    bool confirmed = false;
    int change = ChoiceRow(app, "##choice", 0, option.Label.c_str(), value.c_str(), false, nullptr, &confirmed);
    bool focused = ImGui::IsItemFocused();
    if (count == 0)
        return focused;
    if (change != 0)
    {
        int next = index >= count ? (change > 0 ? 0 : count - 1) : (index + change + count) % count;
        if (option.Values[next] != value)
            changed(app, option.Key, option.Values[next]);
    }
    else if (confirmed)
    {
        OpenPickList(app, option.Label, option.Values, index < count ? index : -1,
            [key = option.Key, values = option.Values, changed](App& app, int i) { changed(app, key, values[i]); });
    }
    return focused;
}

// "Don't change" and the player's layouts, a deleted one shown as not found
void LayoutControl(App& app, const LuaScriptOption& option, const std::string& value, const LuaOptionChanged& changed)
{
    std::optional<std::string> layoutId = LuaScriptOption::GetLayoutId(value);
    std::vector<LuaLayoutChoice> layouts = LuaLayoutChoices(app.GetHost());
    std::string shown = kDontChange;
    int current = 0;
    if (layoutId)
    {
        shown = kLayoutMissing;
        current = -1;
        for (int i = 0; i < (int)layouts.size(); i++)
        {
            if (Lower(layouts[i].Id) == *layoutId)
            {
                shown = LuaLayoutName(layouts[i]);
                current = i + 1;
            }
        }
    }
    if (!DetailRow(app, "##layout", 0, option.Label.c_str(), shown.c_str()))
        return;
    std::vector<std::string> labels {kDontChange};
    std::vector<std::string> ids {LuaScriptOption::DONT_CHANGE};
    for (const LuaLayoutChoice& layout : layouts)
    {
        labels.push_back(LuaLayoutName(layout));
        ids.push_back(LuaScriptOption::GetLayoutId(layout.Id).value_or(layout.Id));
    }
    OpenPickList(app, option.Label, std::move(labels), current, [key = option.Key, ids = std::move(ids), value, changed](App& app, int i)
    {
        if (ids[i] != value)
            changed(app, key, ids[i]);
    });
}

}

std::vector<LuaLayoutChoice> LuaLayoutChoices(Host& host)
{
    if (!host.Layouts)
        return {};
    std::vector<Frontend::LayoutConfiguration> configs;
    for (const Frontend::LayoutChoice& choice : host.Layouts->All())
        configs.push_back(choice.Config);
    return Frontend::ToLuaLayoutChoices(configs);
}

std::string LuaLayoutName(const LuaLayoutChoice& choice)
{
    if (choice.Name)
        return *choice.Name;
    return choice.UnnamedNumber > 0 ? "Unnamed layout " + std::to_string(choice.UnnamedNumber) : "Unnamed layout";
}

bool LuaOptionRows(App& app, const std::vector<LuaScriptOption>& options, const std::map<std::string, std::string>& values,
    const LuaOptionChanged& changed)
{
    bool changeFocused = false;
    for (int i = 0; i < (int)options.size(); i++)
    {
        const LuaScriptOption& option = options[i];
        if (std::optional<std::string> heading = Frontend::SectionHeadingBefore(options, i))
            SectionLabel(app, heading->c_str());
        auto stored = values.find(option.Key);
        std::string value = stored != values.end() ? stored->second : option.Resolve(std::nullopt);
        ImGui::PushID(option.Key.c_str());
        switch (option.OptionKind)
        {
        case LuaScriptOption::Kind::Toggle:
            ToggleControl(app, option, value, changed);
            break;
        case LuaScriptOption::Kind::Number:
            changeFocused |= NumberControl(app, option, value, changed);
            break;
        case LuaScriptOption::Kind::Choice:
            changeFocused |= ChoiceControl(app, option, value, changed);
            break;
        case LuaScriptOption::Kind::Layout:
            LayoutControl(app, option, value, changed);
            break;
        }
        ImGui::PopID();
    }
    return changeFocused;
}

}
