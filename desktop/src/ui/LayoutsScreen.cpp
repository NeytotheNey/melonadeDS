// The layouts to pick from, as the app's layout list (LayoutsScreen): the Default layout, the desktop's own four arrangements and the
// custom ones from layouts.json, the one picked marked, focused when the screen opens. Three ways in: Settings > Display picks the global
// layout (input_layout_id), a game's settings pick its own (with "Use global layout" first, as the app's RomConfig layout choice), and the
// pause menu picks one while the game runs, stored where the one in use came from (the game's own, the run's --layout, or the global one)
#include <string>

#include "Icons.h"
#include "Library.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"
#include "layout/Layouts.h"

namespace Ui
{

namespace
{

using Frontend::LayoutChoice;

// the settings' global layout, as the frontend resolves it without a game (input_layout_id, else the desktop's older desktop_layout, else
// the app's Default)
const LayoutChoice& GlobalLayout(const Frontend::Layouts& layouts, const Frontend::Settings& settings)
{
    std::string id = settings.GetString("input_layout_id", "");
    if (const LayoutChoice* c = id.empty() ? nullptr : layouts.Find(id))
        return *c;
    if (id.empty() && settings.Find("desktop_layout"))
    {
        if (const LayoutChoice* c = layouts.FindByName(settings.GetString("desktop_layout", "")))
            return *c;
    }
    return layouts.Default();
}

// what a row says about a layout on its right
std::string Kind(const LayoutChoice& c)
{
    if (c.BuiltIn != Frontend::BuiltInLayout::None)
        return "Built in";
    size_t sizes = c.Config.Variants.size();
    std::string kind = sizes == 1 ? "1 size" : std::to_string(sizes) + " sizes";
    if (c.Config.Detached)
        kind += ", detached windows";
    return kind;
}

class LayoutsScreen : public Screen
{
public:
    LayoutsScreen(LayoutsFor mode, const Frontend::LibraryEntry* entry) : Mode(mode)
    {
        if (entry)
            Entry = *entry;
    }

    const char* Id() const override { return Mode == LayoutsFor::InGame ? "pause/layout" : Mode == LayoutsFor::Game ? "library/layout" : "layouts"; }
    bool Overlay() const override { return Mode == LayoutsFor::InGame; }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float footer = FooterHeight(app);
        if (Mode == LayoutsFor::InGame)
            ImGui::GetWindowDrawList()->AddRectFilled(win, ImVec2(win.x + size.x, win.y + size.y), WithAlpha(p.Surface, 0.94f));

        if (ScreenHeader(app, Mode == LayoutsFor::Game ? "Game layout" : "Layout", true))
        {
            app.Pop();
            return;
        }
        if (!host.Layouts || !host.Settings)
            return;
        const Frontend::Layouts& layouts = *host.Layouts;
        const LayoutChoice& global = GlobalLayout(layouts, *host.Settings);

        // the one picked now
        std::string picked;
        if (Mode == LayoutsFor::Global)
            picked = global.Id();
        else if (Mode == LayoutsFor::Game)
            picked = CurrentConfig(app).LayoutId;
        else if (host.LayoutInUse)
            picked = host.LayoutInUse();
        // a game's layout that's gone shows as the global one, as it plays
        if (Mode == LayoutsFor::Game && !picked.empty() && !layouts.Find(picked))
            picked.clear();

        float pad = app.Px(40);
        float colW = std::min(size.x - 2 * pad, app.Px(760));
        float x = win.x + (size.x - colW) / 2;
        float top = win.y + app.Px(88);
        float ring = app.Px(12);
        if (Mode == LayoutsFor::Game)
        {
            TextAt(app, app.Regular, 15, ImVec2(x, top), p.OnSurfaceVariant, Ellipsize(app, app.Regular, 15, Entry.Name(), colW).c_str());
            top += app.Px(28);
        }
        else if (Mode == LayoutsFor::InGame && host.LayoutSource)
        {
            std::string source = host.LayoutSource();
            const char* where = source == "game" ? "Picking one changes this game's own layout"
                : source == "run" ? "Picking one changes this run's layout (--layout), which isn't saved"
                                  : "Picking one changes the global layout";
            TextAt(app, app.Regular, 15, ImVec2(x, top), p.OnSurfaceVariant, where);
            top += app.Px(28);
        }
        ImGui::SetCursorScreenPos(ImVec2(x - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(4)));
        ImGui::BeginChild("##layouts", ImVec2(colW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);

        if (Mode == LayoutsFor::Game)
        {
            std::string hint = "Now " + global.DisplayName;
            if (MenuRow(app, "##global", picked.empty() ? Icon::Check : Icon::Dashboard, "Use global layout", hint.c_str(), picked.empty()))
                Pick(app, "", "Use global layout");
        }
        for (const LayoutChoice& c : layouts.All())
        {
            std::string id = "##" + c.Id();
            bool isPicked = c.Id() == picked;
            std::string hint = Kind(c);
            if (Mode == LayoutsFor::Global && isPicked)
                hint = "Global · " + hint;
            if (MenuRow(app, id.c_str(), isPicked ? Icon::Check : c.BuiltIn != Frontend::BuiltInLayout::None ? Icon::DesktopWindows : Icon::Dashboard,
                    c.DisplayName.c_str(), hint.c_str(), isPicked))
                Pick(app, c.Id(), c.DisplayName);
        }
        if (layouts.CustomCount() == 0)
        {
            ImGui::Dummy(ImVec2(1, app.Px(8)));
            TextAt(app, app.Regular, 15, ImGui::GetCursorScreenPos(), p.OnSurfaceVariant,
                "Layouts made on the phone show here once its layouts.json is in the profile folder", colW);
            ImGui::Dummy(ImVec2(1, app.Px(40)));
        }
        ImGui::Dummy(ImVec2(1, app.Px(24)));
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        FooterHints(app, {{"A|Enter", "Pick"}, {"B|Esc", "Back"}}, WithAlpha(p.SurfaceContainerLowest, 0.92f));
    }

private:
    Frontend::RomConfig CurrentConfig(App& app) const
    {
        Frontend::Library* lib = app.GetHost().Library;
        return lib ? lib->GetConfig(Frontend::Library::ConfigKey(Entry)) : Entry.Config;
    }

    void Pick(App& app, const std::string& id, const std::string& name)
    {
        Host& host = app.GetHost();
        if (Mode == LayoutsFor::Global)
        {
            if (host.ChangeSetting)
                host.ChangeSetting("input_layout_id", picojson::value(id));
            app.Toast("Layout: " + name);
        }
        else if (Mode == LayoutsFor::Game)
        {
            Frontend::RomConfig config = CurrentConfig(app);
            config.LayoutId = id;
            Frontend::Library* lib = host.Library;
            if (lib && !lib->SetConfig(Frontend::Library::ConfigKey(Entry), config))
                app.Toast("Couldn't save it in the library; it holds until the app closes");
            else
                app.Toast(id.empty() ? "This game uses the global layout" : "This game's layout: " + name);
        }
        else
        {
            std::string source = host.LayoutSource ? host.LayoutSource() : "global";
            if (host.PickLayoutInGame)
                host.PickLayoutInGame(id);
            app.Toast((source == "game" ? "This game's layout: " : source == "run" ? "This run's layout: " : "Layout: ") + name);
        }
        app.Pop();
    }

    LayoutsFor Mode;
    Frontend::LibraryEntry Entry;
};

}

std::unique_ptr<Screen> MakeLayoutsScreen(LayoutsFor mode, const Frontend::LibraryEntry* entry)
{
    return std::make_unique<LayoutsScreen>(mode, entry);
}

}
