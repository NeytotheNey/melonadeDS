// The game library, the home screen when no game runs (phase 2 step A of docs/plan-desktop-app.md, the app's launcher in
// docs/plan-launcher.md section 1): the games in the ROM folders as a List or a Grid (rom_list_view_mode), sorted as the app sorts them
// (rom_sorting_mode and rom_sorting_order), with a filter (All, Played, Not played; not saved, as in the app) and a search through the
// on-screen keyboard. The controls sit in a row above the games, which the focus reaches by going up from the first row; L / R switch
// the view. A game opens its details (with Play), and the menu has Open ROM, the folders, a rescan, Settings and Quit. The list and the
// grid are virtualized (ImGuiListClipper), and the icons' textures are made as they're first drawn
#include <algorithm>
#include <cmath>
#include <optional>

#include "imgui_internal.h"
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

// List | Grid: a segment each; returns the one chosen, or -1
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
    explicit LibraryScreen(bool openDetails) : OpenDetails(openDetails) {}

    const char* Id() const override { return "library"; }

    bool Back(App&) override
    {
        if (MenuOpen)
            CloseMenu();
        // the root: nothing to go back to
        return true;
    }

    void Tab(App& app, int) override
    {
        if (MenuOpen)
            return;
        SetView(app, !Grid(app));
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
        Frontend::Library::Progress progress = lib ? lib->GetProgress() : Frontend::Library::Progress();
        Rebuild(app);
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

        // --open library/details: the first game's details, once the scan is done
        if (OpenDetails && lib && !lib->Busy() && progress.Scanned)
        {
            OpenDetails = false;
            if (!Visible.empty())
            {
                FocusedRom = Visible[0]->Id;
                app.Push(MakeGameDetailsScreen(*Visible[0]));
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
        if (!status.empty())
        {
            float room = win.x + size.x - pad - (m0.x + mark + app.Px(200));
            std::string fitted = Ellipsize(app, app.Regular, 16, status, std::max(room, app.Px(60)));
            ImVec2 s = TextSize(app, app.Regular, 16, fitted.c_str());
            float sx = win.x + size.x - pad - s.x;
            TextAt(app, app.Regular, 16, ImVec2(sx, m0.y + (mark - s.y) / 2), p.OnSurfaceVariant, fitted.c_str());
            if (progress.Scanning || progress.Preparing)
                IconAt(app, Icon::Sync, 20, ImVec2(sx - app.Px(16), m0.y + mark / 2), p.Primary);
        }

        // the controls: view, sort, filter, search, and the menu on the right
        float rowY = win.y + app.Px(88);
        ImGui::SetCursorScreenPos(ImVec2(win.x + pad, rowY));
        // a narrow window: the controls as icons
        bool compact = size.x < app.Px(980);
        const char* views[] = {compact ? "" : "List", compact ? "" : "Grid"};
        const ImWchar viewIcons[] = {Icon::ViewList, Icon::GridView};
        int view = Segmented(app, views, viewIcons, 2, Grid(app) ? 1 : 0);
        if (view >= 0)
            SetView(app, view == 1);
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
        if (Visible.empty())
            DrawEmpty(app, progress, folders, total, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH));
        else
            gamesFocused = Grid(app) ? DrawGrid(app, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH))
                                     : DrawList(app, ImVec2(win.x + pad, top), ImVec2(size.x - 2 * pad, contentH));
        if (menu)
            ImGui::EndDisabled();
        if (menu)
            DrawMenu(app, rowY + app.Px(52));

        if (!gamesFocused)
            LoggedRom = 0;
        if (menu)
            FooterHints(app, {{"A|Enter", "Choose"}, {"B|Esc", "Close"}}, p.SurfaceContainerLowest);
        else if (searchFocused && !Query.empty())
            FooterHints(app, {{"A|Enter", "Edit search"}, {"X|Del", "Clear search"}, {"L/R|Q/W", "List / Grid"}}, p.SurfaceContainerLowest);
        else if (gamesFocused)
            FooterHints(app, {{"A|Enter", "Details"}, {"L/R|Q/W", "List / Grid"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "Select"}, {"L/R|Q/W", "List / Grid"}}, p.SurfaceContainerLowest);
    }

private:
    bool Grid(App& app) const
    {
        Host& host = app.GetHost();
        return host.Settings && host.Settings->GetString("rom_list_view_mode", "grid") == "grid";
    }

    void SetView(App& app, bool grid)
    {
        Host& host = app.GetHost();
        if (grid == Grid(app))
            return;
        if (host.ChangeSetting)
            host.ChangeSetting("rom_list_view_mode", picojson::value(std::string(grid ? "grid" : "list")));
        // the focus stays on the same game, which a new layout puts somewhere else
        WantFocus = true;
        printf("ui: library %s\n", grid ? "grid" : "list");
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
        return 0;
    }

    void Focused(App& app, const LibraryEntry& e)
    {
        // said when it moves, for --ui-script checks
        if (LoggedRom != e.Id)
        {
            printf("ui: library focus %s\n", e.Name().c_str());
            fflush(stdout);
            LoggedRom = e.Id;
        }
        FocusedRom = e.Id;
        if (Frontend::Library* lib = app.GetHost().Library)
            lib->LastFocused = e.Id;
    }

    void Open(App& app, const LibraryEntry& e)
    {
        Focused(app, e);
        app.Push(MakeGameDetailsScreen(e));
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
                bool pressed = ImGui::InvisibleButton("##rom", size, ImGuiButtonFlags_EnableNav);
                app.TrackFocus(i == def);
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
                if (pressed)
                    Open(app, e);
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
                    bool pressed = ImGui::InvisibleButton("##rom", ImVec2(tileW, tileH), ImGuiButtonFlags_EnableNav);
                    app.TrackFocus(i == def);
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
                    GameArt(app, draw, e, a0, a1, app.Px(12), 0.62f);
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
                    if (pressed)
                        Open(app, e);
                }
            }
        }
        ImGui::PopStyleVar();
        EndGames();
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

    bool OpenDetails = false;
    bool MenuOpen = false, MenuFocus = false;
    ImGuiID MenuButtonId = 0;
    std::string Query;
    int Filter = 0;
    int Shape = -1;
    int64_t FocusedRom = 0, LoggedRom = 0;

    std::shared_ptr<const std::vector<LibraryEntry>> Source;
    int BuiltSort = -1, BuiltFilter = -1;
    std::string BuiltQuery;
    std::vector<const LibraryEntry*> Visible;
};

}

std::unique_ptr<Screen> MakeLibraryScreen(bool openDetails)
{
    return std::make_unique<LibraryScreen>(openDetails);
}

}
