// RetroAchievements over the running game, in the main window: the popups (an unlock, the game loaded, a leaderboard sent, the connection
// lost and back, ...; top center, queued, one at a time) and the indicators (the challenges, the progress and the leaderboard trackers; top
// left, while rc_client shows them). Drawn into the main viewport's background list, under any screen and its dimming, so they never take
// the input; never part of the game's frame (--screenshot), only of the window (--window-shot). The containers are kept and their text
// reused, so the frames between events allocate nothing. UI thread only
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "imgui.h"
#include "achievements/AchievementsLogic.h"

namespace Ui
{

class App;

// [url]'s image (a badge, or the game's icon with [gameId]) in [min, min + side], rounded by [rounding], at [alpha]; the trophy on a
// surface-variant square until it's downloaded and read (BadgeCache, App::ArtTexture)
void DrawBadge(App& app, ImDrawList* draw, ImVec2 min, float side, float rounding, const std::string& url, uint32_t gameId = 0,
    float alpha = 1);

class RaOverlay
{
public:
    // an event from Achievements::DrainEvents
    void OnEvent(App& app, const Frontend::AchievementEvent& event);
    // the game stopped: its indicators and the popups still to come go
    void EndSession();
    // a popup is up or waiting, or a timed indicator is shown: the window is drawn every pass meanwhile
    bool Animating() const { return !Popups.empty() || !Started.empty(); }
    // an indicator the settings let show is up: drawn with each present
    bool HasIndicators(App& app) const;
    void Draw(App& app, ImDrawList* draw);
    // bumped by the events that change the achievement list (an unlock, a game loaded, a reset)
    uint32_t Changes() const { return ChangeCount; }
    void Changed() { ChangeCount++; }

private:
    using Clock = std::chrono::steady_clock;

    struct Popup
    {
        std::string Label, Title, Body, ImageUrl;
        uint32_t GameId = 0;              // the image is the game's icon
        uint32_t LeaderboardId = 0;       // a submitted score waiting for its rank (its scoreboard event)
        bool Disconnect = false;          // "Can't reach RetroAchievements"
        std::string Score;                // that score
        Clock::time_point WaitUntil {};
        // the text fitted to the card, made again when the scale changes
        float FittedScale = 0;
        std::string LabelFit, TitleFit;
        std::vector<std::string> BodyFit;
    };

    struct Indicator
    {
        uint32_t Id = 0;
        std::string ImageUrl, Text, Detail;
        Clock::time_point Until {};       // a leaderboard start's, shown for a while
        float FittedScale = 0;
        std::string TextFit, DetailFit;
    };

    void Queue(Popup popup);
    void Scoreboard(const Frontend::AchievementEvent& event);
    void Fit(App& app, Popup& popup, float textWidth);
    // the head popup, or null while none is up (the head may still wait for its scoreboard)
    Popup* ShownPopup(Clock::time_point now);
    // returns the card's rectangle (min x, min y, max x, max y)
    ImVec4 DrawPopup(App& app, ImDrawList* draw, Popup& popup, float alpha);
    // a "Can't reach" popup not shown yet dropped (its "is back" then isn't queued either); false when there's none
    bool DropWaitingDisconnect();
    // an indicator card at [y] (window pixels): its image, or [icon] when it has none; returns the card's height
    float DrawIndicator(App& app, ImDrawList* draw, Indicator& indicator, float y, ImWchar icon);
    static void Remove(std::vector<Indicator>& list, uint32_t id);
    static Indicator& Find(std::vector<Indicator>& list, uint32_t id);

    std::deque<Popup> Popups;
    // when the head popup started showing; unset while it waits
    bool HeadShown = false;
    Clock::time_point HeadStart {};
    std::vector<Indicator> Challenges, Trackers, Started;
    bool ProgressShown = false;
    Indicator Progress;
    uint32_t ChangeCount = 0;
    // the settings' keys, made once so the per-frame lookups allocate nothing
    const std::string ChallengeKey = "ra_active_challenge_indicators";
    const std::string ProgressKey = "ra_progress_indicators";
    const std::string LeaderboardKey = "ra_leaderboard_indicators";
};

}
