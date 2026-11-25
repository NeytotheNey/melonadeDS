#include "RaOverlay.h"

#include <algorithm>

#include "imgui_internal.h"
#include "Icons.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"
#include "achievements/Achievements.h"
#include "achievements/BadgeCache.h"

namespace Ui
{

namespace
{

using Frontend::AchievementEvent;

// a popup's time on screen, its fade in and out, and how long a submitted score waits for its rank
constexpr float POPUP_SECONDS = 5.5f;
constexpr float POPUP_FADE_SECONDS = 0.15f;
constexpr auto SCOREBOARD_WAIT = std::chrono::seconds(3);
// how long "Leaderboard attempt started" stays
constexpr auto STARTED_SHOWN = std::chrono::seconds(3);

void TextOn(App& app, ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color, const std::string& text)
{
    draw->AddText(font, app.Px(size), ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y)), color, text.c_str(), text.c_str() + text.size());
}

// [text] into [out], cut to [width] with an ellipsis; a text that fits is copied into [out]'s own buffer, so a short one that changes
// every frame (a tracker) allocates nothing
void FitInto(App& app, std::string& out, ImFont* font, float size, const std::string& text, float width)
{
    if (TextSize(app, font, size, text.c_str()).x <= width)
        out.assign(text);
    else
        out = Ellipsize(app, font, size, text, width);
}

bool GameRunning(App& app)
{
    Host& host = app.GetHost();
    return host.GameRunning && host.GameRunning();
}

}

void DrawBadge(App& app, ImDrawList* draw, ImVec2 min, float side, float rounding, const std::string& url, uint32_t gameId, float alpha)
{
    ImVec2 max(min.x + side, min.y + side);
    GLuint texture = 0;
    if (Frontend::BadgeCache* cache = app.GetHost().Badges)
    {
        if (const std::string* file = cache->File(url, gameId))
        {
            int w = 0, h = 0;
            texture = app.ArtTexture(*file, 0, false, w, h);
        }
    }
    if (texture)
    {
        draw->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)texture), min, max, ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, (int)(alpha * 255)), rounding);
        return;
    }
    const Palette& p = app.Colors();
    draw->AddRectFilled(min, max, WithAlpha(p.SurfaceVariant, alpha), rounding);
    float size = side * 0.55f;
    const char* glyph = IconText(Icon::Trophy);
    ImVec2 extent = app.Icons->CalcTextSizeA(size, FLT_MAX, 0, glyph);
    draw->AddText(app.Icons, size, ImVec2(IM_ROUND(min.x + (side - extent.x) / 2), IM_ROUND(min.y + (side - extent.y) / 2)),
        WithAlpha(p.OnSurfaceVariant, alpha), glyph);
}

void RaOverlay::OnEvent(App& app, const AchievementEvent& e)
{
    using T = AchievementEvent::Type;
    Host& host = app.GetHost();
    Frontend::Achievements* ra = host.Achievements;
    if (e.Kind == T::AchievementTriggered || e.Kind == T::GameLoaded || e.Kind == T::GameHasNoAchievements || e.Kind == T::Reset)
        ChangeCount++;
    // what comes while no game runs (a token login at the start) has nothing to show over
    if (!GameRunning(app))
        return;
    Popup popup;
    popup.Label = "RetroAchievements";
    switch (e.Kind)
    {
    case T::AchievementTriggered:
        popup.Label = "Achievement unlocked";
        popup.Title = Frontend::TitleWithoutTag(e.Title);
        popup.Body = e.Description;
        popup.ImageUrl = e.ImageUrl;
        Queue(std::move(popup));
        break;
    case T::GameLoaded:
        popup.Label = e.Hardcore ? "Achievements loaded \xc2\xb7 Hardcore" : "Achievements loaded";
        popup.Title = e.Title;
        popup.Body = "Unlocked: " + std::to_string(e.Unlocked) + "/" + std::to_string(e.Total);
        popup.ImageUrl = e.ImageUrl;
        popup.GameId = e.Id;
        Queue(std::move(popup));
        break;
    case T::GameHasNoAchievements:
        popup.Title = "This game has no achievements";
        popup.ImageUrl = e.ImageUrl;
        popup.GameId = e.Id;
        Queue(std::move(popup));
        break;
    case T::GameLoadFailed:
        popup.Title = "Failed to load achievements";
        popup.Body = "Make sure you're online.";
        Queue(std::move(popup));
        break;
    case T::LoginExpired:
        popup.Title = "RetroAchievements login expired";
        popup.Body = "Log in again in Settings \xe2\x80\xba Achievements.";
        Queue(std::move(popup));
        break;
    case T::GameCompleted:
    case T::SubsetCompleted:
    {
        Frontend::Achievements::Session session = ra ? ra->GetSession() : Frontend::Achievements::Session();
        Frontend::Achievements::User user = ra ? ra->GetUser() : Frontend::Achievements::User();
        popup.Label = ra && ra->IsHardcoreSession() ? "Mastered" : "Completed";
        popup.Title = e.Kind == T::SubsetCompleted && !e.Title.empty() ? e.Title : session.Title;
        popup.Body = user.DisplayName.empty() ? user.UserName : user.DisplayName;
        popup.ImageUrl = session.IconUrl;
        popup.GameId = session.GameId;
        Queue(std::move(popup));
        break;
    }
    case T::LeaderboardSubmitted:
    {
        Frontend::Achievements::Session session = ra ? ra->GetSession() : Frontend::Achievements::Session();
        popup.Label = "Leaderboard";
        popup.Title = e.Title;
        popup.Score = e.Display;
        popup.LeaderboardId = e.Id;
        popup.WaitUntil = Clock::now() + SCOREBOARD_WAIT;
        popup.ImageUrl = session.IconUrl;
        popup.GameId = session.GameId;
        Queue(std::move(popup));
        break;
    }
    case T::LeaderboardScoreboard:
        Scoreboard(e);
        break;
    case T::LeaderboardFailed:
    {
        Frontend::Achievements::Session session = ra ? ra->GetSession() : Frontend::Achievements::Session();
        popup.Label = "Leaderboard";
        popup.Title = e.Title;
        popup.Body = "Attempt failed";
        popup.ImageUrl = session.IconUrl;
        popup.GameId = session.GameId;
        Queue(std::move(popup));
        break;
    }
    case T::Disconnected:
        popup.Title = "Can't reach RetroAchievements";
        popup.Body = "Unlocks will be sent when it's back.";
        popup.Disconnect = true;
        Queue(std::move(popup));
        break;
    case T::Reconnected:
        // back before "Can't reach" was shown: neither has anything to tell
        if (DropWaitingDisconnect())
            break;
        popup.Title = "RetroAchievements is back";
        popup.Body = "All unlocks sent.";
        Queue(std::move(popup));
        break;
    case T::ServerError:
        popup.Title = "RetroAchievements error";
        popup.Body = e.Message;
        Queue(std::move(popup));
        break;
    case T::ChallengeIndicatorShow:
    {
        Indicator& challenge = Find(Challenges, e.Id);
        challenge.ImageUrl = e.ImageUrl;
        challenge.Text = Frontend::TitleWithoutTag(e.Title);
        challenge.FittedScale = 0;
        break;
    }
    case T::ChallengeIndicatorHide:
        Remove(Challenges, e.Id);
        break;
    case T::ProgressIndicatorShow:
    case T::ProgressIndicatorUpdate:
        ProgressShown = true;
        Progress.Id = e.Id;
        Progress.ImageUrl.assign(e.ImageUrl);
        Progress.Text.assign(e.Progress);
        Progress.FittedScale = 0;
        break;
    case T::ProgressIndicatorHide:
        ProgressShown = false;
        break;
    case T::LeaderboardTrackerShow:
    case T::LeaderboardTrackerUpdate:
    {
        Indicator& tracker = Find(Trackers, e.Id);
        tracker.Text.assign(e.Display);
        tracker.FittedScale = 0;
        break;
    }
    case T::LeaderboardTrackerHide:
        Remove(Trackers, e.Id);
        break;
    case T::LeaderboardStarted:
    {
        if (host.Settings && !host.Settings->GetBool(LeaderboardKey, true))
            break;
        Indicator started;
        started.Id = e.Id;
        started.Text = "Leaderboard attempt started";
        started.Detail = e.Title;
        started.Until = Clock::now() + STARTED_SHOWN;
        Started.push_back(std::move(started));
        break;
    }
    default:
        break;
    }
}

void RaOverlay::EndSession()
{
    Popups.clear();
    HeadShown = false;
    Challenges.clear();
    Trackers.clear();
    Started.clear();
    ProgressShown = false;
    ChangeCount++;
}

bool RaOverlay::HasIndicators(App& app) const
{
    if (Challenges.empty() && Trackers.empty() && !ProgressShown)
        return false;
    Frontend::Settings* s = app.GetHost().Settings;
    auto on = [&](const std::string& key) { return !s || s->GetBool(key, true); };
    return (!Challenges.empty() && on(ChallengeKey)) || (ProgressShown && on(ProgressKey)) || (!Trackers.empty() && on(LeaderboardKey));
}

void RaOverlay::Queue(Popup popup)
{
    printf("ui: ra popup \"%s\" \"%s\"\n", popup.Label.c_str(), popup.Title.c_str());
    fflush(stdout);
    Popups.push_back(std::move(popup));
}

bool RaOverlay::DropWaitingDisconnect()
{
    for (size_t i = 0; i < Popups.size(); i++)
    {
        if (!Popups[i].Disconnect || (i == 0 && HeadShown))
            continue;
        printf("ui: ra popup \"%s\" dropped: reconnected before it showed\n", Popups[i].Title.c_str());
        fflush(stdout);
        Popups.erase(Popups.begin() + (std::ptrdiff_t)i);
        return true;
    }
    return false;
}

void RaOverlay::Scoreboard(const AchievementEvent& e)
{
    for (size_t i = 0; i < Popups.size(); i++)
    {
        Popup& popup = Popups[i];
        // the head one that's showing already says "Submitted:"
        if (popup.LeaderboardId != e.Id || (i == 0 && HeadShown))
            continue;
        popup.Body = (e.Display.empty() ? popup.Score : e.Display) + " \xe2\x80\xa2 Rank " + std::to_string(e.Rank) + " of "
            + std::to_string(e.Entries);
        popup.LeaderboardId = 0;
        popup.FittedScale = 0;
        return;
    }
}

RaOverlay::Popup* RaOverlay::ShownPopup(Clock::time_point now)
{
    while (!Popups.empty())
    {
        Popup& head = Popups.front();
        if (!HeadShown)
        {
            if (head.LeaderboardId && now < head.WaitUntil)
                return nullptr;
            if (head.LeaderboardId)
            {
                // no rank came in time
                head.Body = "Submitted: " + head.Score;
                head.LeaderboardId = 0;
                head.FittedScale = 0;
            }
            HeadShown = true;
            HeadStart = now;
        }
        if (std::chrono::duration<float>(now - HeadStart).count() < POPUP_SECONDS)
            return &head;
        Popups.pop_front();
        HeadShown = false;
    }
    return nullptr;
}

void RaOverlay::Fit(App& app, Popup& popup, float textWidth)
{
    popup.FittedScale = app.GetScale();
    popup.LabelFit = Ellipsize(app, app.Medium, 14, popup.Label, textWidth);
    popup.TitleFit = Ellipsize(app, app.Medium, 19, popup.Title, textWidth);
    popup.BodyFit = WrapLines(app, app.Regular, 15, popup.Body, textWidth, 2);
}

ImVec4 RaOverlay::DrawPopup(App& app, ImDrawList* draw, Popup& popup, float alpha)
{
    const Palette& p = app.Colors();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float w = std::min(app.Px(420), viewport->Size.x - app.Px(32));
    float pad = app.Px(12), image = app.Px(56), gap = app.Px(12);
    float textW = w - 2 * pad - image - gap;
    if (textW <= 0)
        return ImVec4(0, 0, 0, 0);
    if (popup.FittedScale != app.GetScale())
        Fit(app, popup, textW);
    float labelH = TextSize(app, app.Medium, 14, "Ag").y, titleH = TextSize(app, app.Medium, 19, "Ag").y;
    float bodyH = TextSize(app, app.Regular, 15, "Ag").y;
    float textH = labelH + app.Px(2) + titleH + (popup.BodyFit.empty() ? 0 : app.Px(2) + bodyH * popup.BodyFit.size());
    float h = 2 * pad + std::max(image, textH);
    ImVec2 min(IM_ROUND(viewport->Pos.x + (viewport->Size.x - w) / 2), IM_ROUND(viewport->Pos.y + app.Px(16)));
    ImVec2 max(min.x + w, min.y + h);
    float radius = app.Px(16);
    draw->AddRectFilled(min, max, WithAlpha(p.SurfaceContainerLow, alpha), radius);
    draw->AddRect(min, max, WithAlpha(p.OutlineVariant, alpha), radius, std::max(1.f, app.Px(1)));
    DrawBadge(app, draw, ImVec2(min.x + pad, min.y + (h - image) / 2), image, app.Px(8), popup.ImageUrl, popup.GameId, alpha);
    float x = min.x + pad + image + gap;
    float y = min.y + (h - textH) / 2;
    TextOn(app, draw, app.Medium, 14, ImVec2(x, y), WithAlpha(p.Primary, alpha), popup.LabelFit);
    y += labelH + app.Px(2);
    TextOn(app, draw, app.Medium, 19, ImVec2(x, y), WithAlpha(p.OnSurface, alpha), popup.TitleFit);
    y += titleH + app.Px(2);
    for (const std::string& line : popup.BodyFit)
    {
        TextOn(app, draw, app.Regular, 15, ImVec2(x, y), WithAlpha(p.OnSurfaceVariant, alpha), line);
        y += bodyH;
    }
    return ImVec4(min.x, min.y, max.x, max.y);
}

float RaOverlay::DrawIndicator(App& app, ImDrawList* draw, Indicator& indicator, float y, ImWchar icon)
{
    const Palette& p = app.Colors();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float pad = app.Px(8), image = app.Px(32), gap = app.Px(8);
    float maxW = std::min(app.Px(260), viewport->Size.x - app.Px(16));
    float textMax = maxW - 2 * pad - image - gap;
    if (textMax <= 0)
        return 0;
    // one line in the label's size, or two smaller ones with a detail
    bool twoLines = !indicator.Detail.empty();
    float textSize = twoLines ? 13 : icon ? 19 : 15;
    ImFont* font = app.Medium;
    if (indicator.FittedScale != app.GetScale())
    {
        indicator.FittedScale = app.GetScale();
        FitInto(app, indicator.TextFit, font, textSize, indicator.Text, textMax);
        if (twoLines)
            FitInto(app, indicator.DetailFit, app.Regular, 14, indicator.Detail, textMax);
    }
    float textW = TextSize(app, font, textSize, indicator.TextFit.c_str()).x;
    if (twoLines)
        textW = std::max(textW, TextSize(app, app.Regular, 14, indicator.DetailFit.c_str()).x);
    float w = 2 * pad + image + gap + textW;
    float h = 2 * pad + image;
    ImVec2 min(viewport->Pos.x + app.Px(8), y), max(min.x + w, min.y + h);
    float radius = app.Px(12);
    draw->AddRectFilled(min, max, p.SurfaceContainerLow, radius);
    draw->AddRect(min, max, p.OutlineVariant, radius, std::max(1.f, app.Px(1)));
    ImVec2 i0(min.x + pad, min.y + pad);
    if (icon)
    {
        const char* glyph = IconText(icon);
        float size = app.Px(24);
        ImVec2 extent = app.Icons->CalcTextSizeA(size, FLT_MAX, 0, glyph);
        draw->AddText(app.Icons, size, ImVec2(IM_ROUND(i0.x + (image - extent.x) / 2), IM_ROUND(i0.y + (image - extent.y) / 2)), p.Primary,
            glyph);
    }
    else
        DrawBadge(app, draw, i0, image, app.Px(8), indicator.ImageUrl);
    float x = i0.x + image + gap;
    if (twoLines)
    {
        float lineH = TextSize(app, font, textSize, "Ag").y, detailH = TextSize(app, app.Regular, 14, "Ag").y;
        float ty = min.y + (h - lineH - detailH) / 2;
        TextOn(app, draw, font, textSize, ImVec2(x, ty), p.Primary, indicator.TextFit);
        TextOn(app, draw, app.Regular, 14, ImVec2(x, ty + lineH), p.OnSurface, indicator.DetailFit);
    }
    else
    {
        float lineH = TextSize(app, font, textSize, "Ag").y;
        TextOn(app, draw, font, textSize, ImVec2(x, min.y + (h - lineH) / 2), p.OnSurface, indicator.TextFit);
    }
    return h;
}

void RaOverlay::Draw(App& app, ImDrawList* draw)
{
    Clock::time_point now = Clock::now();
    Started.erase(std::remove_if(Started.begin(), Started.end(), [&](const Indicator& i) { return i.Until <= now; }), Started.end());
    ImVec4 popupRect(0, 0, 0, 0);
    float popupAlpha = 0;
    if (Popup* popup = ShownPopup(now))
    {
        float t = std::chrono::duration<float>(now - HeadStart).count();
        float alpha = std::clamp(std::min(t, POPUP_SECONDS - t) / POPUP_FADE_SECONDS, 0.f, 1.f);
        popupRect = DrawPopup(app, draw, *popup, alpha);
        popupAlpha = alpha;
    }

    Frontend::Settings* s = app.GetHost().Settings;
    auto on = [&](const std::string& key) { return !s || s->GetBool(key, true); };
    // the column starts under the popup where they'd overlap (a narrow window), following its fade
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float y = viewport->Pos.y + app.Px(8);
    float columnLeft = viewport->Pos.x + app.Px(8);
    float columnRight = columnLeft + std::min(app.Px(260), viewport->Size.x - app.Px(16));
    if (popupAlpha > 0 && popupRect.x < columnRight && popupRect.z > columnLeft)
        y += (popupRect.w + app.Px(8) - y) * popupAlpha;
    float gap = app.Px(6);
    if (!Challenges.empty() && on(ChallengeKey))
    {
        for (Indicator& challenge : Challenges)
            y += DrawIndicator(app, draw, challenge, y, 0) + gap;
    }
    if (ProgressShown && on(ProgressKey))
        y += DrawIndicator(app, draw, Progress, y, 0) + gap;
    if (on(LeaderboardKey))
    {
        for (Indicator& tracker : Trackers)
            y += DrawIndicator(app, draw, tracker, y, Icon::Timer) + gap;
        for (Indicator& started : Started)
            y += DrawIndicator(app, draw, started, y, Icon::Timer) + gap;
    }
}

void RaOverlay::Remove(std::vector<Indicator>& list, uint32_t id)
{
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Indicator& i) { return i.Id == id; }), list.end());
}

RaOverlay::Indicator& RaOverlay::Find(std::vector<Indicator>& list, uint32_t id)
{
    for (Indicator& indicator : list)
    {
        if (indicator.Id == id)
            return indicator;
    }
    list.emplace_back();
    list.back().Id = id;
    return list.back();
}

}
