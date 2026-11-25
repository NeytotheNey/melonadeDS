// A game's own settings, the app's ROM settings screen (RomConfigUi) for what the desktop can apply when the game boots: its display
// name (on the on-screen keyboard), the console it boots as (locked to DSi for a DSiWare title, as the app does), the GBA slot, its own
// layout (or the global one) and the Mods tab's widescreen section (RomModsUi: widescreen, its ratio and the WideMelon camera; the app's
// wide layout pick is left out, as the desktop picks its layouts its own way). They're kept by the ROM's RetroAchievements hash, so every
// copy of the game shares them. The app's other per-game settings belong to later phases and are listed disabled ("Coming later"): the
// microphone (phase 6) and mods (phase 4).
// Changes go to library.db at once (Library::SetConfig) and apply the next time the game starts, from the library or the command line.
// Reached from the details screen, a featured mosaic tile's menu button, or by holding confirm (or the mouse) on a tile
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "Icons.h"
#include "Library.h"
#include "LibraryCards.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"
#include "camera/WidescreenLaunch.h"
#include "layout/LayoutEditing.h"
#include "layout/LayoutGeometry.h"
#include "layout/Layouts.h"

namespace Ui
{

namespace
{

using Frontend::LibraryEntry;
using Frontend::RomConfig;

const RomConfig::Console kConsoles[] = {RomConfig::Console::Default, RomConfig::Console::Ds, RomConfig::Console::Dsi};
const RomConfig::GbaSlot kSlots[] = {RomConfig::GbaSlot::None, RomConfig::GbaSlot::GbaRom, RomConfig::GbaSlot::RumblePak,
    RomConfig::GbaSlot::MemoryExpansion};
// RomModsConfig.WIDE_CAMERA_MAX_WIDTHS: the camera's widest views to pick from
const int kCameraMaxWidths[] = {426, 448, 512, 640, Frontend::MAX_WIDE_VIEW_WIDTH};
// WidescreenAspect.DEFAULT
constexpr const char* kDefaultAspect = "16:9";

// WidescreenAspect.viewWidth: the even 3D view width of a ratio's key, 0 for one it doesn't know
int AspectViewWidth(const std::string& key)
{
    std::optional<float> ratio = Frontend::WidescreenAspectRatio(key);
    return ratio ? Frontend::RoundToInt(*ratio * Frontend::DS_SCREEN_HEIGHT) : 0;
}

// WidescreenAspect.fromViewWidth: the ratio whose width is closest to [viewWidth], as an index into WidescreenAspectKeys
int AspectIndexOf(int viewWidth)
{
    const std::vector<std::string>& keys = Frontend::WidescreenAspectKeys();
    int best = 0;
    for (int i = 1; i < (int)keys.size(); i++)
    {
        if (std::abs(AspectViewWidth(keys[i]) - viewWidth) < std::abs(AspectViewWidth(keys[best]) - viewWidth))
            best = i;
    }
    return best;
}

class GameSettingsScreen : public Screen
{
public:
    explicit GameSettingsScreen(const LibraryEntry& entry) : Entry(entry) {}

    const char* Id() const override { return "library/game-settings"; }

    void Clear(App& app) override
    {
        // X: the display name back to the banner's, the GBA ROM unset
        RomConfig c = Current(app);
        if (FocusedRow == "name" && !c.CustomName.empty())
        {
            c.CustomName.clear();
            Store(app, c);
            app.Toast("The banner's title again");
        }
        else if (FocusedRow == "gbarom" && !c.GbaRomPath.empty())
        {
            c.GbaRomPath.clear();
            Store(app, c);
        }
    }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float footer = FooterHeight(app);
        RomConfig config = Current(app);

        // the display name typed on the keyboard, stored once it closes (blank: the banner's again, as in the app)
        if (EditingName && !app.KeyboardOpen())
        {
            EditingName = false;
            std::string name = NameBuffer;
            while (!name.empty() && name.back() == ' ')
                name.pop_back();
            while (!name.empty() && name.front() == ' ')
                name.erase(name.begin());
            if (name == BannerName())
                name.clear();
            if (name != config.CustomName)
            {
                config.CustomName = name;
                Store(app, config);
            }
        }

        if (ScreenHeader(app, "Game settings", true))
            app.Pop();

        float pad = app.Px(40);
        float colW = std::min(size.x - 2 * pad, app.Px(760));
        float x = win.x + (size.x - colW) / 2;
        // the game, above the rows
        float top = win.y + app.Px(88);
        float art = app.Px(48);
        LibraryEntry shown = Entry;
        shown.Config = config;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        GameArt(app, draw, shown, ImVec2(x, top), ImVec2(x + art, top + art), app.Px(10), 0.85f);
        ImVec2 ns = TextSize(app, app.Medium, 20, "Ag");
        TextAt(app, app.Medium, 20, ImVec2(x + art + app.Px(16), top + art / 2 - ns.y), p.OnSurface,
            Ellipsize(app, app.Medium, 20, shown.Name(), colW - art - app.Px(16)).c_str());
        TextAt(app, app.Regular, 15, ImVec2(x + art + app.Px(16), top + art / 2), p.OnSurfaceVariant,
            Ellipsize(app, app.Regular, 15, Entry.FileName, colW - art - app.Px(16)).c_str());
        top += art + app.Px(16);

        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(x - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(4)));
        ImGui::BeginChild("##gamesettings", ImVec2(colW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        FocusedRow.clear();

        SectionLabel(app, "Game");
        {
            std::string detail = config.CustomName.empty() ? BannerName() + "  (from the banner)" : config.CustomName;
            if (DetailRow(app, "##name", Icon::Edit, "Display name", detail.c_str(), true))
            {
                NameBuffer = config.CustomName.empty() ? BannerName() : config.CustomName;
                EditingName = true;
                app.OpenKeyboard(&NameBuffer, "Display name", 60);
            }
            Track("name");
        }
        {
            // Default: the Console setting's, which it says
            std::string global = host.Settings && host.Settings->GetString("console_type", "ds") == "dsi" ? "DSi" : "DS";
            std::string value = config.ConsoleType == RomConfig::Console::Default ? "Default (" + global + ")" : RomConfig::ConsoleName(config.ConsoleType);
            const char* detail = config.ConsoleType == RomConfig::Console::Dsi || (config.ConsoleType == RomConfig::Console::Default && global == "DSi")
                ? "DSi needs its BIOS, firmware and NAND (Settings > Emulation)" : "Applies when the game starts";
            if (Entry.DsiWare)
            {
                // a DSiWare title only boots as a DSi
                MenuRow(app, "##console", Icon::DeveloperBoard, "Boot system", "DSi (DSiWare)", false, false);
            }
            else if (int change = ChoiceRow(app, "##console", Icon::DeveloperBoard, "Boot system", value.c_str(), false, detail))
            {
                int i = IndexOf(kConsoles, config.ConsoleType);
                config.ConsoleType = kConsoles[(i + change + 3) % 3];
                Store(app, config);
            }
            Track("console");
        }

        SectionLabel(app, "GBA slot");
        {
            if (int change = ChoiceRow(app, "##gbaslot", Icon::SimCard, "GBA slot", RomConfig::GbaSlotName(config.Gba), false,
                    config.Gba == RomConfig::GbaSlot::RumblePak ? "For games that rumble. Applies when the game starts" : "Applies when the game starts"))
            {
                int i = IndexOf(kSlots, config.Gba);
                config.Gba = kSlots[(i + change + 4) % 4];
                Store(app, config);
            }
            Track("gbaslot");
            if (config.Gba == RomConfig::GbaSlot::GbaRom)
            {
                std::string detail = config.GbaRomPath.empty() ? std::string("Not set: a .gba file (its save is next to it)") : config.GbaRomPath;
                if (DetailRow(app, "##gbarom", Icon::FolderOpen, "GBA ROM", detail.c_str(), false))
                {
                    if (host.Hidden || !host.PickGbaRom)
                        app.Toast("The file dialog doesn't open in a hidden run");
                    else if (std::optional<std::string> picked = host.PickGbaRom(config.GbaRomPath))
                    {
                        config.GbaRomPath = *picked;
                        Store(app, config);
                    }
                }
                Track("gbarom");
            }
        }

        SectionLabel(app, "Layout");
        {
            // the app's per-game layout (RomConfig.layoutId), "Use global layout" by default
            std::string detail = "Use global layout";
            if (!config.LayoutId.empty())
            {
                const Frontend::LayoutChoice* c = host.Layouts ? host.Layouts->Find(config.LayoutId) : nullptr;
                detail = c ? c->DisplayName : "A layout that's gone (the global one shows)";
            }
            if (DetailRow(app, "##layout", Icon::Dashboard, "Layout", detail.c_str(), false))
                app.Push(MakeLayoutsScreen(LayoutsFor::Game, &Entry));
            Track("layout");
        }

        DrawWidescreen(app, config);

        SectionLabel(app, "Coming later");
        // the app's other per-game settings, for the phases that build what they need
        MenuRow(app, "##mic", Icon::Mic, "Microphone source", "Coming later", false, false);
        MenuRow(app, "##mods", Icon::Extension, "Mods", "Coming later", false, false);

        ImGui::Dummy(ImVec2(1, app.Px(8)));
        if (MenuRow(app, "##reset", Icon::RestartAlt, "Reset to defaults", config.IsDefault() ? "Nothing changed" : nullptr, false, !config.IsDefault()))
        {
            Store(app, RomConfig());
            app.Toast("Back to the defaults");
            WantFocus = true;
            FocusId = 0;
        }
        Track("reset");
        ImGui::Dummy(ImVec2(1, app.Px(24)));
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        if (FocusedRow == "name" && !config.CustomName.empty())
            FooterHints(app, {{"A|Enter", "Rename"}, {"X|Del", "Banner title"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else if (FocusedRow == "name")
            FooterHints(app, {{"A|Enter", "Rename"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else if (FocusedRow == "gbarom")
            FooterHints(app, {{"A|Enter", "Choose file"}, {"X|Del", "Clear"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else if (FocusedRow == "console" || FocusedRow == "gbaslot" || FocusedRow == "wideaspect" || FocusedRow == "widemax")
            FooterHints(app, {{"A|Enter", "Change"}, {"\xe2\x80\xb9 \xe2\x80\xba", "Previous / next"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "Choose"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
    }

private:
    // RomModsUi's widescreen section, its rows and notes in its order: widescreen and its ratio, then the camera's rows while it's on. The
    // width is kept with the software renderer, for when it's switched to OpenGL or Compute, as the app keeps it
    void DrawWidescreen(App& app, RomConfig config)
    {
        Host& host = app.GetHost();
        SectionLabel(app, "Widescreen");
        if (host.Settings && host.Settings->GetString("video_renderer", "compute") == "software")
            Note(app, "Needs the OpenGL or Compute renderer. With the software renderer the game shows at its normal width.");
        std::optional<int> wideViewWidth = Frontend::ValidWideViewWidth(config.WideViewWidth);
        if (ToggleRow(app, "##wide", Icon::AspectRatio, "Use widescreen", "Widens the 3D view; 2D, menus and touch stay the same. Applies "
                "when the game starts", wideViewWidth.has_value()))
        {
            config.WideViewWidth = wideViewWidth ? std::nullopt : std::optional<int>(AspectViewWidth(kDefaultAspect));
            Store(app, config);
        }
        Track("wide");
        if (!wideViewWidth)
            return;

        const std::vector<std::string>& aspects = Frontend::WidescreenAspectKeys();
        int aspect = AspectIndexOf(*wideViewWidth);
        if (int change = ChoiceRow(app, "##wideaspect", Icon::Straighten, "Aspect ratio", aspects[aspect].c_str()))
        {
            int count = (int)aspects.size();
            config.WideViewWidth = AspectViewWidth(aspects[(aspect + change + count) % count]);
            Store(app, config);
        }
        Track("wideaspect");

        if (ToggleRow(app, "##widecamera", Icon::Joystick, "Camera", "Look around with the right stick, zoom out with the zoom hotkey, or "
                "drag and scroll the top screen. Applies when the game starts", config.WideCamera))
        {
            config.WideCamera = !config.WideCamera;
            Store(app, config);
        }
        Track("widecamera");
        if (!config.WideCamera)
            return;
        DrawCameraRows(app, config, *wideViewWidth);
    }

    void DrawCameraRows(App& app, RomConfig config, int shownWidth)
    {
        // only widths past the shown one zoom out at all; a stored width that isn't one of them shows as it is, with none picked
        std::vector<int> widths;
        for (int w : kCameraMaxWidths)
        {
            if (w > shownWidth)
                widths.push_back(w);
        }
        int maxWidth = Frontend::ValidWideCameraMaxWidth(config.WideCameraMaxWidth);
        int index = (int)(std::find(widths.begin(), widths.end(), maxWidth) - widths.begin());
        std::string value = std::to_string(maxWidth) + " pixels wide";
        if (int change = ChoiceRow(app, "##widemax", Icon::FitScreen, "Zoom out up to", value.c_str()); change && !widths.empty())
        {
            int count = (int)widths.size();
            index = index >= count ? (change > 0 ? 0 : count - 1) : (index + change + count) % count;
            config.WideCameraMaxWidth = widths[index];
            Store(app, config);
        }
        Track("widemax");
        if (maxWidth <= shownWidth)
            Note(app, "That isn't wider than the screen is shown, so the camera stays off. Pick a wider one.");
        if (ToggleRow(app, "##widecenter", Icon::Gamepad, "Return to center when moving", "Eases the view back to the middle while you walk "
                "with the D-pad or left stick", config.WideCameraReturnToCenter))
        {
            config.WideCameraReturnToCenter = !config.WideCameraReturnToCenter;
            Store(app, config);
        }
        Track("widecenter");
        if (ToggleRow(app, "##widetext", Icon::Title, "Auto zoom in on text", "Pok\xc3\xa9mon Platinum, HG/SS and Gen 5 zoom back in for text "
                "boxes; battles and menus always do", config.WideCameraZoomOnText))
        {
            config.WideCameraZoomOnText = !config.WideCameraZoomOnText;
            Store(app, config);
        }
        Track("widetext");
    }

    // a line of text under the rows, as the settings' notes
    static void Note(App& app, const char* text)
    {
        float w = ImGui::GetContentRegionAvail().x - app.Px(40);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1, app.Px(6)));
        ImVec2 s = TextAt(app, app.Regular, 14, ImVec2(pos.x + app.Px(20), pos.y + app.Px(6)), app.Colors().OnSurfaceVariant, text, w);
        ImGui::Dummy(ImVec2(1, s.y));
    }

    template <typename T, size_t N> static int IndexOf(const T (&values)[N], T value)
    {
        for (size_t i = 0; i < N; i++)
        {
            if (values[i] == value)
                return (int)i;
        }
        return 0;
    }

    RomConfig Current(App& app) const
    {
        Frontend::Library* lib = app.GetHost().Library;
        return lib ? lib->GetConfig(Frontend::Library::ConfigKey(Entry)) : Entry.Config;
    }

    void Store(App& app, const RomConfig& config)
    {
        Frontend::Library* lib = app.GetHost().Library;
        if (lib && !lib->SetConfig(Frontend::Library::ConfigKey(Entry), config))
            app.Toast("Couldn't save it in the library; it holds until the app closes");
    }

    // the name without a display name of its own: the banner's title, or the file name
    std::string BannerName() const
    {
        LibraryEntry e = Entry;
        e.Config = RomConfig();
        return e.Name();
    }

    void Track(const char* row)
    {
        if (ImGui::IsItemFocused())
            FocusedRow = row;
    }

    LibraryEntry Entry;
    std::string FocusedRow;
    std::string NameBuffer;
    bool EditingName = false;
};

}

std::unique_ptr<Screen> MakeGameSettingsScreen(const LibraryEntry& entry)
{
    return std::make_unique<GameSettingsScreen>(entry);
}

}
