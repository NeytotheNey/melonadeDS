// The parts of the RetroAchievements client (Achievements.h) with no threads, network or emulator in them, so the tests can run them: the
// console's memory as rc_client reads it, the ping's rich presence shim, and the copies of rc_client's events handed to the UI thread
#pragma once

#include <cstdint>
#include <string>

#include "rc_client.h"

namespace Frontend
{

// The memory rc_client can read, as consoleinfo.c maps the DS: 0x00xxxxxx main RAM (4 MB on a DS, 16 MB on a DSi, wrapped by its mask),
// 0x01xxxxxx the ARM9's data TCM (wrapped by its size), nothing else
struct NdsMemoryView
{
    const uint8_t* MainRam = nullptr;
    uint32_t MainRamMask = 0;
    const uint8_t* Dtcm = nullptr;
    uint32_t DtcmMask = 0;
};

// rc_client's read: [numBytes] bytes from [address] into [buffer], each byte's address wrapped by its region's mask (so a read never
// leaves the region, as RetroAchievementsManager::PeekMemory does), in the console's order (little-endian). The bytes read, or 0 for an
// address outside both regions or a view without them
uint32_t ReadNdsMemory(const NdsMemoryView& view, uint32_t address, uint8_t* buffer, uint32_t numBytes);

// [body] (a dorequest.php POST's form fields) without its rich presence (the m= field) when it's a ping (r=ping); any other request as it is.
// rc_client has no switch for the rich presence, so a softcore session with it off sends its pings through this
std::string StripPingRichPresence(const std::string& body);
// the r= field of [body] ("login2", "ping", ...), for the log: never the rest, which has the token or the password
std::string RequestApi(const std::string& body);

// What the UI thread hears about, copied out of rc_client's events (so no pointer of rc_client's crosses threads) or made by Achievements
// itself. The fields that don't apply to a type stay empty or 0
struct AchievementEvent
{
    enum class Type
    {
        // rc_client's events (RC_CLIENT_EVENT_*)
        AchievementTriggered,      // Id, Title, Description, ImageUrl (the badge), Points
        LeaderboardStarted,        // Id, Title, Description
        LeaderboardFailed,         // Id, Title
        LeaderboardSubmitted,      // Id, Title, Display (the score sent)
        ChallengeIndicatorShow,    // Id, ImageUrl
        ChallengeIndicatorHide,    // Id
        ProgressIndicatorShow,     // Id, Title, ImageUrl, Progress, Percent
        ProgressIndicatorHide,
        ProgressIndicatorUpdate,   // as Show
        LeaderboardTrackerShow,    // Id (the tracker's), Display
        LeaderboardTrackerHide,    // Id
        LeaderboardTrackerUpdate,  // Id, Display
        LeaderboardScoreboard,     // Id (the leaderboard's), Display (the score sent), Best, Rank, Entries
        Reset,                     // asks for the console to be reset (hardcore turned on mid-game; never asked for here)
        GameCompleted,             // Title (the game's)
        SubsetCompleted,           // Id, Title, ImageUrl
        ServerError,               // Id (the related achievement or leaderboard), Api, Message, Result
        Disconnected,              // an unlock couldn't be sent and waits (Achievements::HasPendingUnlocks)
        Reconnected,               // every waiting unlock was sent
        // Achievements' own
        LoginSucceeded,            // Title (the display name), Points (hardcore score), SoftcorePoints
        LoginFailed,               // Message
        LoginExpired,              // the saved token isn't valid any more: logged out, the user name kept
        GameLoaded,                // Id (the game), Title, ImageUrl (its icon), Unlocked, Total, Points (unlocked), TotalPoints, Hardcore
        GameHasNoAchievements,     // Id, Title (the game is known, or not, but has nothing to unlock)
        GameLoadFailed,            // Message, Result
    };

    Type Kind = Type::Reset;
    uint32_t Frame = 0;     // the emulator frame it came at (the last frame run, for one that came with a server reply)
    uint32_t Id = 0;
    std::string Title, Description, ImageUrl;
    std::string Progress;   // the measured progress ("12/50", "24%")
    float Percent = 0;
    std::string Display;    // a tracker's or a submitted score's text
    std::string Best;       // the scoreboard's best score
    uint32_t Rank = 0, Entries = 0;
    uint32_t Points = 0, SoftcorePoints = 0, TotalPoints = 0;
    uint32_t Unlocked = 0, Total = 0;
    bool Hardcore = false;
    std::string Api, Message;
    int Result = 0;
};

// The file a badge or icon at [url] is cached as (BadgeCache.h): the URL's last path segment without its query, characters other than
// A-Z a-z 0-9 - _ . made _, at most 100 bytes, and only with an image's extension (png, jpg, jpeg, gif, webp); empty for anything else
std::string BadgeFileName(const std::string& url);
// an achievement's title as the popups show it: without a trailing "[m]" (the missable tag some sets put there) and the spaces before it
std::string TitleWithoutTag(const std::string& title);

// [event] copied, with [frame]; false for a type it doesn't know (rc_client's newer ones)
bool CopyClientEvent(const rc_client_event_t& event, uint32_t frame, AchievementEvent& out);
// the type's name ("achievement triggered"), and one line about [event] for the log
const char* EventTypeName(AchievementEvent::Type type);
std::string DescribeEvent(const AchievementEvent& event);

}
