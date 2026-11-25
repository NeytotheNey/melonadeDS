// The RetroAchievements client's pure parts (src/achievements/AchievementsLogic): the memory rc_client reads, as the app's
// RetroAchievementsManager::PeekMemory maps it, the ping's rich presence shim, and the events' copies
#include <cstring>
#include <string>
#include <vector>

#include "Test.h"
#include "achievements/AchievementsLogic.h"

using namespace Frontend;

namespace Tests
{

namespace
{

// a 4 MB main RAM and a 16 KB DTCM with a byte pattern each can be told apart by
struct FakeMemory
{
    std::vector<uint8_t> MainRam = std::vector<uint8_t>(4u << 20);
    std::vector<uint8_t> Dtcm = std::vector<uint8_t>(16u << 10);

    FakeMemory()
    {
        for (size_t i = 0; i < MainRam.size(); i++)
            MainRam[i] = (uint8_t)(i * 7 + 1);
        for (size_t i = 0; i < Dtcm.size(); i++)
            Dtcm[i] = (uint8_t)(i * 13 + 5);
    }

    NdsMemoryView View()
    {
        return {MainRam.data(), (uint32_t)MainRam.size() - 1, Dtcm.data(), (uint32_t)Dtcm.size() - 1};
    }
};

uint32_t LittleEndian(const uint8_t* bytes, uint32_t count)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < count; i++)
        value |= (uint32_t)bytes[i] << (i * 8);
    return value;
}

}

TEST(AchievementsMemory, mainRamReadsLittleEndianBytes)
{
    FakeMemory memory;
    uint8_t buffer[4] = {};
    CHECK(ReadNdsMemory(memory.View(), 0x00001234, buffer, 4) == 4);
    CHECK(LittleEndian(buffer, 4) == LittleEndian(&memory.MainRam[0x1234], 4));
    CHECK(ReadNdsMemory(memory.View(), 0x00001234, buffer, 1) == 1);
    CHECK(buffer[0] == memory.MainRam[0x1234]);
}

TEST(AchievementsMemory, mainRamWrapsByteByByte)
{
    FakeMemory memory;
    uint8_t buffer[4] = {};
    // the last two bytes of the 4 MB, then the first two
    CHECK(ReadNdsMemory(memory.View(), 0x003FFFFE, buffer, 4) == 4);
    CHECK(buffer[0] == memory.MainRam[0x3FFFFE]);
    CHECK(buffer[1] == memory.MainRam[0x3FFFFF]);
    CHECK(buffer[2] == memory.MainRam[0]);
    CHECK(buffer[3] == memory.MainRam[1]);
    // a DSi's 16 MB address on a DS is its mirror
    CHECK(ReadNdsMemory(memory.View(), 0x00C00010, buffer, 1) == 1);
    CHECK(buffer[0] == memory.MainRam[0x10]);
}

TEST(AchievementsMemory, dtcmIsTheSecondRegion)
{
    FakeMemory memory;
    uint8_t buffer[2] = {};
    CHECK(ReadNdsMemory(memory.View(), 0x01000020, buffer, 2) == 2);
    CHECK(buffer[0] == memory.Dtcm[0x20]);
    CHECK(buffer[1] == memory.Dtcm[0x21]);
    // wrapped by its 16 KB
    CHECK(ReadNdsMemory(memory.View(), 0x01004000, buffer, 1) == 1);
    CHECK(buffer[0] == memory.Dtcm[0]);
}

TEST(AchievementsMemory, otherRegionsAndMissingMemoryReadNothing)
{
    FakeMemory memory;
    uint8_t buffer[4] = {9, 9, 9, 9};
    CHECK(ReadNdsMemory(memory.View(), 0x02000000, buffer, 4) == 0);
    CHECK(ReadNdsMemory(memory.View(), 0xFF000000, buffer, 1) == 0);
    CHECK(ReadNdsMemory(NdsMemoryView(), 0x00000000, buffer, 4) == 0);
    CHECK(buffer[0] == 9);
}

TEST(AchievementsMemory, anySizeIsRead)
{
    FakeMemory memory;
    uint8_t buffer[16] = {};
    CHECK(ReadNdsMemory(memory.View(), 0x00000100, buffer, 16) == 16);
    CHECK(memcmp(buffer, &memory.MainRam[0x100], 16) == 0);
    CHECK(ReadNdsMemory(memory.View(), 0x00000100, buffer, 0) == 0);
}

TEST(AchievementsPing, richPresenceLeavesAPing)
{
    std::string ping = "r=ping&u=test&t=t0k3n&g=1&m=Playing+Fake+Game&h=0&x=abc";
    CHECK(StripPingRichPresence(ping) == "r=ping&u=test&t=t0k3n&g=1&h=0&x=abc");
    // at the end, and the only field after r
    CHECK(StripPingRichPresence("r=ping&g=1&m=x") == "r=ping&g=1");
    CHECK(StripPingRichPresence("r=ping&m=x") == "r=ping");
}

TEST(AchievementsPing, otherRequestsAreUntouched)
{
    std::string award = "r=awardachievement&u=test&t=t0k3n&a=1&h=0&m=abc&v=def";
    CHECK(StripPingRichPresence(award) == award);
    CHECK(StripPingRichPresence("") == "");
    // a field that only starts like the API's isn't it, and a value with m= in it isn't the field
    CHECK(StripPingRichPresence("r=pings&m=x") == "r=pings&m=x");
    CHECK(StripPingRichPresence("r=ping&x=m%3Dy") == "r=ping&x=m%3Dy");
}

TEST(AchievementsPing, apiIsTheRField)
{
    CHECK(RequestApi("r=login2&u=test&p=secret") == "login2");
    CHECK(RequestApi("u=test&r=ping") == "ping");
    CHECK(RequestApi("u=test") == "");
}

TEST(AchievementsEvents, achievementIsCopied)
{
    rc_client_achievement_t achievement {};
    std::string title = "First steps", description = "Play 300 frames", badge = "http://127.0.0.1/Badge/1.png";
    achievement.title = title.c_str();
    achievement.description = description.c_str();
    achievement.badge_url = badge.c_str();
    achievement.id = 101;
    achievement.points = 5;
    strcpy(achievement.measured_progress, "12/50");
    achievement.measured_percent = 24.0f;
    rc_client_event_t event {};
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
    event.achievement = &achievement;

    AchievementEvent copy;
    CHECK(CopyClientEvent(event, 300, copy));
    // the strings are the copy's own
    title = "changed";
    CHECK(copy.Kind == AchievementEvent::Type::AchievementTriggered);
    CHECK(copy.Frame == 300);
    CHECK(copy.Id == 101);
    CHECK(copy.Title == "First steps");
    CHECK(copy.Description == "Play 300 frames");
    CHECK(copy.ImageUrl == badge);
    CHECK(copy.Points == 5);
    CHECK(copy.Progress == "12/50");
    CHECK_NEAR(24.0f, copy.Percent, 0);
}

TEST(AchievementsEvents, trackerScoreboardAndErrorsAreCopied)
{
    rc_client_leaderboard_tracker_t tracker {};
    tracker.id = 3;
    // a display that fills its buffer, without the terminator
    memset(tracker.display, '9', sizeof(tracker.display));
    rc_client_event_t event {};
    event.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE;
    event.leaderboard_tracker = &tracker;
    AchievementEvent copy;
    CHECK(CopyClientEvent(event, 7, copy));
    CHECK(copy.Kind == AchievementEvent::Type::LeaderboardTrackerUpdate);
    CHECK(copy.Id == 3);
    CHECK(copy.Display == std::string(sizeof(tracker.display), '9'));

    rc_client_leaderboard_scoreboard_t board {};
    board.leaderboard_id = 201;
    strcpy(board.submitted_score, "0:10.00");
    strcpy(board.best_score, "0:09.50");
    board.new_rank = 2;
    board.num_entries = 40;
    event = {};
    event.type = RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD;
    event.leaderboard_scoreboard = &board;
    CHECK(CopyClientEvent(event, 8, copy));
    CHECK(copy.Kind == AchievementEvent::Type::LeaderboardScoreboard);
    CHECK(copy.Id == 201);
    CHECK(copy.Display == "0:10.00");
    CHECK(copy.Best == "0:09.50");
    CHECK(copy.Rank == 2 && copy.Entries == 40);

    rc_client_server_error_t error {};
    error.api = "award_achievement";
    error.error_message = "Unknown achievement";
    error.related_id = 102;
    error.result = RC_API_FAILURE;
    event = {};
    event.type = RC_CLIENT_EVENT_SERVER_ERROR;
    event.server_error = &error;
    CHECK(CopyClientEvent(event, 9, copy));
    CHECK(copy.Kind == AchievementEvent::Type::ServerError);
    CHECK(copy.Id == 102 && copy.Api == "award_achievement" && copy.Message == "Unknown achievement");
    CHECK(copy.Result == RC_API_FAILURE);
}

TEST(AchievementsEvents, unknownTypesAreSkipped)
{
    rc_client_event_t event {};
    event.type = 999;
    AchievementEvent copy;
    CHECK(!CopyClientEvent(event, 0, copy));
    event.type = RC_CLIENT_EVENT_DISCONNECTED;
    CHECK(CopyClientEvent(event, 0, copy));
    CHECK(copy.Kind == AchievementEvent::Type::Disconnected);
}

TEST(AchievementsBadges, fileNameComesFromTheUrl)
{
    CHECK(std::string("00101.png") == BadgeFileName("https://media.retroachievements.org/Badge/00101.png"));
    CHECK(std::string("00101_lock.png") == BadgeFileName("http://127.0.0.1:8765/Badge/00101_lock.png?x=1#y"));
    CHECK(std::string("a_b.PNG") == BadgeFileName("https://host/Images/a b.PNG"));
    CHECK(std::string("x.webp") == BadgeFileName("x.webp"));
    // what could leave the folder or isn't an image is nothing
    CHECK(std::string("") == BadgeFileName("https://host/Badge/"));
    CHECK(std::string("") == BadgeFileName("https://host/Badge/.png"));
    CHECK(std::string("") == BadgeFileName("https://host/Badge/.."));
    CHECK(std::string("") == BadgeFileName("https://host/Badge/run.exe"));
    CHECK(std::string("") == BadgeFileName("https://host/" + std::string(120, 'a') + ".png"));
    CHECK(std::string("x.png") == BadgeFileName("https://host/..\\x.png"));
}

TEST(AchievementsBadges, titleTagIsDropped)
{
    CHECK(std::string("Catch them all") == TitleWithoutTag("Catch them all [m]"));
    CHECK(std::string("Catch them all") == TitleWithoutTag("Catch them all[M]  "));
    CHECK(std::string("No tag") == TitleWithoutTag("No tag"));
    CHECK(std::string("[m]") == TitleWithoutTag("[m]"));
}

}
