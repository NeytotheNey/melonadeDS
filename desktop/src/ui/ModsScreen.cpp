// The pause menu's Mods, the app's LuaScriptsDialog and LuaScriptOptionsDialog: every script this session loaded (and the ones that
// couldn't be read, which can't be switched on) with a switch that stops or starts it for this session only, and for a script with options
// an Options panel that restarts it with the new values and saves them to the game's settings (LuaSession::SetOptions)
#include <map>
#include <memory>
#include <string>

#include "Icons.h"
#include "LuaOptionControls.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"
#include "lua/LuaSession.h"

namespace Ui
{

namespace
{

using Frontend::LuaScriptStatus;

// the second line of a script's row: its description while it runs (the switch already says it does), otherwise why it's off
std::string StateLine(const LuaScriptStatus& script)
{
    if (script.IsRunning)
        return script.Description.value_or("Running");
    if (script.LastError)
        return "Switched off: " + *script.LastError;
    return "Switched off";
}

void ModsRows(App& app)
{
    Frontend::LuaSession* lua = app.GetHost().Lua;
    if (!lua || !lua->HasScripts())
    {
        app.Pop();
        return;
    }
    bool first = true;
    for (const LuaScriptStatus& script : lua->Scripts())
    {
        ImGui::PushID(script.Id);
        std::string state = StateLine(script);
        if (ToggleRow(app, "##script", Icon::Code, script.Name.c_str(), state.c_str(), script.IsRunning, first, script.WasRead))
        {
            if (script.IsRunning)
                lua->Disable(script.Id);
            else if (!lua->Enable(script.Id))
                app.Toast("Couldn't switch " + script.Name + " on");
        }
        first = false;
        if (!script.Options.empty() && MenuRow(app, "##options", Icon::Tune, "Options\xe2\x80\xa6", nullptr))
            app.Push(MakeLuaOptionsScreen(script));
        ImGui::PopID();
    }
    if (MenuRow(app, "##close", Icon::Close, "Close", nullptr))
        app.Pop();
}

}

std::unique_ptr<Screen> MakeModsScreen()
{
    return MakePanelScreen("pause/mods", "Mods", "", ModsRows);
}

std::unique_ptr<Screen> MakeLuaOptionsScreen(const LuaScriptStatus& script)
{
    // the values being changed, from the ones the script runs with; kept by the rows, and by a pick list's callback while it's open
    auto values = std::make_shared<std::map<std::string, std::string>>(script.OptionValues);
    int id = script.Id;
    std::string name = script.Name;
    std::vector<Frontend::LuaScriptOption> options = script.Options;
    return MakePanelScreen("pause/mods/options", name, "", [id, name, options, values](App& app)
    {
        LuaOptionRows(app, options, *values,
            [values](App&, const std::string& key, const std::string& value) { (*values)[key] = value; });
        ImGui::Dummy(ImVec2(1, app.Px(8)));
        if (MenuRow(app, "##save", Icon::RestartAlt, "Save and restart", nullptr))
        {
            app.Pop();
            Frontend::LuaSession* lua = app.GetHost().Lua;
            if (!lua || !lua->SetOptions(id, *values))
                app.Toast("Couldn't restart " + name);
        }
        else if (MenuRow(app, "##cancel", Icon::Close, "Cancel", nullptr))
            app.Pop();
    });
}

}
