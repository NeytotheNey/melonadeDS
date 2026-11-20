// The start screen, when no game runs: Open ROM (the Windows file dialog), the recent games, Settings and Quit. The game library replaces
// it in phase 2
#include <algorithm>
#include <filesystem>

#include "Icons.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

// a recent game: its name, and its folder under it
bool RecentRow(App& app, const char* id, const std::string& name, const std::string& folder, float width)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(width, app.Px(68));
    bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    app.TrackFocus(false);
    bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    float radius = app.Px(18);
    ImU32 bg = focused ? p.SecondaryContainer : held ? Blend(0, p.OnSurface, 0.12f) : hovered ? WithAlpha(p.OnSurface, 0.08f) : 0;
    draw->AddRectFilled(pos, max, bg, radius);
    if (focused)
        FocusRing(app, draw, pos, max, radius);
    ImU32 on = focused ? p.OnSecondaryContainer : p.OnSurface;
    ImU32 sub = focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
    float cy = pos.y + size.y / 2;
    // the game's mark: a tonal square with a controller
    float mark = app.Px(44);
    ImVec2 m0(pos.x + app.Px(14), cy - mark / 2);
    draw->AddRectFilled(m0, ImVec2(m0.x + mark, m0.y + mark), focused ? p.Primary : p.SurfaceContainerHighest, app.Px(10));
    IconAt(app, Icon::SportsEsports, 26, ImVec2(m0.x + mark / 2, cy), focused ? p.OnPrimary : p.OnSurfaceVariant);
    float tx = m0.x + mark + app.Px(16), tw = max.x - tx - app.Px(16);
    std::string n = Ellipsize(app, app.Medium, 18, name, tw);
    std::string f = Ellipsize(app, app.Regular, 14, folder, tw);
    ImVec2 ns = TextSize(app, app.Medium, 18, n.c_str()), fs = TextSize(app, app.Regular, 14, f.c_str());
    float top = cy - (ns.y + fs.y + app.Px(2)) / 2;
    TextAt(app, app.Medium, 18, ImVec2(tx, top), on, n.c_str());
    TextAt(app, app.Regular, 14, ImVec2(tx, top + ns.y + app.Px(2)), sub, f.c_str());
    return pressed;
}

class StartScreen : public Screen
{
public:
    const char* Id() const override { return "start"; }
    // the root: nothing to go back to
    bool Back(App&) override { return true; }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float pad = app.Px(40), footer = FooterHeight(app);

        // the app's name with its mark
        float mark = app.Px(56);
        ImVec2 m0(win.x + pad, win.y + app.Px(36));
        draw->AddRectFilled(m0, ImVec2(m0.x + mark, m0.y + mark), p.PrimaryContainer, app.Px(16));
        IconAt(app, Icon::VideogameAsset, 34, ImVec2(m0.x + mark / 2, m0.y + mark / 2), p.OnPrimaryContainer);
        ImVec2 title = TextSize(app, app.Medium, 34, "melonadeDS");
        TextAt(app, app.Medium, 34, ImVec2(m0.x + mark + app.Px(18), m0.y + (mark - title.y) / 2 - app.Px(10)), p.OnSurface, "melonadeDS");
        TextAt(app, app.Regular, 16, ImVec2(m0.x + mark + app.Px(18), m0.y + (mark + title.y) / 2 - app.Px(8)), p.OnSurfaceVariant,
            "Open a game to start playing");
        float top = m0.y + mark + app.Px(36);

        // two columns on a wide window: the actions, then the recent games; one under the other on a narrow one
        bool wide = size.x >= app.Px(900);
        float actionsW = wide ? app.Px(380) : size.x - 2 * pad;
        float x = win.x + pad, y = top;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (BigButton(app, "##open", Icon::FolderOpen, "Open ROM", ".nds or .dsi file", true, actionsW, true))
            host.OpenRom();
        float step = ImGui::GetItemRectSize().y + app.Px(14);
        ImGui::SetCursorScreenPos(ImVec2(x, y += step));
        if (BigButton(app, "##settings", Icon::Settings, "Settings", nullptr, false, actionsW))
            app.Push(MakeSettingsScreen());
        step = ImGui::GetItemRectSize().y + app.Px(14);
        ImGui::SetCursorScreenPos(ImVec2(x, y += step));
        if (BigButton(app, "##quit", Icon::Power, "Quit", nullptr, false, actionsW))
            host.Quit();
        y += ImGui::GetItemRectSize().y + app.Px(28);

        float listX = wide ? x + actionsW + app.Px(40) : x;
        float listY = wide ? top - app.Px(38) : y;
        float listW = wide ? win.x + size.x - pad - listX : actionsW;
        ImGui::SetCursorScreenPos(ImVec2(listX - app.Px(20), listY));
        SectionLabel(app, "Recent games");
        listY = ImGui::GetCursorScreenPos().y;
        float listH = win.y + size.y - footer - app.Px(16) - listY;
        std::vector<std::string> recent = host.RecentRoms ? host.RecentRoms() : std::vector<std::string>();
        if (recent.empty())
        {
            const char* empty = "Games you open show up here.";
            const char* note = "Open ROM reads .nds and .dsi files. Zip and 7z archives aren't supported yet: unpack them first.";
            TextAt(app, app.Regular, 17, ImVec2(listX, listY + app.Px(4)), p.OnSurface, empty);
            TextAt(app, app.Regular, 15, ImVec2(listX, listY + app.Px(34)), p.OnSurfaceVariant, note, listW);
        }
        else if (listH > app.Px(40))
        {
            // the child's padding leaves room for the focus ring
            float ring = app.Px(12);
            ImGui::SetCursorScreenPos(ImVec2(listX - ring, listY - ring));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
            ImGui::BeginChild("##recent", ImVec2(listW + 2 * ring, listH + ring), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(6)));
            for (size_t i = 0; i < recent.size(); i++)
            {
                std::filesystem::path path = Frontend::PathFromUtf8(recent[i]);
                ImGui::PushID((int)i);
                if (RecentRow(app, "##game", Frontend::PathToUtf8(path.stem()), Frontend::PathToUtf8(path.parent_path()), listW))
                    host.BootRom(recent[i]);
                ImGui::PopID();
            }
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        FooterHints(app, {{"A|Enter", "Select"}}, p.SurfaceContainerLowest);
    }
};

}

std::unique_ptr<Screen> MakeStartScreen()
{
    return std::make_unique<StartScreen>();
}

}
