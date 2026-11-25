#include "AchievementsLogic.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontend
{

namespace
{

// the form fields of [body], in order ("r=ping", "u=name", ...)
std::vector<std::string> SplitFields(const std::string& body)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= body.size())
    {
        size_t end = body.find('&', start);
        if (end == std::string::npos)
            end = body.size();
        fields.push_back(body.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}

std::string Text(const char* text)
{
    return text ? text : "";
}

void CopyAchievement(const rc_client_achievement_t* achievement, AchievementEvent& out)
{
    if (!achievement)
        return;
    out.Id = achievement->id;
    out.Title = Text(achievement->title);
    out.Description = Text(achievement->description);
    out.ImageUrl = Text(achievement->badge_url);
    out.Points = achievement->points;
    out.Progress = std::string(achievement->measured_progress, strnlen(achievement->measured_progress, sizeof(achievement->measured_progress)));
    out.Percent = achievement->measured_percent;
}

void CopyLeaderboard(const rc_client_leaderboard_t* leaderboard, AchievementEvent& out)
{
    if (!leaderboard)
        return;
    out.Id = leaderboard->id;
    out.Title = Text(leaderboard->title);
    out.Description = Text(leaderboard->description);
    out.Display = Text(leaderboard->tracker_value);
}

std::string Display(const char* text, size_t size)
{
    return std::string(text, strnlen(text, size));
}

// rc_client's event type as ours; false for one it doesn't know
bool MapType(uint32_t type, AchievementEvent::Type& out)
{
    using T = AchievementEvent::Type;
    switch (type)
    {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: out = T::AchievementTriggered; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED: out = T::LeaderboardStarted; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED: out = T::LeaderboardFailed; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED: out = T::LeaderboardSubmitted; return true;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW: out = T::ChallengeIndicatorShow; return true;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE: out = T::ChallengeIndicatorHide; return true;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW: out = T::ProgressIndicatorShow; return true;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE: out = T::ProgressIndicatorHide; return true;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE: out = T::ProgressIndicatorUpdate; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW: out = T::LeaderboardTrackerShow; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE: out = T::LeaderboardTrackerHide; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE: out = T::LeaderboardTrackerUpdate; return true;
    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD: out = T::LeaderboardScoreboard; return true;
    case RC_CLIENT_EVENT_RESET: out = T::Reset; return true;
    case RC_CLIENT_EVENT_GAME_COMPLETED: out = T::GameCompleted; return true;
    case RC_CLIENT_EVENT_SERVER_ERROR: out = T::ServerError; return true;
    case RC_CLIENT_EVENT_DISCONNECTED: out = T::Disconnected; return true;
    case RC_CLIENT_EVENT_RECONNECTED: out = T::Reconnected; return true;
    case RC_CLIENT_EVENT_SUBSET_COMPLETED: out = T::SubsetCompleted; return true;
    default: return false;
    }
}

}

uint32_t ReadNdsMemory(const NdsMemoryView& view, uint32_t address, uint8_t* buffer, uint32_t numBytes)
{
    const uint8_t* region;
    uint32_t mask;
    switch (address & 0xFF000000u)
    {
    case 0x00000000u:
        region = view.MainRam;
        mask = view.MainRamMask;
        break;
    case 0x01000000u:
        region = view.Dtcm;
        mask = view.DtcmMask;
        break;
    default:
        return 0;
    }
    if (!region || !buffer)
        return 0;
    // byte by byte with the mask on each, so a read past the region's end wraps into it, as PeekMemory reads
    for (uint32_t i = 0; i < numBytes; i++)
        buffer[i] = region[(address + i) & mask];
    return numBytes;
}

std::string StripPingRichPresence(const std::string& body)
{
    std::vector<std::string> fields = SplitFields(body);
    bool ping = false;
    for (const std::string& field : fields)
        ping |= field == "r=ping";
    if (!ping)
        return body;
    std::string out;
    for (const std::string& field : fields)
    {
        if (field.rfind("m=", 0) == 0)
            continue;
        if (!out.empty())
            out += '&';
        out += field;
    }
    return out;
}

std::string RequestApi(const std::string& body)
{
    for (const std::string& field : SplitFields(body))
    {
        if (field.rfind("r=", 0) == 0)
            return field.substr(2);
    }
    return "";
}

bool CopyClientEvent(const rc_client_event_t& event, uint32_t frame, AchievementEvent& out)
{
    out = AchievementEvent();
    if (!MapType(event.type, out.Kind))
        return false;
    out.Frame = frame;
    CopyAchievement(event.achievement, out);
    CopyLeaderboard(event.leaderboard, out);
    if (event.leaderboard_tracker)
    {
        out.Id = event.leaderboard_tracker->id;
        out.Display = Display(event.leaderboard_tracker->display, sizeof(event.leaderboard_tracker->display));
    }
    if (const rc_client_leaderboard_scoreboard_t* board = event.leaderboard_scoreboard)
    {
        out.Id = board->leaderboard_id;
        out.Display = Display(board->submitted_score, sizeof(board->submitted_score));
        out.Best = Display(board->best_score, sizeof(board->best_score));
        out.Rank = board->new_rank;
        out.Entries = board->num_entries;
    }
    if (const rc_client_server_error_t* error = event.server_error)
    {
        out.Id = error->related_id;
        out.Api = Text(error->api);
        out.Message = Text(error->error_message);
        out.Result = error->result;
    }
    if (const rc_client_subset_t* subset = event.subset)
    {
        out.Id = subset->id;
        out.Title = Text(subset->title);
        out.ImageUrl = Text(subset->badge_url);
    }
    return true;
}

const char* EventTypeName(AchievementEvent::Type type)
{
    using T = AchievementEvent::Type;
    switch (type)
    {
    case T::AchievementTriggered: return "achievement triggered";
    case T::LeaderboardStarted: return "leaderboard started";
    case T::LeaderboardFailed: return "leaderboard failed";
    case T::LeaderboardSubmitted: return "leaderboard submitted";
    case T::ChallengeIndicatorShow: return "challenge indicator shown";
    case T::ChallengeIndicatorHide: return "challenge indicator hidden";
    case T::ProgressIndicatorShow: return "progress indicator shown";
    case T::ProgressIndicatorHide: return "progress indicator hidden";
    case T::ProgressIndicatorUpdate: return "progress indicator updated";
    case T::LeaderboardTrackerShow: return "tracker shown";
    case T::LeaderboardTrackerHide: return "tracker hidden";
    case T::LeaderboardTrackerUpdate: return "tracker updated";
    case T::LeaderboardScoreboard: return "scoreboard";
    case T::Reset: return "reset requested";
    case T::GameCompleted: return "game completed";
    case T::SubsetCompleted: return "subset completed";
    case T::ServerError: return "server error";
    case T::Disconnected: return "disconnected";
    case T::Reconnected: return "reconnected";
    case T::LoginSucceeded: return "logged in";
    case T::LoginFailed: return "login failed";
    case T::LoginExpired: return "login expired";
    case T::GameLoaded: return "game loaded";
    case T::GameHasNoAchievements: return "game has no achievements";
    case T::GameLoadFailed: return "game load failed";
    }
    return "?";
}

std::string DescribeEvent(const AchievementEvent& e)
{
    using T = AchievementEvent::Type;
    char text[512];
    const char* name = EventTypeName(e.Kind);
    switch (e.Kind)
    {
    case T::AchievementTriggered:
        snprintf(text, sizeof(text), "%s %u at frame %u: %s (%u points)", name, e.Id, e.Frame, e.Title.c_str(), e.Points);
        break;
    case T::ProgressIndicatorShow:
    case T::ProgressIndicatorUpdate:
        snprintf(text, sizeof(text), "%s %u at frame %u: %s %s", name, e.Id, e.Frame, e.Title.c_str(), e.Progress.c_str());
        break;
    case T::LeaderboardSubmitted:
    case T::LeaderboardTrackerShow:
    case T::LeaderboardTrackerUpdate:
        snprintf(text, sizeof(text), "%s %u at frame %u: %s %s", name, e.Id, e.Frame, e.Title.c_str(), e.Display.c_str());
        break;
    case T::LeaderboardScoreboard:
        snprintf(text, sizeof(text), "%s %u: %s, best %s, rank %u of %u", name, e.Id, e.Display.c_str(), e.Best.c_str(), e.Rank, e.Entries);
        break;
    case T::ServerError:
        snprintf(text, sizeof(text), "%s (%s, %u): %s", name, e.Api.c_str(), e.Id, e.Message.c_str());
        break;
    case T::LoginSucceeded:
        snprintf(text, sizeof(text), "%s as %s (%u points, %u softcore)", name, e.Title.c_str(), e.Points, e.SoftcorePoints);
        break;
    case T::LoginFailed:
    case T::GameLoadFailed:
        snprintf(text, sizeof(text), "%s: %s", name, e.Message.c_str());
        break;
    case T::GameLoaded:
        snprintf(text, sizeof(text), "%s %u at frame %u: %s, %u of %u unlocked (%u of %u points), %s", name, e.Id, e.Frame, e.Title.c_str(),
            e.Unlocked, e.Total, e.Points, e.TotalPoints, e.Hardcore ? "hardcore" : "softcore");
        break;
    default:
        if (e.Title.empty())
            snprintf(text, sizeof(text), "%s %u at frame %u", name, e.Id, e.Frame);
        else
            snprintf(text, sizeof(text), "%s %u at frame %u: %s", name, e.Id, e.Frame, e.Title.c_str());
        break;
    }
    return text;
}

}
