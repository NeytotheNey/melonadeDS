// The layouts to pick from, as the app's layout list (LayoutsScreen): the Default layout, the desktop's own four arrangements and the
// custom ones from layouts.json, the one picked marked, focused when the screen opens. Three ways in: Settings > Display picks the global
// layout (input_layout_id), a game's settings pick its own (with "Use global layout" first, as the app's RomConfig layout choice), and the
// pause menu picks one while the game runs, stored where the one in use came from (the game's own, the run's --layout, or the global one).
// As in the app, New layout opens the layout editor on a new one, and a custom layout's options button (Right from its row) edits, renames,
// copies or deletes it (asked first; the app deletes at once with an Undo); the built-in ones have none
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
        // a rename or a copy's name typed, before the list's layouts are read (storing it rebuilds them)
        FinishKeyboard(app);
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

        // as the app's list: a new layout starts from the Default
        if (MenuRow(app, "##new", Icon::Add, "New layout", "Starts from the Default"))
            app.Push(MakeLayoutEditorScreen({LayoutEditorOpen::What::New, ""}));
        if (Mode == LayoutsFor::Game)
        {
            std::string hint = "Now " + global.DisplayName;
            if (MenuRow(app, "##global", picked.empty() ? Icon::Check : Icon::Dashboard, "Use global layout", hint.c_str(), picked.empty()))
                Pick(app, "", "Use global layout");
        }
        DrawRows(app, layouts, picked);
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
        FooterHints(app, {{"A|Enter", "Pick"}, {"Right|Right", "Options"}, {"B|Esc", "Back"}}, WithAlpha(p.SurfaceContainerLowest, 0.92f));
        OpenTyping(app);
    }

private:
    // the layouts, each a row that picks it, and a custom one's options button beside it (Right reaches it, as the app's options button)
    void DrawRows(App& app, const Frontend::Layouts& layouts, const std::string& picked)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
        if (!ImGui::BeginTable("##rows", 2, ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX))
        {
            ImGui::PopStyleVar();
            return;
        }
        ImGui::TableSetupColumn("##row", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##options", ImGuiTableColumnFlags_WidthFixed, app.Px(60));
        std::string options;
        for (const LayoutChoice& c : layouts.All())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string id = "##" + c.Id();
            bool isPicked = c.Id() == picked;
            std::string hint = Kind(c);
            if (Mode == LayoutsFor::Global && isPicked)
                hint = "Global · " + hint;
            if (MenuRow(app, id.c_str(), isPicked ? Icon::Check : c.BuiltIn != Frontend::BuiltInLayout::None ? Icon::DesktopWindows : Icon::Dashboard,
                    c.DisplayName.c_str(), hint.c_str(), isPicked))
                Pick(app, c.Id(), c.DisplayName);
            ImGui::TableNextColumn();
            // the app's built-in Default has no options; nor have the desktop's arrangements
            if (c.BuiltIn == Frontend::BuiltInLayout::None && OptionsButton(app, ("##opt" + c.Id()).c_str()))
                options = c.Id();
        }
        ImGui::EndTable();
        ImGui::PopStyleVar();
        // after the list, which opening them can't change any more
        if (!options.empty())
            OpenOptions(app, options);
    }

    bool OptionsButton(App& app, const char* id)
    {
        const Palette& p = app.Colors();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float h = app.Px(52);
        ImVec2 size(app.Px(52), h);
        pos.x += app.Px(4);
        ImGui::SetCursorScreenPos(pos);
        bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
        app.TrackFocus(false);
        bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 max(pos.x + size.x, pos.y + size.y);
        if (focused || hovered)
            draw->AddRectFilled(pos, max, focused ? p.SecondaryContainer : WithAlpha(p.OnSurface, 0.08f), h / 2);
        if (focused)
            FocusRing(app, draw, pos, max, h / 2);
        IconAt(app, Icon::MoreVert, 26, ImVec2(pos.x + size.x / 2, pos.y + h / 2), focused ? p.OnSecondaryContainer : p.OnSurfaceVariant);
        return pressed;
    }

    // a custom layout's options, as the app's menu on its row: edit, copy (asking a name), delete; plus rename (the app renames in the editor)
    void OpenOptions(App& app, const std::string& id)
    {
        const LayoutChoice* c = app.GetHost().Layouts->Find(id);
        if (!c)
            return;
        LayoutsScreen* self = this;
        app.Push(MakePanelScreen("layouts/options", c->DisplayName, "", [self, id](App& app)
        {
            const Frontend::LayoutConfiguration* config = app.GetHost().Layouts->FindCustom(id);
            if (!config)
            {
                app.Pop();
                return;
            }
            std::string name = config->Name.value_or("");
            if (MenuRow(app, "##edit", Icon::Edit, "Edit", nullptr, true))
            {
                app.Pop();
                app.Push(MakeLayoutEditorScreen({LayoutEditorOpen::What::Layout, id}));
            }
            else if (MenuRow(app, "##rename", Icon::Title, "Rename", nullptr))
            {
                app.Pop();
                self->StartTyping(app, Typing::Rename, id, name);
            }
            else if (MenuRow(app, "##copy", Icon::Layers, "Copy", "A new layout like this one, with a name of its own"))
            {
                app.Pop();
                self->StartTyping(app, Typing::Copy, id, name);
            }
            else if (MenuRow(app, "##delete", Icon::Delete, "Delete", nullptr))
            {
                app.Pop();
                self->AskDelete(app, id, name);
            }
        }));
    }

    void AskDelete(App& app, const std::string& id, const std::string& name)
    {
        LayoutsScreen* self = this;
        std::string body = "It goes from layouts.json for good. Games that use it use the global layout again; if it's the global layout, the "
                           "Default takes its place.";
        app.Push(MakePanelScreen("layouts/delete", "Delete \"" + (name.empty() ? std::string("Unnamed layout") : name) + "\"?", body,
            [self, id, name](App& app)
            {
                if (MenuRow(app, "##cancel", Icon::Close, "Cancel", nullptr, true))
                    app.Pop();
                else if (MenuRow(app, "##delete", Icon::Delete, "Delete", nullptr))
                {
                    app.Pop();
                    self->Delete(app, id, name);
                }
            }));
    }

    void Delete(App& app, const std::string& id, const std::string& name)
    {
        Host& host = app.GetHost();
        bool wasGlobal = host.Settings && host.Settings->GetString("input_layout_id", "") == id;
        if (!host.Layouts->Remove(id))
        {
            app.Toast("Couldn't save layouts.json");
            return;
        }
        // first: the list was made again, so the frontend's own pointers into it go stale until this
        if (host.LayoutsChanged)
            host.LayoutsChanged();
        // the app's fallback when the selected layout goes (LayoutsViewModel.applyFallbackLayout)
        if (wasGlobal && host.ChangeSetting)
            host.ChangeSetting("input_layout_id", picojson::value(std::string(Frontend::Layouts::DEFAULT_ID)));
        app.Toast("Layout \"" + name + "\" deleted");
    }

    enum class Typing { None, Rename, Copy };

    // the keyboard opens once the list has its focus back from the options card (OpenTyping), so the row gets it again when it closes
    void StartTyping(App& app, Typing what, const std::string& id, const std::string& name)
    {
        (void)app;
        TypingFor = what;
        TypingId = id;
        TypedText = name;
        TypingOpen = false;
    }

    void OpenTyping(App& app)
    {
        if (TypingFor == Typing::None || TypingOpen || WantFocus)
            return;
        TypingOpen = true;
        app.OpenKeyboard(&TypedText, TypingFor == Typing::Rename ? "Layout name" : "Name of the copy", 40);
    }

    void FinishKeyboard(App& app)
    {
        if (TypingFor == Typing::None || !TypingOpen || app.KeyboardOpen())
            return;
        Typing what = TypingFor;
        TypingFor = Typing::None;
        Host& host = app.GetHost();
        const Frontend::LayoutConfiguration* config = host.Layouts->FindCustom(TypingId);
        size_t start = TypedText.find_first_not_of(' ');
        std::string name = start == std::string::npos ? "" : TypedText.substr(start, TypedText.find_last_not_of(' ') - start + 1);
        // a blank name changes nothing, as the app's rename keeps the old one
        if (!config || name.empty())
            return;
        Frontend::LayoutConfiguration changed = *config;
        changed.Name = name;
        // the app's copyLayout: the same layout without its id, which saving gives a new one
        if (what == Typing::Copy)
            changed.Id = Frontend::Layouts::NewId();
        if (!host.Layouts->Put(changed))
        {
            app.Toast("Couldn't save layouts.json");
            return;
        }
        if (host.LayoutsChanged)
            host.LayoutsChanged();
        app.Toast(what == Typing::Copy ? "Copied as \"" + name + "\"" : "Renamed \"" + name + "\"");
    }

    Typing TypingFor = Typing::None;
    bool TypingOpen = false;
    std::string TypingId, TypedText;

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
