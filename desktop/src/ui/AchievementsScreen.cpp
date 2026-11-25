// The pause menu's Achievements, the app's AchievementList: the game's icon and title, what's unlocked and the mode along the top; a tab
// per set when the game has more than one (core first, L / R); then the set's achievements in rc_client's progress grouping, a section
// per bucket in a fixed order, each row with its badge (the locked one while it's locked), title, description, progress and points.
// Confirming a row opens its page on retroachievements.org. Copied out of rc_client's list when the screen opens and again when an unlock,
// a reset or a state load changes it (App::AchievementChanges), so nothing of rc_client's is held while it's drawn. Virtualized per section
#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "imgui_internal.h"
#include "Icons.h"
#include "RaOverlay.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"
#include "achievements/Achievements.h"

namespace Ui
{

namespace
{

// what a list from the server can hold at most here, so a broken one can't grow the screen without end
constexpr uint32_t MAX_SETS = 64;
constexpr uint32_t MAX_ACHIEVEMENTS = 10000;

// the sections, in this order (rc_client's progress buckets; unofficial ones aren't listed)
const struct { uint8_t Bucket; const char* Label; } SECTIONS[] = {
    {RC_CLIENT_ACHIEVEMENT_BUCKET_UNSYNCED, "Not sent yet"},
    {RC_CLIENT_ACHIEVEMENT_BUCKET_ACTIVE_CHALLENGE, "Active challenges"},
    {RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED, "Recently unlocked"},
    {RC_CLIENT_ACHIEVEMENT_BUCKET_ALMOST_THERE, "Almost there"},
    {RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED, "Locked"},
    {RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED, "Unlocked"},
    {RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED, "Unsupported"},
};

std::string Text(const char* text)
{
    return text ? text : "";
}

// a small tag (the mode, "Missable") at [pos], as the library's tiles have them; returns its width
float Chip(App& app, ImDrawList* draw, ImVec2 pos, const char* text, ImU32 bg, ImU32 fg)
{
    ImVec2 s = TextSize(app, app.Regular, 13, text);
    float w = s.x + app.Px(12), h = app.Px(22);
    draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, app.Px(5));
    draw->AddText(app.Regular, app.Px(13), ImVec2(IM_ROUND(pos.x + app.Px(6)), IM_ROUND(pos.y + (h - s.y) / 2)), fg, text);
    return w;
}

class AchievementsScreen : public Screen
{
public:
    const char* Id() const override { return "pause/achievements"; }

    void Tab(App& app, int direction) override
    {
        if (Sets.size() > 1)
            SetTab((TabIndex + direction + (int)Sets.size()) % (int)Sets.size());
    }

    void Draw(App& app) override
    {
        // the game unloaded meanwhile (a logout, the session's end): back to the pause menu, also with the rows copied before
        if (!HasAchievementList(app.GetHost()))
        {
            app.Pop();
            return;
        }
        if (!Built || Seen != app.AchievementChanges())
            Rebuild(app);
        const Palette& p = app.Colors();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float footer = FooterHeight(app);
        if (DrawHeader(app, win, size))
        {
            app.Pop();
            return;
        }
        float top = win.y + app.Px(88) + app.Px(8);
        if (Sets.size() > 1)
        {
            int clicked = TabStrip(app, ImVec2(win.x + app.Px(40), win.y + app.Px(88) + app.Px(4)), size.x - app.Px(40), TabLabels.data(),
                (int)TabLabels.size(), TabIndex);
            if (clicked >= 0 && clicked != TabIndex)
                SetTab(clicked);
            top = win.y + app.Px(88) + app.Px(56);
        }

        // one column, at most 900 wide, centered; it scrolls with the focus
        float pad = app.Px(40);
        float colW = std::min(size.x - 2 * pad, app.Px(900));
        float x = win.x + (size.x - colW) / 2;
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(x - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(4)));
        if (ResetScroll)
        {
            ImGui::SetNextWindowScroll(ImVec2(0, 0));
            ResetScroll = false;
        }
        ImGui::BeginChild("##achievements", ImVec2(colW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (TabIndex < (int)Sets.size())
            DrawSections(app, Sets[TabIndex]);
        ImGui::Dummy(ImVec2(1, app.Px(24)));
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        if (Sets.size() > 1)
            FooterHints(app, {{"A|Enter", "View achievement"}, {"L/R|Q/W", "Sets"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "View achievement"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
    }

private:
    struct Row
    {
        uint32_t Id = 0, Points = 0;
        std::string Title, Description, BadgeUrl, Progress;
        float Percent = 0;
        bool Missable = false, Locked = true;
        // the text fitted to the row, made again when its width changes
        float FittedWidth = -1;
        std::string TitleFit;
        std::vector<std::string> DescriptionFit;
    };
    struct Section
    {
        const char* Label = "";
        std::vector<size_t> Rows;
    };
    struct Set
    {
        uint32_t Id = 0;
        std::string Title;
        std::vector<Section> Sections;
    };
    struct Placed
    {
        size_t Row;
        uint8_t Bucket;
        uint32_t Subset;
    };

    void SetTab(int tab)
    {
        TabIndex = tab;
        WantFocus = true;
        FocusId = 0;
        ResetScroll = true;
    }

    // the sets (core first), by rc_client_create_subset_list
    static std::vector<std::pair<uint32_t, std::string>> ListSets(rc_client_t* client, uint32_t gameId)
    {
        std::vector<std::pair<uint32_t, std::string>> sets;
        if (rc_client_subset_list_t* list = rc_client_create_subset_list(client))
        {
            for (uint32_t i = 0; i < list->num_subsets && i < MAX_SETS; i++)
            {
                if (const rc_client_subset_t* subset = list->subsets[i])
                    sets.emplace_back(subset->id, Text(subset->title));
            }
            rc_client_destroy_subset_list(list);
        }
        std::stable_partition(sets.begin(), sets.end(), [&](const auto& set) { return set.first == gameId; });
        return sets;
    }

    // each achievement's set: the progress grouping puts the ones of every set in its shared buckets (recently unlocked, ...), the lock
    // state grouping splits all of them by set
    static std::unordered_map<uint32_t, uint32_t> SetOfEach(rc_client_t* client)
    {
        std::unordered_map<uint32_t, uint32_t> sets;
        rc_client_achievement_list_t* list = rc_client_create_achievement_list(client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
        if (!list)
            return sets;
        for (uint32_t b = 0; b < list->num_buckets; b++)
        {
            const rc_client_achievement_bucket_t& bucket = list->buckets[b];
            for (uint32_t i = 0; i < bucket.num_achievements && sets.size() < MAX_ACHIEVEMENTS; i++)
            {
                if (const rc_client_achievement_t* a = bucket.achievements[i])
                    sets[a->id] = bucket.subset_id;
            }
        }
        rc_client_destroy_achievement_list(list);
        return sets;
    }

    void Rebuild(App& app)
    {
        Built = true;
        Seen = app.AchievementChanges();
        Rows.clear();
        Sets.clear();
        TabLabels.clear();
        Unlocked = Total = Points = TotalPoints = 0;
        Frontend::Achievements* ra = app.GetHost().Achievements;
        rc_client_t* client = ra ? ra->Client() : nullptr;
        if (!client)
            return;
        Session = ra->GetSession();
        Hardcore = ra->IsHardcoreSession();
        // the lists point into the game, which a logout mid-game frees on the emulator thread: it stays until they're copied and destroyed
        std::unique_lock game = ra->HoldGame();
        std::vector<std::pair<uint32_t, std::string>> sets = ListSets(client, Session.GameId);
        bool split = sets.size() > 1;
        std::unordered_map<uint32_t, uint32_t> setOf;
        if (split)
            setOf = SetOfEach(client);

        std::vector<Placed> placed;
        rc_client_achievement_list_t* list = rc_client_create_achievement_list(client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        if (!list)
            return;
        for (uint32_t b = 0; b < list->num_buckets; b++)
        {
            const rc_client_achievement_bucket_t& bucket = list->buckets[b];
            for (uint32_t i = 0; i < bucket.num_achievements && Rows.size() < MAX_ACHIEVEMENTS; i++)
            {
                const rc_client_achievement_t* a = bucket.achievements[i];
                if (!a)
                    continue;
                Row row;
                row.Id = a->id;
                row.Points = a->points;
                row.Title = Frontend::TitleWithoutTag(Text(a->title));
                row.Description = Text(a->description);
                row.Locked = bucket.bucket_type != RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED
                    && bucket.bucket_type != RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED
                    && bucket.bucket_type != RC_CLIENT_ACHIEVEMENT_BUCKET_UNSYNCED;
                row.BadgeUrl = Text(row.Locked && a->badge_locked_url ? a->badge_locked_url : a->badge_url);
                row.Progress = std::string(a->measured_progress, strnlen(a->measured_progress, sizeof(a->measured_progress)));
                row.Percent = std::clamp(a->measured_percent, 0.f, 100.f);
                row.Missable = a->type == RC_CLIENT_ACHIEVEMENT_TYPE_MISSABLE;
                Total++;
                TotalPoints += row.Points;
                if (!row.Locked)
                {
                    Unlocked++;
                    Points += row.Points;
                }
                uint32_t subset = 0;
                if (split)
                {
                    auto found = setOf.find(row.Id);
                    subset = bucket.subset_id ? bucket.subset_id : found != setOf.end() ? found->second : 0;
                }
                placed.push_back({Rows.size(), bucket.bucket_type, subset});
                Rows.push_back(std::move(row));
            }
        }
        rc_client_destroy_achievement_list(list);
        game.unlock();

        if (!split)
            sets.assign(1, {0, ""});
        for (const auto& [id, title] : sets)
        {
            Set set;
            set.Id = id;
            set.Title = title.empty() ? Session.Title : title;
            for (const auto& kind : SECTIONS)
            {
                Section section;
                section.Label = kind.Label;
                for (const Placed& at : placed)
                {
                    if (at.Bucket == kind.Bucket && (!split || at.Subset == id))
                        section.Rows.push_back(at.Row);
                }
                if (!section.Rows.empty())
                    set.Sections.push_back(std::move(section));
            }
            Sets.push_back(std::move(set));
        }
        for (const Set& set : Sets)
            TabLabels.push_back(set.Title.c_str());
        TabIndex = std::clamp(TabIndex, 0, std::max(0, (int)Sets.size() - 1));
        Counts = std::to_string(Unlocked) + " of " + std::to_string(Total) + " unlocked \xc2\xb7 " + std::to_string(Points) + " of "
            + std::to_string(TotalPoints) + " points";
        printf("ui: achievements: %u of %u unlocked, %zu set%s\n", Unlocked, Total, Sets.size(), Sets.size() == 1 ? "" : "s");
        fflush(stdout);
    }

    // the back arrow (true when it's clicked), the game's icon and title, the counts and the mode, in ScreenHeader's 88
    bool DrawHeader(App& app, ImVec2 win, ImVec2 size)
    {
        const Palette& p = app.Colors();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float height = app.Px(88);
        float x = win.x + app.Px(36);
        // clickable, but not a stop for the controller's focus, which B covers
        float d = app.Px(48);
        ImGui::SetCursorScreenPos(ImVec2(x, win.y + (height - d) / 2));
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        bool back = ImGui::InvisibleButton("##back", ImVec2(d, d));
        ImGui::PopItemFlag();
        ImVec2 c(x + d / 2, win.y + height / 2);
        if (ImGui::IsItemHovered())
            draw->AddCircleFilled(c, d / 2, WithAlpha(p.OnSurface, 0.08f));
        IconAt(app, Icon::ArrowBack, 28, c, p.OnSurface);
        x += d + app.Px(12);

        float icon = app.Px(48);
        DrawBadge(app, draw, ImVec2(x, win.y + (height - icon) / 2), icon, app.Px(12), Session.IconUrl, Session.GameId);
        x += icon + app.Px(16);
        float textW = win.x + size.x - app.Px(40) - x;
        std::string title = Ellipsize(app, app.Medium, 22, Session.Title, textW);
        ImVec2 ts = TextSize(app, app.Medium, 22, title.c_str());
        ImVec2 cs = TextSize(app, app.Regular, 15, Counts.c_str());
        float chipH = app.Px(22);
        float lineH = std::max(cs.y, chipH);
        float y = win.y + (height - ts.y - app.Px(4) - lineH) / 2;
        TextAt(app, app.Medium, 22, ImVec2(x, y), p.OnSurface, title.c_str());
        y += ts.y + app.Px(4);
        TextAt(app, app.Regular, 15, ImVec2(x, y + (lineH - cs.y) / 2), p.OnSurfaceVariant, Counts.c_str());
        Chip(app, draw, ImVec2(x + cs.x + app.Px(12), y + (lineH - chipH) / 2), Hardcore ? "Hardcore" : "Softcore",
            Hardcore ? p.PrimaryContainer : p.SurfaceContainerHigh, Hardcore ? p.OnPrimaryContainer : p.OnSurfaceVariant);
        return back;
    }

    void DrawSections(App& app, Set& set)
    {
        float rowH = app.Px(96), spacing = app.Px(4);
        bool firstRow = true;
        for (Section& section : set.Sections)
        {
            SectionLabel(app, section.Label);
            ImGuiListClipper clipper;
            clipper.Begin((int)section.Rows.size(), rowH + spacing);
            if (firstRow && WantFocus)
                clipper.IncludeItemByIndex(0);
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                    DrawRow(app, Rows[section.Rows[i]], rowH, firstRow && i == 0);
            }
            firstRow = false;
        }
    }

    void DrawRow(App& app, Row& row, float rowH, bool isDefault)
    {
        const Palette& p = app.Colors();
        ImGui::PushID((int)row.Id);
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImVec2 size(ImGui::GetContentRegionAvail().x, rowH);
        bool chosen = ImGui::InvisibleButton("##achievement", size, ImGuiButtonFlags_EnableNav);
        app.TrackFocus(isDefault);
        bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImGui::PopID();
        if (chosen)
            app.OpenUrl("https://retroachievements.org/achievement/" + std::to_string(row.Id));

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 max(at.x + size.x, at.y + size.y);
        float radius = app.Px(16);
        ImU32 bg = focused ? p.SecondaryContainer : held ? WithAlpha(p.OnSurface, 0.12f) : hovered ? WithAlpha(p.OnSurface, 0.08f) : 0;
        if (bg)
            draw->AddRectFilled(at, max, bg, radius);
        if (focused)
            FocusRing(app, draw, at, max, radius);
        ImU32 on = focused ? p.OnSecondaryContainer : p.OnSurface;
        ImU32 sub = focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;

        float badge = app.Px(48);
        DrawBadge(app, draw, ImVec2(at.x + app.Px(16), at.y + (rowH - badge) / 2), badge, app.Px(8), row.BadgeUrl);

        // the points, on the right: the number over "PTS", as the app's
        float pointsW = app.Px(64);
        std::string points = std::to_string(row.Points);
        ImVec2 ps = TextSize(app, app.Medium, 19, points.c_str()), ls = TextSize(app, app.Regular, 13, "PTS");
        float py = at.y + (rowH - ps.y - ls.y) / 2;
        float pc = max.x - app.Px(16) - pointsW / 2;
        TextAt(app, app.Medium, 19, ImVec2(pc - ps.x / 2, py), on, points.c_str());
        TextAt(app, app.Regular, 13, ImVec2(pc - ls.x / 2, py + ps.y), sub, "PTS");

        float x = at.x + app.Px(16) + badge + app.Px(16);
        float textW = max.x - app.Px(16) - pointsW - app.Px(12) - x;
        float chipW = row.Missable ? TextSize(app, app.Regular, 13, "Missable").x + app.Px(12) + app.Px(8) : 0;
        if (row.FittedWidth != textW)
        {
            row.FittedWidth = textW;
            row.TitleFit = Ellipsize(app, app.Medium, 19, row.Title, std::max(0.f, textW - chipW));
            row.DescriptionFit = WrapLines(app, app.Regular, 15, row.Description, std::max(1.f, textW), 2);
        }
        bool progress = row.Locked && !row.Progress.empty();
        float titleH = TextSize(app, app.Medium, 19, "Ag").y, lineH = TextSize(app, app.Regular, 15, "Ag").y;
        float barLineH = TextSize(app, app.Regular, 13, "Ag").y;
        float blockH = titleH + app.Px(2) + lineH * row.DescriptionFit.size() + (progress ? app.Px(6) + barLineH : 0);
        float y = at.y + (rowH - blockH) / 2;
        ImVec2 tsz = TextAt(app, app.Medium, 19, ImVec2(x, y), on, row.TitleFit.c_str());
        if (row.Missable)
            Chip(app, draw, ImVec2(x + tsz.x + app.Px(8), y + (titleH - app.Px(22)) / 2), "Missable",
                focused ? Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.12f) : p.SurfaceContainerHigh, sub);
        y += titleH + app.Px(2);
        for (const std::string& line : row.DescriptionFit)
        {
            TextAt(app, app.Regular, 15, ImVec2(x, y), sub, line.c_str());
            y += lineH;
        }
        if (progress)
        {
            // the measured progress: a bar and its text ("12/50"), as the app's
            y += app.Px(6);
            ImVec2 ms = TextSize(app, app.Regular, 13, row.Progress.c_str());
            float barW = std::max(0.f, std::min(app.Px(200), textW - ms.x - app.Px(10)));
            float h = app.Px(2), cy = y + barLineH / 2;
            ImU32 track = focused ? WithAlpha(p.OnSecondaryContainer, 0.25f) : p.SecondaryContainer;
            ImU32 fill = focused ? p.OnSecondaryContainer : p.Primary;
            draw->AddRectFilled(ImVec2(x, cy - h), ImVec2(x + barW, cy + h), track, h);
            draw->AddRectFilled(ImVec2(x, cy - h), ImVec2(x + barW * row.Percent / 100.f, cy + h), fill, h);
            TextAt(app, app.Regular, 13, ImVec2(x + barW + app.Px(10), y), sub, row.Progress.c_str());
        }
    }

    bool Built = false;
    uint32_t Seen = 0;
    Frontend::Achievements::Session Session;
    bool Hardcore = false;
    std::vector<Row> Rows;
    std::vector<Set> Sets;
    std::vector<const char*> TabLabels;
    int TabIndex = 0;
    bool ResetScroll = false;
    uint32_t Unlocked = 0, Total = 0, Points = 0, TotalPoints = 0;
    std::string Counts;
};

}

bool HasAchievementList(Host& host)
{
    Frontend::Achievements* ra = host.Achievements;
    rc_client_t* client = ra ? ra->Client() : nullptr;
    if (!client)
        return false;
    Frontend::Achievements::Session session = ra->GetSession();
    return session.Active && session.Loaded && rc_client_has_achievements(client);
}

std::unique_ptr<Screen> MakeAchievementsScreen()
{
    return std::make_unique<AchievementsScreen>();
}

}
