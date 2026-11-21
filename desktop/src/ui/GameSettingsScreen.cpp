// A game's own settings, the app's ROM settings screen (RomConfigUi) for what the desktop can apply when the game boots: its display
// name (on the on-screen keyboard), the console it boots as (locked to DSi for a DSiWare title, as the app does) and the GBA slot. They're
// kept by the ROM's RetroAchievements hash, so every copy of the game shares them. The app's other per-game settings belong to later phases
// and are listed disabled ("Coming later"): the microphone (phase 6), the controller layout and WideMelon (phase 3) and mods (phase 4).
// Changes go to library.db at once (Library::SetConfig) and apply the next time the game starts, from the library or the command line.
// Reached from the details screen, a featured mosaic tile's menu button, or by holding confirm (or the mouse) on a tile
#include <algorithm>
#include <string>

#include "Icons.h"
#include "Library.h"
#include "LibraryCards.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

using Frontend::LibraryEntry;
using Frontend::RomConfig;

const RomConfig::Console kConsoles[] = {RomConfig::Console::Default, RomConfig::Console::Ds, RomConfig::Console::Dsi};
const RomConfig::GbaSlot kSlots[] = {RomConfig::GbaSlot::None, RomConfig::GbaSlot::GbaRom, RomConfig::GbaSlot::RumblePak,
    RomConfig::GbaSlot::MemoryExpansion};

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

        SectionLabel(app, "Coming later");
        // the app's other per-game settings, for the phases that build what they need
        MenuRow(app, "##mic", Icon::Mic, "Microphone source", "Coming later", false, false);
        MenuRow(app, "##layout", Icon::Dashboard, "Controller layout", "Coming later", false, false);
        MenuRow(app, "##wide", Icon::AspectRatio, "Widescreen (WideMelon)", "Coming later", false, false);
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
        else if (FocusedRow == "console" || FocusedRow == "gbaslot")
            FooterHints(app, {{"A|Enter", "Change"}, {"\xe2\x80\xb9 \xe2\x80\xba", "Previous / next"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "Choose"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
    }

private:
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
