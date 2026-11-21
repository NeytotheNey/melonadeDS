// A game's details, from the library: its art, title and developer, the file, game code, region, size, when it was last played and its
// RetroAchievements hash, with Play focused. Play boots it as Open ROM does (a zip or 7z unpacked first, which the button says). Back
// goes back to the library, with the focus on the game's tile again. The press that opened the screen can't press Play: Play only takes
// a press that started here (the app's CarriedOverKeyFilter)
#include <algorithm>
#include <cmath>

#include "Icons.h"
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

class GameDetailsScreen : public Screen
{
public:
    explicit GameDetailsScreen(const LibraryEntry& entry) : Entry(entry) {}

    const char* Id() const override { return "library/details"; }

    bool Back(App&) override
    {
        // not while it's starting
        return Starting;
    }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float pad = app.Px(40), footer = FooterHeight(app);

        // a confirm still held from the library, or a mouse button: Play waits for it to be let go
        bool held = ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) || ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_KeypadEnter)
            || ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool armedBefore = Armed;
        if (!held)
            Armed = true;

        if (ScreenHeader(app, "Game details", true) && !Starting)
            app.Pop();

        // the art on the left, the facts on the right
        float top = win.y + app.Px(88) + app.Px(8);
        float artW = std::clamp(std::floor(size.x * 0.26f), app.Px(150), app.Px(300));
        float artH = std::floor(artW * 10 / 9);
        ImVec2 a0(win.x + pad, top), a1(a0.x + artW, a0.y + artH);
        GameArt(app, draw, Entry, a0, a1, app.Px(20), 0.62f);

        float x = a1.x + app.Px(40);
        float width = std::min(win.x + size.x - pad - x, app.Px(720));
        float y = top;
        for (const std::string& line : WrapLines(app, app.Medium, 30, Entry.Name(), width, 2))
        {
            ImVec2 s = TextAt(app, app.Medium, 30, ImVec2(x, y), p.OnSurface, line.c_str());
            y += s.y;
        }
        y += app.Px(4);
        if (!Entry.HasMetadata())
            y += ItalicTextAt(app, app.Regular, 18, ImVec2(x, y), p.OnSurfaceVariant, "No metadata").y;
        else if (!Entry.Developer.empty())
            y += TextAt(app, app.Regular, 18, ImVec2(x, y), p.OnSurfaceVariant, Ellipsize(app, app.Regular, 18, Entry.Developer, width).c_str()).y;
        y += app.Px(20);

        // the facts, two to a row
        struct Fact { ImWchar Icon; const char* Label; std::string Value; bool Wide = false; };
        std::vector<Fact> facts;
        std::string file = Entry.FileName;
        if (!Entry.Entry.empty())
            file += "  \xe2\x80\xba  " + Frontend::PathToUtf8(Frontend::PathFromUtf8(Entry.Entry).filename());
        facts.push_back({Entry.IsArchive() ? Icon::FolderZip : Icon::Description, "File", file});
        facts.push_back({Icon::Folder, "Folder", Frontend::PathToUtf8(Frontend::PathFromUtf8(Entry.Path).parent_path())});
        if (Entry.State == LibraryEntry::kRead)
        {
            facts.push_back({Icon::Tag, "Game code", Entry.GameCode.empty() ? "None" : Entry.GameCode + (Entry.DsiWare ? " (DSiWare)" : "")});
            facts.push_back({Icon::Public, "Region", Entry.Region.empty() ? "Unknown" : Entry.Region});
            std::string sizeText = SizeText(Entry.RomSize);
            if (Entry.IsArchive())
                sizeText += " (" + SizeText(Entry.FileSize) + " packed)";
            facts.push_back({Icon::HardDrive, "Size", sizeText});
        }
        else
            facts.push_back({Icon::HardDrive, "Size", SizeText(Entry.FileSize)});
        facts.push_back({Icon::Schedule, "Last played", PlayedDate(Entry.LastPlayed)});
        if (Entry.State == LibraryEntry::kRead)
            facts.push_back({Icon::Trophy, "RetroAchievements hash", Entry.RaHash.empty() ? "None (rc_hash refused the ROM)" : Entry.RaHash, true});
        float colW = std::floor((width - app.Px(24)) / 2);
        bool twoColumns = colW >= app.Px(200);
        if (!twoColumns)
            colW = width;
        float factH = app.Px(58);
        int col = 0;
        for (size_t i = 0; i < facts.size(); i++)
        {
            // the hash on a row of its own, as it's long
            bool wide = facts[i].Wide;
            if (wide && col == 1)
            {
                y += factH;
                col = 0;
            }
            float fx = x + col * (colW + app.Px(24));
            float fw = wide || !twoColumns ? width : colW;
            IconAt(app, facts[i].Icon, 22, ImVec2(fx + app.Px(11), y + app.Px(18)), p.OnSurfaceVariant);
            TextAt(app, app.Medium, 13, ImVec2(fx + app.Px(34), y), p.OnSurfaceVariant, facts[i].Label);
            TextAt(app, app.Regular, 17, ImVec2(fx + app.Px(34), y + app.Px(20)), p.OnSurface,
                Ellipsize(app, app.Regular, 17, facts[i].Value, fw - app.Px(34)).c_str());
            if (!twoColumns || col == 1 || wide || i + 1 == facts.size())
            {
                y += factH;
                col = 0;
            }
            else
                col = 1;
        }
        if (Entry.State == LibraryEntry::kFailed)
        {
            ImVec2 s = TextAt(app, app.Regular, 15, ImVec2(x, y), p.OnSurfaceVariant,
                "The library couldn't read this file (a damaged archive, not enough memory, or it was in use). The next rescan tries again.",
                width);
            y += s.y + app.Px(8);
        }
        if (Entry.State == LibraryEntry::kNotRead)
        {
            ImVec2 s = TextAt(app, app.Regular, 15, ImVec2(x, y), p.OnSurfaceVariant,
                "This file is online-only (OneDrive or another cloud folder), so the library hasn't read it. Play downloads it; the next "
                "rescan reads its details.", width);
            y += s.y + app.Px(8);
        }
        y += app.Px(12);

        // Play, under the facts (or the art, whichever is lower on a short window)
        Frontend::Library::Progress progress = host.Library ? host.Library->GetProgress() : Frontend::Library::Progress();
        bool unpacking = Starting && progress.Preparing;
        const char* label = unpacking ? "Unpacking\xe2\x80\xa6" : Starting ? "Starting\xe2\x80\xa6" : "Play";
        const char* detail = Entry.IsArchive() && !Starting ? "Unpacked first, then kept for the next few games" : nullptr;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        bool pressed = BigButton(app, "##play", Icon::PlayArrow, label, detail, true, std::min(width, app.Px(360)), true);
        // a press counts once nothing was held when the screen opened
        if (pressed && armedBefore && !Starting && host.BootRom)
        {
            printf("ui: play %s\n", Entry.Path.c_str());
            fflush(stdout);
            // (refused while another game is starting)
            Starting = host.BootRom(Entry.Path);
        }
        else if (pressed && !armedBefore)
            printf("ui: play ignored (the press began on the library)\n");

        FooterHints(app, {{"A|Enter", "Play"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
    }

private:
    LibraryEntry Entry;
    bool Armed = false;
    bool Starting = false;
};

}

std::unique_ptr<Screen> MakeGameDetailsScreen(const LibraryEntry& entry)
{
    return std::make_unique<GameDetailsScreen>(entry);
}

}
