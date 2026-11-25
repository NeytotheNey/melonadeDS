// The game library, the home screen when no game runs (phase 2 of docs/plan-desktop-app.md, the app's launcher in docs/plan-launcher.md
// section 1): the games in the ROM folders as a List, a Grid or a Mosaic (rom_list_view_mode, with the app's values), sorted as the app
// sorts them (rom_sorting_mode and rom_sorting_order), with a filter (All, Played, Not played; not saved, as in the app) and a search
// through the on-screen keyboard. The controls sit in a row above the games, which the focus reaches by going up from the first row; L / R
// cycle the view. A game opens its details (with Play) when the press is let go, and holding it (the mouse too) opens its settings, as a
// long press does in the app. The menu (also Start / Guide) has Open ROM, the folders, a rescan, Settings and Quit. The game the focus was
// on is kept in desktop_library_focus (by path), so the next run starts on it. The views are virtualized (ImGuiListClipper; the mosaic
// clips by its rows), and the icons' textures are made as they're first drawn
#include <algorithm>
#include <cmath>
#include <optional>

#include "imgui_internal.h"
#include "Icons.h"
#include "LibraryCards.h"
#include "MetadataSync.h"
#include "MosaicLayout.h"
#include "Screens.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

using Frontend::LibraryEntry;

struct SortOption { const char* Mode; const char* Order; const char* Name; };
// the app's SortingMode and SortingOrder (alphabetically ascending by default, recently played descending)
const SortOption kSorts[] = {
    {"alphabetically", "ascending", "Title A\xe2\x80\x93Z"},
    {"alphabetically", "descending", "Title Z\xe2\x80\x93" "A"},
    {"recently_played", "descending", "Recently played"},
    {"recently_played", "ascending", "Least recently played"},
};
constexpr int kSortCount = 4;
const char* const kFilters[] = {"All", "Played", "Not played"};
// rom_list_view_mode's values, the app's (its RomListViewMode, lowercased)
const char* const kViews[] = {"list", "grid", "mosaic"};
const char* const kViewNames[] = {"List", "Grid", "Mosaic"};
constexpr int kViewCount = 3;
// how long confirm (or the mouse) is held on a tile for the game's settings instead of its details
constexpr double kHoldSeconds = 0.5;

bool KeyPressed(ImGuiKey a, ImGuiKey b, ImGuiKey c)
{
    return ImGui::IsKeyPressed(a, true) || ImGui::IsKeyPressed(b, true) || ImGui::IsKeyPressed(c, true);
}
bool UpPressed() { return KeyPressed(ImGuiKey_GamepadDpadUp, ImGuiKey_UpArrow, ImGuiKey_GamepadLStickUp); }
bool DownPressed() { return KeyPressed(ImGuiKey_GamepadDpadDown, ImGuiKey_DownArrow, ImGuiKey_GamepadLStickDown); }
bool LeftPressed() { return KeyPressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow, ImGuiKey_GamepadLStickLeft); }
bool RightPressed() { return KeyPressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow, ImGuiKey_GamepadLStickRight); }

std::string LowerAscii(std::string text)
{
    for (char& c : text)
        c = (char)tolower((unsigned char)c);
    return text;
}

// a pill in the controls row: outlined, or filled when [selected] (M3's filter chip)
bool Chip(App& app, const char* id, ImWchar icon, const std::string& label, bool selected, float maxWidth = 0)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = app.Px(44);
    float chrome = app.Px(14 + 24 + 8 + 18);
    std::string text = maxWidth > 0 && !label.empty() ? Ellipsize(app, app.Medium, 16, label, maxWidth - chrome) : label;
    ImVec2 ts = TextSize(app, app.Medium, 16, text.c_str());
    // (an icon alone on a narrow window)
    float w = text.empty() ? app.Px(56) : chrome + ts.x;
    bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    app.TrackFocus(false);
    bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + w, pos.y + h);
    float r = app.Px(12);
    ImU32 base = selected || focused ? p.SecondaryContainer : p.Surface;
    ImU32 on = selected || focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
    ImU32 bg = held ? Blend(base, on, 0.12f) : hovered ? Blend(base, on, 0.08f) : base;
    draw->AddRectFilled(pos, max, bg, r);
    if (!selected && !focused)
        draw->AddRect(pos, max, p.Outline, r, std::max(1.f, app.Px(1.5f)));
    if (focused)
        FocusRing(app, draw, pos, max, r);
    if (text.empty())
    {
        IconAt(app, icon, 22, ImVec2(pos.x + w / 2, pos.y + h / 2), on);
        return pressed;
    }
    IconAt(app, icon, 22, ImVec2(pos.x + app.Px(14 + 12), pos.y + h / 2), on);
    TextAt(app, app.Medium, 16, ImVec2(pos.x + app.Px(14 + 24 + 8), pos.y + (h - ts.y) / 2), on, text.c_str());
    return pressed;
}

// List | Grid | Mosaic: a segment each; returns the one chosen, or -1
int Segmented(App& app, const char* const* labels, const ImWchar* icons, int count, int selected)
{
    const Palette& p = app.Colors();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    float h = app.Px(44), r = h / 2;
    float x = start.x;
    int chosen = -1;
    std::vector<float> edges {x};
    for (int i = 0; i < count; i++)
    {
        bool iconOnly = !labels[i][0];
        ImVec2 ts = TextSize(app, app.Medium, 16, labels[i]);
        float w = iconOnly ? app.Px(60) : app.Px(18 + 22 + 8 + 20) + ts.x;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##segment", ImVec2(w, h), ImGuiButtonFlags_EnableNav))
            chosen = i;
        app.TrackFocus(false);
        ImGui::PopID();
        bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered();
        bool on = i == selected;
        ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundLeft : i == count - 1 ? ImDrawFlags_RoundRight : ImDrawFlags_RoundNone;
        ImU32 base = on ? p.SecondaryContainer : p.Surface;
        ImU32 fg = on ? p.OnSecondaryContainer : p.OnSurface;
        draw->AddRectFilled(ImVec2(x, start.y), ImVec2(x + w, start.y + h), hovered || focused ? Blend(base, fg, 0.08f) : base, r, corners);
        if (iconOnly)
            IconAt(app, icons[i], 22, ImVec2(x + w / 2, start.y + h / 2), fg);
        else
        {
            IconAt(app, on ? Icon::Check : icons[i], 20, ImVec2(x + app.Px(18 + 11), start.y + h / 2), fg);
            TextAt(app, app.Medium, 16, ImVec2(x + app.Px(18 + 22 + 8), start.y + (h - ts.y) / 2), fg, labels[i]);
        }
        if (focused)
            FocusRing(app, draw, ImVec2(x, start.y), ImVec2(x + w, start.y + h), i == 0 || i == count - 1 ? r : 0);
        x += w;
        edges.push_back(x);
    }
    // the outline over the segments, with the lines between them
    ImU32 line = p.Outline;
    float t = std::max(1.f, app.Px(1.5f));
    draw->AddRect(start, ImVec2(x, start.y + h), line, r, t);
    for (size_t i = 1; i + 1 < edges.size(); i++)
        draw->AddLine(ImVec2(edges[i], start.y), ImVec2(edges[i], start.y + h), line, t);
    ImGui::SetCursorScreenPos(ImVec2(x, start.y));
    return chosen;
}

// a round icon button
bool IconButton(App& app, const char* id, ImWchar icon, float d)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(d, d), ImGuiButtonFlags_EnableNav);
    app.TrackFocus(false);
    bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + d / 2, pos.y + d / 2);
    if (focused)
        draw->AddCircleFilled(c, d / 2, p.SecondaryContainer);
    else if (hovered)
        draw->AddCircleFilled(c, d / 2, WithAlpha(p.OnSurface, 0.08f));
    if (focused)
        draw->AddCircle(c, d / 2 + app.Px(4), p.Primary, 0, std::max(2.f, IM_ROUND(app.Px(3))));
    IconAt(app, icon, 26, c, focused ? p.OnSecondaryContainer : p.OnSurfaceVariant);
    return pressed;
}

// a small tag under a grid tile
float MetaChip(App& app, ImDrawList* draw, ImVec2 pos, const std::string& text, float maxWidth, ImU32 bg, ImU32 fg)
{
    std::string fitted = Ellipsize(app, app.Regular, 13, text, maxWidth - app.Px(12));
    ImVec2 s = TextSize(app, app.Regular, 13, fitted.c_str());
    float w = s.x + app.Px(12), h = app.Px(22);
    draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, app.Px(5));
    TextAt(app, app.Regular, 13, ImVec2(pos.x + app.Px(6), pos.y + (h - s.y) / 2), fg, fitted.c_str());
    return w;
}

class LibraryScreen : public Screen
{
public:
    explicit LibraryScreen(LibraryOpen open) : OpenFirst(open) {}

    const char* Id() const override { return "library"; }

    bool Back(App&) override
    {
        if (MenuOpen)
            CloseMenu();
        // the root: nothing to go back to
        return true;
    }

    void Tab(App& app, int direction) override
    {
        if (MenuOpen)
            return;
        SetView(app, (View(app) + direction + kViewCount) % kViewCount);
    }

    void MenuButton(App&) override
    {
        // Start / Guide: the menu, or closed again
        if (MenuOpen)
            CloseMenu();
        else
            OpenMenu();
    }

    void Clear(App& app) override
    {
        // X: the search cleared
        if (!MenuOpen && !Query.empty())
        {
            Query.clear();
            app.Toast("Search cleared");
        }
    }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        Frontend::Library* lib = host.Library;
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float pad = app.Px(40), footer = FooterHeight(app);
        if (lib)
            lib->EnsureScanned();
        if (!SavedFocusRead && host.Settings)
        {
            // the game focused last time (its path), until another one is
            SavedFocus = host.Settings->GetString("desktop_library_focus", "");
            SavedFocusRead = true;
        }
        Frontend::Library::Progress progress = lib ? lib->GetProgress() : Frontend::Library::Progress();
        Rebuild(app);
        if (!SavedFocusSettled)
        {
            // The game focused last time, once the list has it: the focus goes there (a first scan into an empty library.db publishes
            // the games bit by bit). Until it's found, or the scan is done without it, the focus moving doesn't replace it
            bool found = std::any_of(Visible.begin(), Visible.end(), [&](const LibraryEntry* e) { return _stricmp(e->Path.c_str(), SavedFocus.c_str()) == 0; });
            if (found && FocusedRom && !MenuOpen)
            {
                FocusedRom = 0;
                if (lib)
                    lib->LastFocused = 0;
                WantFocus = true;
                FocusId = 0;
            }
            if (SavedFocus.empty() || found || (progress.Scanned && lib && !lib->Busy()))
                SavedFocusSettled = true;
        }
        size_t total = Source ? Source->size() : 0;
        std::vector<std::string> folders = host.Settings ? host.Settings->GetStringList("rom_search_dirs") : std::vector<std::string>();

        // what the content area shows changed (the games after the first scan, or an empty library's buttons once it's done): its default
        // item takes the focus, which the controls above had meanwhile. Not for a search or filter with nothing in it, or back from one:
        // the focus stays on the control just used
        int shape = !Visible.empty() ? 0 : progress.Scanning ? 1 : total > 0 ? 2 : folders.empty() ? 3 : 4;
        if (shape != Shape && Shape != -1 && shape != 1 && shape != 2 && Shape != 2 && !MenuOpen)
        {
            WantFocus = true;
            FocusId = 0;
        }
        Shape = shape;

        // --open library/details, library/settings or library/settings/mods: the first game's, once the scan is done
        if (OpenFirst != LibraryOpen::None && lib && !lib->Busy() && progress.Scanned)
        {
            LibraryOpen open = OpenFirst;
            OpenFirst = LibraryOpen::None;
            if (!Visible.empty())
            {
                FocusedRom = Visible[0]->Id;
                if (open == LibraryOpen::Details)
                    app.Push(MakeGameDetailsScreen(*Visible[0]));
                else
                    app.Push(MakeGameSettingsScreen(*Visible[0], open == LibraryOpen::SettingsMods));
            }
        }

        bool menu = MenuOpen;
        if (menu && MenuFocus)
        {
            MenuFocus = false;
            WantFocus = true;
            FocusId = 0;
        }
        if (menu)
            ImGui::BeginDisabled();

        // the app bar: the name, and what the library is doing on the right
        float mark = app.Px(44);
        ImVec2 m0(win.x + pad, win.y + app.Px(22));
        draw->AddRectFilled(m0, ImVec2(m0.x + mark, m0.y + mark), p.PrimaryContainer, app.Px(12));
        IconAt(app, Icon::VideogameAsset, 28, ImVec2(m0.x + mark / 2, m0.y + mark / 2), p.OnPrimaryContainer);
        ImVec2 name = TextSize(app, app.Medium, 28, "melonade");
        TextAt(app, app.Medium, 28, ImVec2(m0.x + mark + app.Px(14), m0.y + (mark - name.y) / 2), p.OnSurface, "melonade");
        TextAt(app, app.Medium, 28, ImVec2(m0.x + mark + app.Px(14) + name.x, m0.y + (mark - name.y) / 2), p.Primary, "DS");
        std::string status;
        if (progress.Preparing)
            status = "Unpacking " + progress.PreparingName + "\xe2\x80\xa6";
        else if (progress.Scanning && progress.Found > 0)
            status = "Scanning " + std::to_string(progress.Done) + " of " + std::to_string(progress.Found);
        else if (progress.Scanning)
            status = "Looking for games\xe2\x80\xa6";
        else if (total > 0)
            status = std::to_string(total) + (total == 1 ? " game" : " games");
        // the metadata sync at work (Settings > Library says what it's doing)
        bool syncing = !progress.Scanning && !progress.Preparing && total > 0 && host.Metadata && host.Metadata->GetStatus().Running;
        if (syncing)
            status += "  \xc2\xb7  Getting details and art\xe2\x80\xa6";
        if (!status.empty())
        {
            float room = win.x + size.x - pad - (m0.x + mark + app.Px(200));
            std::string fitted = Ellipsize(app, app.Regular, 16, status, std::max(room, app.Px(60)));
            ImVec2 s = TextSize(app, app.Regular, 16, fitted.c_str());
            float sx = win.x + size.x - pad - s.x;
            TextAt(app, app.Regular, 16, ImVec2(sx, m0.y + (mark - s.y) / 2), p.OnSurfaceVariant, fitted.c_str());
            if (progress.Scanning || progress.Preparing || syncing)
                IconAt(app, Icon::Sync, 20, ImVec2(sx - app.Px(16), m0.y + mark / 2), p.Primary);
        }

        // the controls: view, sort, filter, search, and the menu on the right
        float rowY = win.y + app.Px(88);
        ImGui::SetCursorScreenPos(ImVec2(win.x + pad, rowY));
        // a narrow window: the controls as icons
        bool compact = size.x < app.Px(980);
        const char* views[] = {compact ? "" : kViewNames[0], compact ? "" : kViewNames[1], compact ? "" : kViewNames[2]};
        const ImWchar viewIcons[] = {Icon::ViewList, Icon::GridView, Icon::Mosaic};
        int current = View(app);
        int view = Segmented(app, views, viewIcons, kViewCount, current);
        if (view >= 0)
            SetView(app, view);
        float gap = app.Px(12);
        float menuD = app.Px(44);
        float right = win.x + size.x - pad - menuD - gap;
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + gap, rowY));
        int sort = SortIndex(app);
        if (Chip(app, "##sort", Icon::Sort, compact ? std::string() : std::string("Sort: ") + kSorts[sort].Name, false, std::max(app.Px(120), right - ImGui::GetCursorScreenPos().x)))
            SetSort(app, (sort + 1) % kSortCount);
        ImGui::SameLine(0, gap);
        if (Chip(app, "##filter", Icon::FilterList, compact ? std::string() : Filter ? std::string(kFilters[Filter]) : std::string("Filter"), Filter != 0,
                std::max(app.Px(110), right - ImGui::GetCursorScreenPos().x)))
            Filter = (Filter + 1) % 3;
        ImGui::SameLine(0, gap);
        bool searchFocused = false;
        if (Chip(app, "##search", Icon::Search, compact ? std::string() : Query.empty() ? std::string("Search") : "\xe2\x80\x9c" + Query + "\xe2\x80\x9d", !Query.empty(),
                std::max(app.Px(110), right - ImGui::GetCursorScreenPos().x)))
            app.OpenKeyboard(&Query, "Search the library", 40);
        searchFocused = ImGui::IsItemFocused();
        ImGui::SetCursorScreenPos(ImVec2(win.x + size.x - pad - menuD, rowY));
        if (IconButton(app, "##menu", Icon::MoreVert, menuD))
            OpenMenu();
        MenuButtonId = ImGui::GetItemID();

        // the scan's progress under the controls
        float top = rowY + app.Px(44) + app.Px(22);
        if (progress.Scanning)
        {
            float y = top - app.Px(10), x0 = win.x + pad, x1 = win.x + size.x - pad;
            float t = std::max(2.f, app.Px(3));
            draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + t), p.SurfaceContainerHighest, t);
            if (progress.Found > 0)
                draw->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + (x1 - x0) * std::min(1.f, (float)progress.Done / progress.Found), y + t), p.Primary, t);
        }

        float contentH = win.y + size.y - footer - top;
        bool gamesFocused = false;
        FocusedPart = Part::Tile;
        if (Visible.empty())
            DrawEmpty(app, progress, folders, total, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH));
        else if (current == 2)
            gamesFocused = DrawMosaic(app, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH));
        else if (current == 1)
            gamesFocused = DrawGrid(app, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH));
        else
            gamesFocused = DrawList(app, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH));
        if (menu)
            ImGui::EndDisabled();
        if (menu)
            DrawMenu(app, rowY + app.Px(52));

        if (!gamesFocused)
            LoggedRom = 0;
        if (menu)
            FooterHints(app, {{"A|Enter", "Choose"}, {"B|Esc", "Close"}}, p.SurfaceContainerLowest);
        else if (searchFocused && !Query.empty())
            FooterHints(app, {{"A|Enter", "Edit search"}, {"X|Del", "Clear search"}, {"L/R|Q/W", "View"}}, p.SurfaceContainerLowest);
        else if (gamesFocused && FocusedPart == Part::Continue)
            FooterHints(app, {{"A|Enter", "Continue"}, {"L/R|Q/W", "View"}}, p.SurfaceContainerLowest);
        else if (gamesFocused && FocusedPart == Part::Menu)
            FooterHints(app, {{"A|Enter", "Game settings"}, {"L/R|Q/W", "View"}}, p.SurfaceContainerLowest);
        else if (gamesFocused)
            FooterHints(app, {{"A|Enter", "Details"}, {"Hold A|Hold Enter", "Game settings"}, {"L/R|Q/W", "View"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "Select"}, {"L/R|Q/W", "View"}}, p.SurfaceContainerLowest);
    }

private:
    // 0 list, 1 grid, 2 mosaic (the desktop's default is the grid; the app's, the list)
    int View(App& app) const
    {
        Host& host = app.GetHost();
        std::string mode = host.Settings ? host.Settings->GetString("rom_list_view_mode", "grid") : "grid";
        for (int i = 0; i < kViewCount; i++)
        {
            if (mode == kViews[i])
                return i;
        }
        return 1;
    }

    void SetView(App& app, int view)
    {
        Host& host = app.GetHost();
        if (view == View(app))
            return;
        if (host.ChangeSetting)
            host.ChangeSetting("rom_list_view_mode", picojson::value(std::string(kViews[view])));
        // the focus stays on the same game, which a new layout puts somewhere else
        // (FocusId is kept: a view chosen on the control keeps the focus there, while a game's tile is in no other view, so a view
        // chosen with L / R from a game gives the focus to the same game in the new one)
        WantFocus = true;
        PendingFocus = 0;
        InsideTile = 0;
        printf("ui: library %s\n", kViews[view]);
        fflush(stdout);
    }

    int SortIndex(App& app) const
    {
        Host& host = app.GetHost();
        if (!host.Settings)
            return 0;
        std::string mode = host.Settings->GetString("rom_sorting_mode", "alphabetically");
        std::string order = host.Settings->GetString("rom_sorting_order", mode == "recently_played" ? "descending" : "ascending");
        for (int i = 0; i < kSortCount; i++)
        {
            if (mode == kSorts[i].Mode && order == kSorts[i].Order)
                return i;
        }
        return 0;
    }

    void SetSort(App& app, int index)
    {
        Host& host = app.GetHost();
        if (!host.ChangeSetting)
            return;
        host.ChangeSetting("rom_sorting_mode", picojson::value(std::string(kSorts[index].Mode)));
        host.ChangeSetting("rom_sorting_order", picojson::value(std::string(kSorts[index].Order)));
    }

    // the games shown: filtered, searched and sorted again when one of those or the library changed
    void Rebuild(App& app)
    {
        Frontend::Library* lib = app.GetHost().Library;
        std::shared_ptr<const std::vector<LibraryEntry>> entries = lib ? lib->Entries() : nullptr;
        int sort = SortIndex(app);
        if (entries == Source && sort == BuiltSort && Filter == BuiltFilter && Query == BuiltQuery)
            return;
        Source = entries;
        BuiltSort = sort;
        BuiltFilter = Filter;
        BuiltQuery = Query;
        Visible.clear();
        if (!Source)
            return;
        // the app searches the title and the file name
        std::string q = LowerAscii(Query);
        struct Keyed { std::string Name; const LibraryEntry* E; };
        std::vector<Keyed> keyed;
        keyed.reserve(Source->size());
        for (const LibraryEntry& e : *Source)
        {
            if ((Filter == 1 && !e.LastPlayed) || (Filter == 2 && e.LastPlayed))
                continue;
            std::string name = LowerAscii(e.Name());
            if (!q.empty() && name.find(q) == std::string::npos && LowerAscii(e.FileName).find(q) == std::string::npos)
                continue;
            keyed.push_back({std::move(name), &e});
        }
        bool byName = sort <= 1, ascending = std::string(kSorts[sort].Order) == "ascending";
        std::stable_sort(keyed.begin(), keyed.end(), [&](const Keyed& a, const Keyed& b)
        {
            auto byTitle = [&] { return a.Name != b.Name ? a.Name < b.Name : a.E->FileName < b.E->FileName; };
            if (byName)
                return ascending ? byTitle() : (a.Name != b.Name ? a.Name > b.Name : a.E->FileName > b.E->FileName);
            int64_t x = a.E->LastPlayed, y = b.E->LastPlayed;
            // as the app: never played after the played ones (newest first), or before them (oldest first), by title among themselves
            if (!x && !y)
                return byTitle();
            if (!x || !y)
                return ascending ? !x : !y;
            return ascending ? x < y : x > y;
        });
        for (const Keyed& k : keyed)
            Visible.push_back(k.E);
        VisibleGen++;
    }

    // the game the focus goes to when there's nothing to give it back to: the last one focused (the one just played), else the first
    int DefaultIndex(App& app) const
    {
        Frontend::Library* lib = app.GetHost().Library;
        int64_t want = FocusedRom ? FocusedRom : lib ? lib->LastFocused : 0;
        for (size_t i = 0; i < Visible.size(); i++)
        {
            if (Visible[i]->Id == want)
                return (int)i;
        }
        // the one focused when the app last closed
        if (!want && !SavedFocus.empty())
        {
            for (size_t i = 0; i < Visible.size(); i++)
            {
                if (_stricmp(Visible[i]->Path.c_str(), SavedFocus.c_str()) == 0)
                    return (int)i;
            }
        }
        return 0;
    }

    enum class Part { Tile, Continue, Menu };

    void Focused(App& app, const LibraryEntry& e, Part part = Part::Tile)
    {
        // said when it moves, for --ui-script checks
        if (LoggedRom != e.Id || LoggedPart != part)
        {
            printf("ui: library focus %s%s\n", e.Name().c_str(), part == Part::Continue ? " (Continue)" : part == Part::Menu ? " (menu button)" : "");
            fflush(stdout);
            LoggedRom = e.Id;
            LoggedPart = part;
        }
        FocusedRom = e.Id;
        FocusedPart = part;
        Host& host = app.GetHost();
        if (host.Library)
            host.Library->LastFocused = e.Id;
        // kept for the next run, written when the settings are next saved (a game starting, the app closing)
        if (host.Settings && SavedFocusSettled && e.Path != SavedFocus)
        {
            SavedFocus = e.Path;
            host.Settings->Set("desktop_library_focus", picojson::value(e.Path), false);
        }
    }

    void Open(App& app, const LibraryEntry& e)
    {
        Focused(app, e);
        app.Push(MakeGameDetailsScreen(e));
    }

    void OpenSettings(App& app, const LibraryEntry& e)
    {
        printf("ui: game settings of %s\n", e.Name().c_str());
        fflush(stdout);
        app.Push(MakeGameSettingsScreen(e));
    }

    // After a game's tile (its InvisibleButton): the press opens the details once it's let go (over the tile, for the mouse), or the
    // game's settings once it was held kHoldSeconds; that press then opens nothing more
    void TilePress(App& app, const LibraryEntry& e)
    {
        ImGuiID id = ImGui::GetItemID();
        auto enterDown = [] { return ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_KeypadEnter); };
        // (Enter activates an item for one frame, and again as it repeats, so a press of Enter is followed by the key itself)
        if (ImGui::IsItemActivated() && !(PressId == id && PressEnter))
        {
            PressId = id;
            PressStart = ImGui::GetTime();
            PressFired = false;
            PressMouse = GImGui->ActiveIdSource == ImGuiInputSource_Mouse;
            PressEnter = !PressMouse && enterDown();
        }
        if (PressId != id)
            return;
        bool down = PressEnter ? enterDown() : ImGui::IsItemActive();
        if (down && !PressFired && ImGui::GetTime() - PressStart >= kHoldSeconds)
        {
            PressFired = true;
            OpenSettings(app, e);
        }
        else if (!down)
        {
            PressId = 0;
            PressEnter = false;
            if (!PressFired && (!PressMouse || ImGui::IsItemHovered()))
                Open(app, e);
        }
    }

    // after an item: the focus it was to get (PendingFocus), now that it's there
    void ClaimFocus()
    {
        if (!PendingFocus || ImGui::GetItemID() != PendingFocus)
            return;
        ImGui::SetFocusID(PendingFocus, ImGui::GetCurrentWindow());
        GImGui->NavCursorVisible = true;
        GImGui->NavHighlightItemUnderNav = true;
        ImGui::ScrollToItem();
        PendingFocus = 0;
    }

    // a child the games scroll in, with room for the focus ring; [width] the games' width
    void BeginGames(App& app, const char* id, ImVec2 pos, float width, float height)
    {
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(pos.x - ring, pos.y - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::BeginChild(id, ImVec2(width + 2 * ring, height + ring), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    }

    void EndGames()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    bool DrawList(App& app, ImVec2 pos, ImVec2 area)
    {
        const Palette& p = app.Colors();
        float width = std::min(area.x, app.Px(1100));
        pos.x += (area.x - width) / 2;
        float rowH = app.Px(76), spacing = app.Px(4);
        BeginGames(app, "##list", pos, width, area.y);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, spacing));
        int def = DefaultIndex(app);
        ImGuiListClipper clipper;
        clipper.Begin((int)Visible.size(), rowH + spacing);
        if (WantFocus)
            clipper.IncludeItemByIndex(def);
        bool any = false;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                const LibraryEntry& e = *Visible[i];
                ImGui::PushID((int)(e.Id ^ (e.Id >> 32)));
                ImVec2 at = ImGui::GetCursorScreenPos();
                ImVec2 size(ImGui::GetContentRegionAvail().x, rowH);
                ImGui::InvisibleButton("##rom", size, ImGuiButtonFlags_EnableNav);
                app.TrackFocus(i == def);
                TilePress(app, e);
                bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
                ImGui::PopID();
                if (focused)
                {
                    Focused(app, e);
                    any = true;
                }
                ImVec2 max(at.x + size.x, at.y + size.y);
                float radius = app.Px(16);
                ImU32 bg = focused ? p.SecondaryContainer : held ? WithAlpha(p.OnSurface, 0.12f) : hovered ? WithAlpha(p.OnSurface, 0.08f) : 0;
                if (bg)
                    draw->AddRectFilled(at, max, bg, radius);
                if (focused)
                    FocusRing(app, draw, at, max, radius);
                ImU32 on = focused ? p.OnSecondaryContainer : p.OnSurface;
                ImU32 sub = focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
                float art = rowH - app.Px(16);
                ImVec2 a0(at.x + app.Px(12), at.y + app.Px(8));
                GameArt(app, draw, e, a0, ImVec2(a0.x + art, a0.y + art), app.Px(10), 0.85f);
                float tx = a0.x + art + app.Px(18);
                std::string played = PlayedText(e.LastPlayed);
                float playedW = played.empty() ? 0 : TextSize(app, app.Regular, 15, played.c_str()).x + app.Px(24);
                float tw = max.x - app.Px(20) - playedW - tx;
                std::string title = Ellipsize(app, app.Medium, 19, e.Name(), tw);
                ImVec2 ts = TextSize(app, app.Medium, 19, title.c_str());
                ImVec2 ls = TextSize(app, app.Regular, 15, "Ag");
                float ty = at.y + (rowH - ts.y - ls.y - app.Px(3)) / 2;
                TextAt(app, app.Medium, 19, ImVec2(tx, ty), on, title.c_str());
                float ly = ty + ts.y + app.Px(3);
                if (e.HasMetadata())
                {
                    std::string line = e.Developer.empty() ? e.FileName : e.Developer + "  \xc2\xb7  " + e.FileName;
                    TextAt(app, app.Regular, 15, ImVec2(tx, ly), sub, Ellipsize(app, app.Regular, 15, line, tw).c_str());
                }
                else
                {
                    ImVec2 none = ItalicTextAt(app, app.Regular, 15, ImVec2(tx, ly), sub, "No metadata");
                    std::string rest = "  \xc2\xb7  " + e.FileName;
                    TextAt(app, app.Regular, 15, ImVec2(tx + none.x + app.Px(2), ly), sub,
                        Ellipsize(app, app.Regular, 15, rest, tw - none.x - app.Px(2)).c_str());
                }
                if (!played.empty())
                {
                    ImVec2 ps = TextSize(app, app.Regular, 15, played.c_str());
                    TextAt(app, app.Regular, 15, ImVec2(max.x - app.Px(20) - ps.x, at.y + (rowH - ps.y) / 2), sub, played.c_str());
                }
                // a hairline between rows, as the app's list has
                if (!focused && i + 1 < (int)Visible.size())
                    draw->AddLine(ImVec2(tx, max.y + spacing / 2), ImVec2(max.x - app.Px(12), max.y + spacing / 2), p.OutlineVariant,
                        std::max(1.f, app.Px(1)));
            }
        }
        ImGui::PopStyleVar();
        EndGames();
        return any;
    }

    bool DrawGrid(App& app, ImVec2 pos, ImVec2 area)
    {
        const Palette& p = app.Colors();
        float gap = app.Px(16);
        // tiles of about 176 wide: six columns at 720p, as many as fit on a wider window, at least two
        int columns = std::max(2, (int)((area.x + gap) / (app.Px(176) + gap)));
        float tileW = std::floor((area.x - gap * (columns - 1)) / columns);
        float inset = app.Px(8);
        float artW = tileW - 2 * inset, artH = std::floor(artW * 10 / 9);
        float lineH = TextSize(app, app.Medium, 16, "Ag").y;
        float tileH = inset + artH + app.Px(8) + 2 * lineH + app.Px(6) + app.Px(22) + app.Px(10);
        int rows = ((int)Visible.size() + columns - 1) / columns;
        BeginGames(app, "##grid", pos, area.x, area.y);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        int def = DefaultIndex(app);
        ImGuiListClipper clipper;
        clipper.Begin(rows, tileH + gap);
        if (WantFocus)
            clipper.IncludeItemByIndex(def / columns);
        bool any = false;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            {
                for (int col = 0; col < columns; col++)
                {
                    int i = row * columns + col;
                    if (i >= (int)Visible.size())
                        break;
                    if (col)
                        ImGui::SameLine();
                    const LibraryEntry& e = *Visible[i];
                    ImGui::PushID((int)(e.Id ^ (e.Id >> 32)));
                    ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##rom", ImVec2(tileW, tileH), ImGuiButtonFlags_EnableNav);
                    app.TrackFocus(i == def);
                    TilePress(app, e);
                    bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
                    ImGui::PopID();
                    if (focused)
                    {
                        Focused(app, e);
                        any = true;
                    }
                    ImVec2 max(at.x + tileW, at.y + tileH);
                    float radius = app.Px(18);
                    ImU32 bg = focused ? p.SecondaryContainer : held ? WithAlpha(p.OnSurface, 0.12f) : hovered ? WithAlpha(p.OnSurface, 0.08f) : 0;
                    if (bg)
                        draw->AddRectFilled(at, max, bg, radius);
                    if (focused)
                        FocusRing(app, draw, at, max, radius);
                    ImVec2 a0(at.x + inset, at.y + inset), a1(a0.x + artW, a0.y + artH);
                    if (GameArt(app, draw, e, a0, a1, app.Px(12), 0.62f) && e.Icon)
                    {
                        // the banner icon as a badge in the art's corner, as the app's grid tile
                        float b = app.Px(40);
                        ImVec2 b0(a0.x + app.Px(8), a1.y - app.Px(8) - b), b1(b0.x + b, b0.y + b);
                        draw->AddRectFilled(ImVec2(b0.x - app.Px(3), b0.y - app.Px(3)), ImVec2(b1.x + app.Px(3), b1.y + app.Px(3)),
                            WithAlpha(p.SurfaceContainerHighest, 0.92f), app.Px(8));
                        if (GLuint icon = app.IconTexture(e.Id, e.Icon))
                            draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)icon), b0, b1);
                    }
                    if (e.State == LibraryEntry::kNotRead)
                        IconAt(app, Icon::Cloud, 18, ImVec2(a1.x - app.Px(16), a0.y + app.Px(16)), p.OnSurfaceVariant);
                    ImU32 on = focused ? p.OnSecondaryContainer : p.OnSurface;
                    ImU32 sub = focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
                    float y = a1.y + app.Px(8);
                    for (const std::string& line : WrapLines(app, app.Medium, 16, e.Name(), artW, 2))
                    {
                        TextAt(app, app.Medium, 16, ImVec2(a0.x, y), on, line.c_str());
                        y += lineH;
                    }
                    y = a1.y + app.Px(8) + 2 * lineH + app.Px(6);
                    if (e.HasMetadata())
                    {
                        ImU32 chipBg = focused ? Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.12f) : p.SurfaceContainerHigh;
                        float x = a0.x;
                        std::string region = RegionShort(e.GameCode);
                        if (!region.empty())
                            x += MetaChip(app, draw, ImVec2(x, y), region, artW, chipBg, sub) + app.Px(4);
                        if (!e.Developer.empty() && a1.x - x > app.Px(40))
                            MetaChip(app, draw, ImVec2(x, y), e.Developer, a1.x - x, chipBg, sub);
                    }
                    else
                        ItalicTextAt(app, app.Regular, 14, ImVec2(a0.x, y + app.Px(2)), sub, "No metadata");
                }
            }
        }
        ImGui::PopStyleVar();
        EndGames();
        return any;
    }

    // the mosaic packed again when the games shown or the columns changed (not while scrolling: a game played or renamed changes Visible)
    void PackMosaicFor(int columns, Frontend::Library* lib)
    {
        if (MosaicGen == VisibleGen && MosaicColumns == columns)
            return;
        MosaicGen = VisibleGen;
        MosaicColumns = columns;
        std::vector<MosaicEntry> entries = ChooseMosaicSizes((int)Visible.size(), [&](int i) { return Visible[i]->LastPlayed; }, [&](int i)
        {
            // a game with title-screen art, one in six of them by its ROM hash (its path without one), as the app picks them
            const LibraryEntry& e = *Visible[i];
            if (!e.HasTitleArt)
                return false;
            if (lib && lib->PretendWideOneIn > 0)
            {
                // (--pretend-wide-tiles: by the path's FNV-1a)
                uint32_t h = 2166136261u;
                for (unsigned char c : LowerAscii(e.Path))
                    h = (h ^ c) * 16777619u;
                return h % (uint32_t)lib->PretendWideOneIn == 0;
            }
            return IsWideMosaicGame(e.RaHash);
        });
        // the same tiles in the same order (a sync published art that changes no tile's size): no new packing, so nothing moves
        bool same = !Bands.empty() && entries.size() == PackedSizes.size() && columns == PackedColumns
            && std::equal(entries.begin(), entries.end(), PackedSizes.begin(), [](const MosaicEntry& x, const MosaicEntry& y)
            { return x.Item == y.Item && x.Size == y.Size; });
        if (same)
            return;
        PackedSizes = entries;
        PackedColumns = columns;
        Bands = PackMosaic(entries, columns);
        RowBand.clear();
        ItemTile.assign(Visible.size(), {0, 0});
        FirstFeatured = -1;
        int featured = 0, wide = 0;
        for (int b = 0; b < (int)Bands.size(); b++)
        {
            for (int r = 0; r < Bands[b].RowCount; r++)
                RowBand.push_back(b);
            for (int t = 0; t < (int)Bands[b].Tiles.size(); t++)
            {
                const MosaicPlacement& tile = Bands[b].Tiles[t];
                ItemTile[tile.Item] = {b, t};
                if (tile.Size == MosaicSize::Featured)
                {
                    featured++;
                    // the first band's first featured tile has the Continue pill
                    if (FirstFeatured < 0 && b == 0)
                        FirstFeatured = tile.Item;
                }
                else if (tile.Size == MosaicSize::Wide)
                    wide++;
            }
        }
        MosaicRows = (int)RowBand.size();
        printf("ui: mosaic of %zu games: %d columns, %d rows in %zu bands, %d featured, %d wide\n", Visible.size(), columns, MosaicRows,
            Bands.size(), featured, wide);
        fflush(stdout);
    }

    bool DrawMosaic(App& app, ImVec2 pos, ImVec2 area)
    {
        const Palette& p = app.Colors();
        // cells of at least 150 (seven columns at 720p), rows as tall, so a cell is about square as in the app
        float gap = app.Px(12), rowH = app.Px(150);
        int columns = MosaicColumnsFor(area.x, gap, app.Px(150));
        PackMosaicFor(columns, app.GetHost().Library);
        BeginGames(app, "##mosaic", pos, area.x, area.y);
        float width = area.x;

        // the app's section label: what the order is
        int sort = SortIndex(app);
        bool anyPlayed = std::any_of(Visible.begin(), Visible.end(), [](const LibraryEntry* e) { return e->LastPlayed > 0; });
        const char* label = sort == 0 ? "A TO Z" : sort == 1 ? "Z TO A" : !anyPlayed ? nullptr : sort == 2 ? "RECENTLY PLAYED" : "LEAST RECENTLY PLAYED";
        if (label)
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            TextAt(app, app.Medium, 13, ImVec2(at.x + app.Px(2), at.y), p.OnSurfaceVariant, label);
            ImGui::Dummy(ImVec2(1, app.Px(24)));
        }
        ImVec2 origin = ImGui::GetCursorScreenPos();
        int def = DefaultIndex(app);
        ImGuiListClipper clipper;
        clipper.Begin(MosaicRows, rowH + gap);
        if (WantFocus && def < (int)ItemTile.size())
        {
            const MosaicBand& band = Bands[ItemTile[def].first];
            clipper.IncludeItemsByIndex(band.FirstRow, band.FirstRow + band.RowCount);
        }
        std::vector<char> drawn(Bands.size(), 0);
        bool any = false;
        while (clipper.Step())
        {
            if (clipper.DisplayStart >= clipper.DisplayEnd)
                continue;
            // the bands with a row in the range, each once (a featured tile's band is two rows tall)
            int b0 = RowBand[clipper.DisplayStart], b1 = RowBand[clipper.DisplayEnd - 1];
            for (int b = b0; b <= b1; b++)
            {
                if (drawn[b])
                    continue;
                drawn[b] = 1;
                const MosaicBand& band = Bands[b];
                // a column's start from the whole width each time, so rounding can't add up and every row ends at the same edge
                auto colX = [&](int c) { return origin.x + std::floor(c * (width + gap) / columns); };
                for (const MosaicPlacement& tile : band.Tiles)
                {
                    int rows = RowSpan(tile.Size);
                    ImVec2 t0(colX(tile.Column), origin.y + (band.FirstRow + tile.Row) * (rowH + gap));
                    ImVec2 t1(colX(tile.Column + ColumnSpan(tile.Size)) - gap, t0.y + rows * rowH + (rows - 1) * gap);
                    any |= DrawMosaicTile(app, *Visible[tile.Item], tile, t0, t1, tile.Item == def, !HasTileAtEnd(band, tile));
                }
            }
        }
        // out of a featured tile's pill and menu button once the focus went anywhere else
        ImGuiID f = ImGui::GetFocusID();
        if (InsideTile && !PendingFocus && f != InsideTile && f != InsidePill && f != InsideMenu)
            InsideTile = InsidePill = InsideMenu = 0;
        EndGames();
        return any;
    }

    // ImGui moves left and right from the height it last moved up or down at (its preferred position), which for a featured tile, two
    // rows tall, can be the gap between the rows; nothing beside it is then more to the side than above or below, and the move finds
    // nothing. Kept in one of its rows: the nearer one, the top one first
    void KeepRowForSideMoves(ImVec2 t0, ImVec2 t1)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.NavWindow ? g.NavWindow : ImGui::GetCurrentWindow();
        ImVec2& preferred = window->RootWindowForNav->NavPreferredScoringPosRel[g.NavLayer];
        float offset = window->DC.CursorStartPos.y;
        float rowH = (t1.y - t0.y) / 2;
        float top = t0.y + rowH / 2, bottom = t1.y - rowH / 2;
        float y = preferred.y == FLT_MAX ? FLT_MAX : preferred.y + offset;
        // (a row's middle half: ImGui scores rows by the middle of a box)
        bool inRow = y != FLT_MAX && (std::fabs(y - top) < rowH / 4 || std::fabs(y - bottom) < rowH / 4);
        if (!inRow)
            preferred.y = (y != FLT_MAX && std::fabs(y - bottom) < std::fabs(y - top) ? bottom : top) - offset;
    }

    // A mosaic tile: the banner icon on its tinted card (the art in step C) with the title under it. A featured one (2x2) also has its
    // label (Continue or Recent), when it was played, a menu button for the game's settings and, the first one, the Continue pill that
    // starts the game. Those two sit inside the tile, where the D-pad doesn't look, so down goes into them and up comes back out
    bool DrawMosaicTile(App& app, const LibraryEntry& e, const MosaicPlacement& tile, ImVec2 t0, ImVec2 t1, bool isDefault, bool lastInRow)
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        bool featured = tile.Size == MosaicSize::Featured;
        bool showContinue = featured && tile.Item == FirstFeatured;
        ImVec2 size(t1.x - t0.x, t1.y - t0.y);
        ImGui::PushID((int)(e.Id ^ (e.Id >> 32)));
        ImGui::SetCursorScreenPos(t0);
        // (the pill and the menu button over it take the mouse)
        if (featured)
            ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##rom", size, ImGuiButtonFlags_EnableNav);
        ImGuiID tileId = ImGui::GetItemID();
        app.TrackFocus(isDefault);
        ClaimFocus();
        bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImGuiID pillId = showContinue ? ImGui::GetID("##continue") : 0;
        ImGuiID menuId = featured ? ImGui::GetID("##tilemenu") : 0;
        TilePress(app, e);
        bool any = false;
        if (focused)
        {
            Focused(app, e);
            any = true;
            if (featured && DownPressed())
            {
                // into the tile: its pill, else its menu button
                ImGui::NavMoveRequestCancel();
                InsideTile = tileId;
                PendingFocus = pillId ? pillId : menuId;
            }
            else if (lastInRow && RightPressed())
            {
                // nothing beside it: the focus stays rather than look in other rows
                ImGui::NavMoveRequestCancel();
            }
            if (featured)
                KeepRowForSideMoves(t0, t1);
        }

        float radius = featured ? app.Px(20) : app.Px(14);
        // the art fills the tile, with the title on a scrim (the app's tile with art): a wide tile's title screen, else the game's art
        const Frontend::ArtRef& artFile = tile.Size == MosaicSize::Wide && !e.TitleArtFile.empty() ? e.TitleArtFile : e.ArtFile;
        bool art = false;
        if (!artFile.empty())
        {
            draw->AddRectFilled(t0, t1, p.SurfaceContainerHighest, radius);
            art = ArtImage(app, draw, artFile, IsTitleArt(artFile), t0, t1, radius, true);
        }
        const ImU32 onArt = IM_COL32(255, 255, 255, 255), onArtSub = IM_COL32(255, 255, 255, 205);
        if (!featured)
        {
            // the icon over the title (two lines at the bottom), as the app's tile without art
            float tx = app.Px(10);
            float lineH = TextSize(app, app.Medium, 15, "Ag").y;
            std::vector<std::string> lines = WrapLines(app, app.Medium, 15, e.Name(), size.x - 2 * tx, 2);
            float textH = lines.size() * lineH;
            if (art)
                Scrim(app, draw, t0, t1, t1.y - app.Px(16) - textH, radius);
            else
                GameArtIn(app, draw, e, t0, t1, radius, ImVec2(t0.x + app.Px(8), t0.y + app.Px(8)),
                    ImVec2(t1.x - app.Px(8), t1.y - app.Px(12) - textH), 0.75f);
            if (held || hovered)
                draw->AddRectFilled(t0, t1, WithAlpha(p.OnSurface, held ? 0.12f : 0.08f), radius);
            float y = t1.y - app.Px(10) - textH;
            for (const std::string& line : lines)
            {
                TextAt(app, app.Medium, 15, ImVec2(t0.x + tx, y), art ? onArt : p.OnSurface, line.c_str());
                y += lineH;
            }
            if (focused)
                FocusRing(app, draw, t0, t1, radius);
            ImGui::PopID();
            return any;
        }

        // featured: from the bottom up, the pill (the first one), when it was played, the title; the icon in what's left
        float pad = app.Px(16), pillH = app.Px(38), menuD = app.Px(44);
        float y = t1.y - pad - (showContinue ? pillH + app.Px(10) : 0);
        std::string played = PlayedText(e.LastPlayed);
        float playedH = TextSize(app, app.Regular, 14, "Ag").y;
        float titleW = size.x - 2 * pad - (showContinue ? 0 : menuD);
        float titleLineH = TextSize(app, app.Medium, 19, "Ag").y;
        std::vector<std::string> lines = WrapLines(app, app.Medium, 19, e.Name(), titleW, 2);
        y -= playedH;
        float playedY = y;
        y -= app.Px(4) + lines.size() * titleLineH;
        float titleY = y;
        if (art)
            Scrim(app, draw, t0, t1, titleY - app.Px(12), radius);
        else
            GameArtIn(app, draw, e, t0, t1, radius, ImVec2(t0.x + pad, t0.y + app.Px(48)), ImVec2(t1.x - pad, titleY - app.Px(8)), 0.8f);
        if (held || hovered)
            draw->AddRectFilled(t0, t1, WithAlpha(p.OnSurface, held ? 0.12f : 0.08f), radius);
        // the label in the corner
        const char* eyebrow = showContinue ? "CONTINUE" : "RECENT";
        ImVec2 es = TextSize(app, app.Medium, 12, eyebrow);
        ImVec2 e0(t0.x + app.Px(12), t0.y + app.Px(12)), e1(e0.x + es.x + app.Px(14), e0.y + es.y + app.Px(6));
        draw->AddRectFilled(e0, e1, p.SecondaryContainer, app.Px(5));
        TextAt(app, app.Medium, 12, ImVec2(e0.x + app.Px(7), e0.y + app.Px(3)), p.OnSecondaryContainer, eyebrow);
        for (const std::string& line : lines)
        {
            TextAt(app, app.Medium, 19, ImVec2(t0.x + pad, titleY), art ? onArt : p.OnSurface, line.c_str());
            titleY += titleLineH;
        }
        if (!played.empty())
            TextAt(app, app.Regular, 14, ImVec2(t0.x + pad, playedY), art ? onArtSub : p.OnSurfaceVariant, played.c_str());
        if (focused)
            FocusRing(app, draw, t0, t1, radius);

        bool inside = InsideTile == tileId;
        // the Continue pill: starts the game at once
        if (showContinue)
        {
            ImVec2 ts = TextSize(app, app.Medium, 15, "Continue");
            ImVec2 c0(t0.x + pad, t1.y - pad - pillH), c1(c0.x + app.Px(14 + 20 + 8 + 16) + ts.x, t1.y - pad);
            ImGui::SetCursorScreenPos(c0);
            if (!inside)
                ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            bool go = ImGui::InvisibleButton("##continue", ImVec2(c1.x - c0.x, pillH), ImGuiButtonFlags_EnableNav);
            if (!inside)
                ImGui::PopItemFlag();
            app.TrackFocus(false);
            ClaimFocus();
            bool pf = ImGui::IsItemFocused(), ph = ImGui::IsItemHovered(), pa = ImGui::IsItemActive();
            if (pf)
            {
                InsideTile = tileId;
                InsidePill = pillId;
                InsideMenu = menuId;
                Focused(app, e, Part::Continue);
                any = true;
                if (UpPressed())
                {
                    ImGui::NavMoveRequestCancel();
                    PendingFocus = tileId;
                }
                else if (RightPressed())
                {
                    ImGui::NavMoveRequestCancel();
                    PendingFocus = menuId;
                }
            }
            ImU32 bg = pa ? Blend(p.PrimaryContainer, p.OnPrimaryContainer, 0.12f) : ph ? Blend(p.PrimaryContainer, p.OnPrimaryContainer, 0.08f)
                                                                                         : p.PrimaryContainer;
            draw->AddRectFilled(c0, c1, bg, pillH / 2);
            IconAt(app, Icon::PlayArrow, 22, ImVec2(c0.x + app.Px(14 + 10), (c0.y + c1.y) / 2), p.OnPrimaryContainer);
            TextAt(app, app.Medium, 15, ImVec2(c0.x + app.Px(14 + 20 + 8), (c0.y + c1.y - ts.y) / 2), p.OnPrimaryContainer, "Continue");
            if (pf)
                FocusRing(app, draw, c0, c1, pillH / 2);
            if (go && host.BootRom)
            {
                Focused(app, e, Part::Continue);
                printf("ui: continue %s\n", e.Path.c_str());
                fflush(stdout);
                host.BootRom(e.Path);
            }
        }
        // the menu button: the game's settings
        {
            ImVec2 m0(t1.x - app.Px(8) - menuD, t1.y - app.Px(8) - menuD);
            ImGui::SetCursorScreenPos(m0);
            if (!inside)
                ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            bool chosen = ImGui::InvisibleButton("##tilemenu", ImVec2(menuD, menuD), ImGuiButtonFlags_EnableNav);
            if (!inside)
                ImGui::PopItemFlag();
            app.TrackFocus(false);
            ClaimFocus();
            bool mf = ImGui::IsItemFocused(), mh = ImGui::IsItemHovered();
            if (mf)
            {
                InsideTile = tileId;
                InsidePill = pillId;
                InsideMenu = menuId;
                Focused(app, e, Part::Menu);
                any = true;
                if (UpPressed() || (LeftPressed() && !pillId))
                {
                    ImGui::NavMoveRequestCancel();
                    PendingFocus = tileId;
                }
                else if (LeftPressed())
                {
                    ImGui::NavMoveRequestCancel();
                    PendingFocus = pillId;
                }
            }
            ImVec2 c(m0.x + menuD / 2, m0.y + menuD / 2);
            if (mf)
                draw->AddCircleFilled(c, menuD / 2, p.SecondaryContainer);
            else if (mh)
                draw->AddCircleFilled(c, menuD / 2, WithAlpha(p.OnSurface, 0.08f));
            if (mf)
                draw->AddCircle(c, menuD / 2 + app.Px(4), p.Primary, 0, std::max(2.f, IM_ROUND(app.Px(3))));
            IconAt(app, Icon::MoreVert, 24, c, mf ? p.OnSecondaryContainer : art ? onArt : p.OnSurfaceVariant);
            if (chosen)
                OpenSettings(app, e);
        }
        ImGui::PopID();
        return any;
    }

    // nothing to show: why, and what to do about it
    void DrawEmpty(App& app, const Frontend::Library::Progress& progress, const std::vector<std::string>& folders, size_t total, ImVec2 pos,
        ImVec2 area)
    {
        const Palette& p = app.Colors();
        float width = std::min(area.x, app.Px(620));
        float x = pos.x + (area.x - width) / 2, y = pos.y + app.Px(24);
        ImWchar icon = Icon::CreateNewFolder;
        std::string heading, text;
        enum class Action { AddFolder, Rescan, ShowAll, None };
        std::vector<Action> actions;
        if (total > 0)
        {
            icon = Icon::Search;
            if (!Query.empty())
            {
                heading = "No games match \xe2\x80\x9c" + Query + "\xe2\x80\x9d";
                text = Filter ? "The search looks at the titles and file names of the games the filter shows." : "The search looks at the titles and file names.";
            }
            else if (Filter == 1)
            {
                heading = "Nothing played yet";
                text = "Games you play show up here.";
            }
            else
            {
                heading = "Everything here has been played";
                text = "There are no games you haven't played.";
            }
            actions = {Action::ShowAll};
        }
        else if (progress.Scanning)
        {
            icon = Icon::Sync;
            heading = "Looking for games\xe2\x80\xa6";
            text = progress.Found > 0 ? "Reading " + progress.Current : "Going through your ROM folders.";
        }
        else if (folders.empty())
        {
            heading = "Add a ROM folder";
            text = "The library shows the games in the folders you add and their subfolders: .nds and .dsi files, and .zip and .7z archives "
                   "with one in them.";
            actions = {Action::AddFolder};
        }
        else
        {
            icon = Icon::Folder;
            heading = "No games in your ROM folders";
            text = "No .nds or .dsi files, or .zip or .7z archives with one in them, were found in " + std::to_string(folders.size())
                + (folders.size() == 1 ? " folder." : " folders.");
            actions = {Action::AddFolder, Action::Rescan};
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float d = app.Px(72);
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + d, y + d), p.SecondaryContainer, app.Px(20));
        IconAt(app, icon, 40, ImVec2(x + d / 2, y + d / 2), p.OnSecondaryContainer);
        y += d + app.Px(20);
        ImVec2 h = TextAt(app, app.Medium, 26, ImVec2(x, y), p.OnSurface, heading.c_str(), width);
        y += h.y + app.Px(10);
        ImVec2 t = TextAt(app, app.Regular, 17, ImVec2(x, y), p.OnSurfaceVariant, text.c_str(), width);
        y += t.y + app.Px(28);
        Host& host = app.GetHost();
        bool first = true;
        for (Action action : actions)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            bool chosen = false;
            switch (action)
            {
            case Action::AddFolder:
                chosen = BigButton(app, "##addfolder", Icon::CreateNewFolder, "Add a ROM folder", nullptr, true, app.Px(380), first);
                if (chosen)
                    AddFolder(app);
                break;
            case Action::Rescan:
                chosen = BigButton(app, "##rescan", Icon::Refresh, "Rescan the library", nullptr, false, app.Px(380), first);
                if (chosen && host.Library)
                    host.Library->Rescan();
                break;
            case Action::ShowAll:
                chosen = BigButton(app, "##showall", Icon::FilterList, "Show all games", nullptr, true, app.Px(380), first);
                if (chosen)
                {
                    Query.clear();
                    Filter = 0;
                }
                break;
            default:
                break;
            }
            first = false;
            y += ImGui::GetItemRectSize().y + app.Px(14);
        }
        // a game file outside the folders still opens
        if (total == 0 && !progress.Scanning)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (BigButton(app, "##openrom", Icon::FolderOpen, "Open ROM", ".nds, .dsi, .zip or .7z file", false, app.Px(380), actions.empty()))
                host.OpenRom();
        }
    }

    void AddFolder(App& app)
    {
        Host& host = app.GetHost();
        if (host.Hidden || !host.PickFolder)
        {
            app.Toast("The folder picker doesn't open in a hidden run");
            return;
        }
        std::vector<std::string> folders = host.Settings ? host.Settings->GetStringList("rom_search_dirs") : std::vector<std::string>();
        std::optional<std::string> picked = host.PickFolder(folders.empty() ? "" : folders.back());
        if (!picked)
            return;
        for (const std::string& f : folders)
        {
            if (Frontend::Library::SameFolder(f, *picked))
            {
                app.Toast("That folder is in the library already");
                return;
            }
        }
        folders.push_back(*picked);
        picojson::array list;
        for (const std::string& f : folders)
            list.emplace_back(f);
        if (host.ChangeSetting)
            host.ChangeSetting("rom_search_dirs", picojson::value(list));
    }

    void OpenMenu()
    {
        // the focus moves into it next frame, once the games under it are disabled (this frame's would take it)
        MenuOpen = true;
        MenuFocus = true;
        printf("ui: library menu\n");
        fflush(stdout);
    }

    void CloseMenu()
    {
        MenuOpen = false;
        WantFocus = true;
        FocusId = MenuButtonId;
    }

    void DrawMenu(App& app, float y)
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        // over the whole screen, dimming it; a click outside the panel closes it
        ImGui::SetCursorScreenPos(win);
        ImGui::BeginChild("##menuscrim", size, ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(win, ImVec2(win.x + size.x, win.y + size.y), WithAlpha(p.Scrim, 0.32f));
        float panelW = std::min(app.Px(360), size.x - 2 * app.Px(16));
        float padIn = app.Px(10);
        float panelH = 5 * (app.Px(52) + app.Px(4)) + 2 * padIn;
        ImVec2 p0(win.x + size.x - app.Px(40) - panelW, y);
        ImVec2 p1(p0.x + panelW, p0.y + panelH);
        draw->AddRectFilled(ImVec2(p0.x, p0.y + app.Px(2)), ImVec2(p1.x, p1.y + app.Px(4)), WithAlpha(p.Scrim, 0.25f), app.Px(20));
        draw->AddRectFilled(p0, p1, p.SurfaceContainer, app.Px(20));
        if (ImGui::IsMouseClicked(0) && !ImGui::IsMouseHoveringRect(p0, p1, false))
            CloseMenu();
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padIn, padIn));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(4)));
        ImGui::BeginChild("##menupanel", ImVec2(panelW, panelH), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        enum class Item { None, OpenRom, Folders, Rescan, Settings, Quit } choice = Item::None;
        if (MenuRow(app, "##m_open", Icon::FolderOpen, "Open ROM\xe2\x80\xa6", nullptr, true)) choice = Item::OpenRom;
        if (MenuRow(app, "##m_folders", Icon::Folder, "ROM folders", nullptr)) choice = Item::Folders;
        if (MenuRow(app, "##m_rescan", Icon::Refresh, "Rescan the library", nullptr)) choice = Item::Rescan;
        if (MenuRow(app, "##m_settings", Icon::Settings, "Settings", nullptr)) choice = Item::Settings;
        if (MenuRow(app, "##m_quit", Icon::Power, "Quit", nullptr)) choice = Item::Quit;
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        if (choice != Item::None)
            CloseMenu();
        switch (choice)
        {
        case Item::OpenRom: host.OpenRom(); break;
        case Item::Folders: app.Push(MakeSettingsScreen("library")); break;
        case Item::Rescan:
            if (host.Library)
                host.Library->Rescan();
            app.Toast("Scanning the ROM folders again");
            break;
        case Item::Settings: app.Push(MakeSettingsScreen()); break;
        case Item::Quit: host.Quit(); break;
        default: break;
        }
    }

    LibraryOpen OpenFirst = LibraryOpen::None;
    bool MenuOpen = false, MenuFocus = false;
    ImGuiID MenuButtonId = 0;
    std::string Query;
    int Filter = 0;
    int Shape = -1;
    int64_t FocusedRom = 0, LoggedRom = 0;
    Part LoggedPart = Part::Tile, FocusedPart = Part::Tile;
    // desktop_library_focus: the path of the game focused last
    std::string SavedFocus;
    bool SavedFocusRead = false, SavedFocusSettled = false;
    // the tile being pressed (TilePress)
    ImGuiID PressId = 0;
    double PressStart = 0;
    bool PressFired = false, PressMouse = false, PressEnter = false;
    // an item to give the focus to when it's drawn (the mosaic's way into and out of a featured tile)
    ImGuiID PendingFocus = 0;
    // the featured tile whose Continue pill and menu button the D-pad is in (they're out of the D-pad's reach otherwise), and their ids
    ImGuiID InsideTile = 0, InsidePill = 0, InsideMenu = 0;
    // the mosaic as packed for the games shown (VisibleGen) at a number of columns
    uint64_t VisibleGen = 0, MosaicGen = ~0ull;
    std::vector<MosaicEntry> PackedSizes;
    int PackedColumns = 0;
    int MosaicColumns = 0, MosaicRows = 0;
    std::vector<MosaicBand> Bands;
    std::vector<int> RowBand;                  // each row's band
    std::vector<std::pair<int, int>> ItemTile; // each game's band and tile
    int FirstFeatured = -1;                    // the game whose tile has the Continue pill

    std::shared_ptr<const std::vector<LibraryEntry>> Source;
    int BuiltSort = -1, BuiltFilter = -1;
    std::string BuiltQuery;
    std::vector<const LibraryEntry*> Visible;
};

}

std::unique_ptr<Screen> MakeLibraryScreen(LibraryOpen open)
{
    return std::make_unique<LibraryScreen>(open);
}

}
