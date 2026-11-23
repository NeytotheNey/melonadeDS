// A card in the middle of the window over the screen below (dimmed), with a title, a line or two of text and rows the caller draws: the
// questions ("Delete this layout?", "Save your changes?") and the layout editor's menus. Controller, keyboard and mouse reach its rows as
// any screen's; Back closes it
#include <algorithm>

#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

class PanelScreen : public Screen
{
public:
    PanelScreen(std::string id, std::string title, std::string body, std::function<void(App&)> rows)
        : Name(std::move(id)), Title(std::move(title)), Body(std::move(body)), Rows(std::move(rows))
    {
    }

    const char* Id() const override { return Name.c_str(); }
    bool Overlay() const override { return true; }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float pad = app.Px(24), ring = app.Px(12);
        float w = std::min(size.x - app.Px(32), app.Px(560));
        float textW = w - 2 * pad;
        ImVec2 titleSize = TextSize(app, app.Medium, 22, Title.c_str(), textW);
        ImVec2 bodySize = Body.empty() ? ImVec2(0, 0) : TextSize(app, app.Regular, 15, Body.c_str(), textW);
        float head = pad + titleSize.y + (Body.empty() ? 0 : app.Px(10) + bodySize.y) + app.Px(16);
        // the rows' height from the last frame (a guess on the first), the card no taller than the window
        float rowsH = std::min(RowsHeight > 0 ? RowsHeight : app.Px(240), size.y - app.Px(48) - head - pad);
        float h = head + rowsH + pad;
        ImVec2 pos(win.x + (size.x - w) / 2, win.y + std::max(app.Px(24), (size.y - h) / 2));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), p.SurfaceContainerLow, app.Px(28));
        draw->AddRect(pos, ImVec2(pos.x + w, pos.y + h), p.OutlineVariant, app.Px(28), std::max(1.f, app.Px(1)));
        TextAt(app, app.Medium, 22, ImVec2(pos.x + pad, pos.y + pad), p.OnSurface, Title.c_str(), textW);
        if (!Body.empty())
            TextAt(app, app.Regular, 15, ImVec2(pos.x + pad, pos.y + pad + titleSize.y + app.Px(10)), p.OnSurfaceVariant, Body.c_str(), textW);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + pad - ring, pos.y + head - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(2)));
        ImGui::BeginChild("##rows", ImVec2(textW + 2 * ring, rowsH + 2 * ring), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        float top = ImGui::GetCursorPosY();
        if (Rows)
            Rows(app);
        RowsHeight = ImGui::GetCursorPosY() - top - app.Px(2);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

private:
    std::string Name, Title, Body;
    std::function<void(App&)> Rows;
    float RowsHeight = 0;
};

}

std::unique_ptr<Screen> MakePanelScreen(const std::string& id, const std::string& title, const std::string& body, std::function<void(App&)> rows)
{
    return std::make_unique<PanelScreen>(id, title, body, std::move(rows));
}

}
