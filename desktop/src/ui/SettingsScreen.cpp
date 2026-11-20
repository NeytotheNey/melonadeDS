// Settings, for now the few that prove the settings path through the menus: renderer, internal resolution, vsync and the theme. Each change
// is saved to settings.json and applied right away, to a running game too. The full settings screens replace this one
#include <algorithm>
#include <string>

#include "Icons.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

const char* const kRenderers[] = {"software", "opengl", "compute"};
const char* const kRendererNames[] = {"Software", "OpenGL", "Compute"};
const char* const kThemes[] = {"system", "light", "dark"};
const char* const kThemeNames[] = {"System", "Light", "Dark"};

int IndexOf(const char* const* values, int count, const std::string& value, int fallback)
{
    for (int i = 0; i < count; i++)
    {
        if (value == values[i])
            return i;
    }
    return fallback;
}

class SettingsScreen : public Screen
{
public:
    const char* Id() const override { return "settings"; }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        Frontend::Settings& settings = *host.Settings;
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float pad = app.Px(40), footer = FooterHeight(app);

        if (ScreenHeader(app, "Settings", true))
            app.Pop();

        // one column, at most 720 wide, centered on a wide window
        float colW = std::min(size.x - 2 * pad, app.Px(720));
        float x = win.x + (size.x - colW) / 2;
        float top = win.y + app.Px(88);
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(x - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(4)));
        ImGui::BeginChild("##settings", ImVec2(colW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);

        SectionLabel(app, "Video");
        int renderer = IndexOf(kRenderers, 3, settings.GetString("video_renderer", "compute"), 2);
        if (int change = ChoiceRow(app, "##renderer", Icon::Memory, "Renderer", kRendererNames[renderer], true))
        {
            renderer = (renderer + change + 3) % 3;
            host.SetRenderer(kRenderers[renderer]);
        }
        int scale = std::clamp(settings.GetInt("video_internal_resolution", 4), 1, 16);
        char scaleText[16];
        snprintf(scaleText, sizeof(scaleText), "%dx", scale);
        if (int change = ChoiceRow(app, "##scale", Icon::HighQuality, "Internal resolution", scaleText))
        {
            // 1x to 16x, wrapping around
            scale = (scale - 1 + change + 16) % 16 + 1;
            host.SetScale(scale);
        }
        bool vsync = settings.GetBool("desktop_vsync", false);
        if (ToggleRow(app, "##vsync", Icon::Sync, "VSync", "The main window waits for the monitor", vsync))
            host.SetVSync(!vsync);

        SectionLabel(app, "Look");
        int theme = IndexOf(kThemes, 3, settings.GetString("desktop_theme", "system"), 0);
        if (int change = ChoiceRow(app, "##theme", theme == 1 ? Icon::LightMode : theme == 2 ? Icon::DarkMode : Icon::BrightnessAuto,
                "Theme", kThemeNames[theme]))
        {
            theme = (theme + change + 3) % 3;
            settings.Set("desktop_theme", picojson::value(std::string(kThemes[theme])));
            app.ApplyTheme();
            printf("theme: %s\n", kThemes[theme]);
        }

        ImGui::Dummy(ImVec2(1, app.Px(12)));
        TextAt(app, app.Regular, 15, ImGui::GetCursorScreenPos(), p.OnSurfaceVariant,
            "More settings (audio, input, BIOS, the renderer's options) are coming in the next update.", colW);
        ImGui::Dummy(ImVec2(1, app.Px(40)));

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        FooterHints(app, {{"A|Enter", "Next value"}, {"\xe2\x80\xb9 \xe2\x80\xba", "Previous / next"}, {"B|Esc", "Back"}},
            p.SurfaceContainerLowest);
    }
};

}

std::unique_ptr<Screen> MakeSettingsScreen()
{
    return std::make_unique<SettingsScreen>();
}

}
