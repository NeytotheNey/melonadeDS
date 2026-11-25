// The pause menu: a panel along the left over the game's last frame, dimmed. Resume, savestates, the screens (together or detached, swap),
// the layout (pick one, or edit the one in use), the Lua scripts (Mods, while the game runs any), settings, reset and quitting. A
// RetroAchievements hardcore session has no Load state and no Mods, as the app's filterRomPauseMenuOption
#include "Icons.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"
#include "layout/Layouts.h"
#include "lua/LuaSession.h"

namespace Ui
{

namespace
{

void Divider(App& app)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float y = pos.y + app.Px(8);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x + app.Px(16), y), ImVec2(pos.x + w - app.Px(16), y + std::max(1.f, app.Px(1))),
        app.Colors().OutlineVariant);
    ImGui::Dummy(ImVec2(1, app.Px(9)));
}

class PauseScreen : public Screen
{
public:
    const char* Id() const override { return "pause"; }
    bool Overlay() const override { return true; }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float footer = FooterHeight(app);

        // the panel, the mockup's 400 wide (the whole window on a narrow one)
        float panelW = std::min(app.Px(420), size.x);
        bool full = panelW >= size.x - app.Px(8);
        draw->AddRectFilled(win, ImVec2(win.x + panelW, win.y + size.y), p.SurfaceContainerLow, full ? 0 : app.Px(28),
            ImDrawFlags_RoundRight);

        // the game: its mark, name, and that it's paused, with the renderer
        float pad = app.Px(24);
        float mark = app.Px(48);
        ImVec2 m0(win.x + pad + app.Px(8), win.y + app.Px(24));
        draw->AddRectFilled(m0, ImVec2(m0.x + mark, m0.y + mark), p.PrimaryContainer, app.Px(12));
        IconAt(app, Icon::VideogameAsset, 30, ImVec2(m0.x + mark / 2, m0.y + mark / 2), p.OnPrimaryContainer);
        float tx = m0.x + mark + app.Px(16), tw = win.x + panelW - pad - tx;
        std::string title = Ellipsize(app, app.Medium, 20, host.GameTitle ? host.GameTitle() : "", tw);
        std::string status = "Paused";
        if (host.RendererStatus)
            status += " \xc2\xb7 " + host.RendererStatus();
        ImVec2 ts = TextSize(app, app.Medium, 20, title.c_str());
        TextAt(app, app.Medium, 20, ImVec2(tx, m0.y + app.Px(4)), p.OnSurface, title.c_str());
        TextAt(app, app.Regular, 15, ImVec2(tx, m0.y + app.Px(4) + ts.y + app.Px(2)), p.OnSurfaceVariant,
            Ellipsize(app, app.Regular, 15, status, tw).c_str());

        // the items, scrolling on a short window; the child's padding leaves room for the focus ring
        float listY = m0.y + mark + app.Px(14);
        // the hints beside the panel when there's room (as the mockup has them), otherwise along the whole bottom under it
        bool footerBeside = size.x - panelW >= app.Px(380);
        float listBottom = win.y + size.y - (footerBeside ? app.Px(8) : footer + app.Px(4));
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(win.x + pad - ring, listY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::BeginChild("##items", ImVec2(panelW - 2 * pad + 2 * ring, listBottom - listY),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(2)));

        const char* resumeHint = app.UsingPad() ? "B" : "Esc";
        if (MenuRow(app, "##resume", Icon::PlayArrow, "Resume", resumeHint, true))
            app.CloseAll();
        if (MenuRow(app, "##save", Icon::Save, "Save state", nullptr))
            app.Push(MakeStatesScreen(true));
        bool hardcore = host.HardcoreSession && host.HardcoreSession();
        if (!hardcore && MenuRow(app, "##load", Icon::History, "Load state", nullptr))
            app.Push(MakeStatesScreen(false));
        Divider(app);

        bool detached = host.IsDetached && host.IsDetached();
        if (ToggleRow(app, "##detach", Icon::OpenInNew, "Detached screens", "Each screen in its own window", detached) && host.SetDetached)
            host.SetDetached(!detached);
        bool swapped = host.IsSwapped && host.IsSwapped();
        if (ToggleRow(app, "##swap", Icon::SwapVert, "Swap screens", nullptr, swapped) && host.ToggleSwap)
            host.ToggleSwap();
        {
            const Frontend::LayoutChoice* inUse = host.Layouts && host.LayoutInUse ? host.Layouts->Find(host.LayoutInUse()) : nullptr;
            if (MenuRow(app, "##layout", Icon::Dashboard, "Layout", inUse ? inUse->DisplayName.c_str() : nullptr))
                app.Push(MakeLayoutsScreen(LayoutsFor::InGame));
            // the layout in use in the layout editor; a built-in one can't change, so a new layout starts from what it shows
            bool builtIn = inUse && inUse->BuiltIn != Frontend::BuiltInLayout::None;
            if (MenuRow(app, "##editlayout", Icon::Edit, builtIn ? "New layout from this one" : "Edit layout", nullptr))
                app.Push(MakeLayoutEditorScreen({LayoutEditorOpen::What::InUse, ""}));
        }
        Divider(app);

        // while this session loaded a script, even once they're all switched off (the app's filterRomPauseMenuOption)
        if (!hardcore && host.Lua && host.Lua->HasScripts() && MenuRow(app, "##mods", Icon::Extension, "Mods", nullptr))
            app.Push(MakeModsScreen());
        if (MenuRow(app, "##settings", Icon::Settings, "Settings", nullptr))
            app.Push(MakeSettingsScreen());
        if (MenuRow(app, "##reset", Icon::RestartAlt, "Reset", nullptr))
        {
            if (host.Reset)
                host.Reset();
            app.CloseAll();
            app.Toast("Reset");
        }
        Divider(app);
        if (MenuRow(app, "##tostart", Icon::Logout, "Quit to library", nullptr) && host.QuitToStart)
            host.QuitToStart();
        if (MenuRow(app, "##quit", Icon::Power, "Quit melonadeDS", nullptr) && host.Quit)
            host.Quit();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        FooterHints(app, {{"A|Enter", "Select"}, {"B|Esc", "Resume"}}, WithAlpha(p.SurfaceContainerLowest, 0.92f), footerBeside ? panelW : 0);
    }

};

}

std::unique_ptr<Screen> MakePauseScreen()
{
    return std::make_unique<PauseScreen>();
}

std::unique_ptr<Screen> MakePendingUnlocksQuestion(std::function<void()> stop)
{
    printf("ui: ask stop with unlocks pending\n");
    fflush(stdout);
    return MakePanelScreen("ra/pending", "Some unlocks haven't reached RetroAchievements yet.", "Stop now and they're lost.",
        [stop](App& app)
        {
            if (MenuRow(app, "##keep", Icon::PlayArrow, "Keep playing", nullptr, true))
                app.Pop();
            else if (MenuRow(app, "##stop", Icon::Power, "Stop anyway", nullptr))
            {
                app.Pop();
                stop();
            }
        });
}

}
