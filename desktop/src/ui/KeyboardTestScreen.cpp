// A text field and the on-screen keyboard, to check the keyboard with a controller alone (--open keyboard). Nothing else uses a text field
// yet; the library's search and the RetroAchievements login will
#include "Icons.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

class KeyboardTestScreen : public Screen
{
public:
    const char* Id() const override { return "keyboard"; }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        if (ScreenHeader(app, "Keyboard test", true))
            app.Pop();
        float colW = std::min(size.x - 2 * app.Px(40), app.Px(720));
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(win.x + (size.x - colW) / 2 - ring, win.y + app.Px(88) - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::BeginChild("##field", ImVec2(colW + 2 * ring, app.Px(120)), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        std::string shown = Text.empty() ? "(empty)" : Text;
        if (MenuRow(app, "##name", Icon::Keyboard, "Name", shown.c_str(), true))
            app.OpenKeyboard(&Text, "Name");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        if (!app.KeyboardOpen() && Shown != Text)
        {
            // for scripted checks
            printf("ui: text \"%s\"\n", Text.c_str());
            Shown = Text;
        }
        FooterHints(app, {{"A|Enter", "Edit"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
    }

private:
    std::string Text, Shown;
};

}

std::unique_ptr<Screen> MakeKeyboardTestScreen()
{
    return std::make_unique<KeyboardTestScreen>();
}

}
