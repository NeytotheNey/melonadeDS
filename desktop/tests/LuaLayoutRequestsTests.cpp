// LuaUiOverridesTest.kt's LuaLayoutRequests and getLuaLayoutRequest cases, case for case; its getPlayerLayoutPicks cases test a Kotlin flow
// helper the desktop doesn't have
#include <string>

#include "Test.h"
#include "lua/requests/LuaLayoutRequests.h"

using namespace Frontend;

namespace Tests
{

static std::string Show(const std::string& text)
{
    return "\"" + text + "\"";
}

static std::string Show(const LuaLayoutRequest& request)
{
    switch (request.Type)
    {
    case LuaLayoutRequest::Kind::PlayerLayout: return "PlayerLayout";
    case LuaLayoutRequest::Kind::Layout: return "Layout(" + request.Id + ")";
    case LuaLayoutRequest::Kind::None: return "None";
    }
    return "?";
}

}

namespace
{

const std::string LAYOUT_A = "00000000-0000-0000-0000-00000000000a";
const std::string LAYOUT_B = "00000000-0000-0000-0000-00000000000b";
const LuaLayoutRequest REQUEST_A = LuaLayoutRequest::Of(LAYOUT_A);
const LuaLayoutRequest REQUEST_B = LuaLayoutRequest::Of(LAYOUT_B);

}

TEST(LuaLayoutRequests, theNewestLayoutRequestWinsAndStoppingFallsBack)
{
    LuaLayoutRequests requests;
    CHECK_EQ(LAYOUT_A, SOME(requests.OnScriptRequest(0, REQUEST_A)));
    CHECK_EQ(LAYOUT_B, SOME(requests.OnScriptRequest(1, REQUEST_B)));

    CHECK_EQ(LAYOUT_A, SOME(requests.OnScriptStopped(1)));
    // no baseline: the player's layout (none) once no script holds one
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaLayoutRequests, aPlayerLayoutRequestDropsOnlyThatScriptsRequest)
{
    LuaLayoutRequests requests;
    requests.OnScriptRequest(0, REQUEST_A);
    requests.OnScriptRequest(1, REQUEST_B);

    CHECK_EQ(LAYOUT_A, SOME(requests.OnScriptRequest(1, LuaLayoutRequest::Player())));
    CHECK_NONE(requests.OnScriptRequest(0, LuaLayoutRequest::Player()));
}

TEST(LuaLayoutRequests, aRequestForNothingKeepsTheScriptsEarlierRequest)
{
    LuaLayoutRequests requests;
    requests.OnScriptRequest(0, REQUEST_A);
    requests.OnScriptRequest(1, REQUEST_B);

    CHECK_EQ(LAYOUT_B, SOME(requests.OnScriptRequest(1, LuaLayoutRequest::Nothing())));
    // it doesn't make script 0's request the most recent either
    CHECK_EQ(LAYOUT_B, SOME(requests.OnScriptRequest(0, LuaLayoutRequest::Nothing())));
    CHECK_EQ(LAYOUT_A, SOME(requests.OnScriptStopped(1)));
    // and it doesn't add one for a script that had none
    CHECK_NONE(requests.OnScriptStopped(0));
    CHECK_NONE(requests.OnScriptRequest(2, LuaLayoutRequest::Nothing()));
}

TEST(LuaLayoutRequests, aRepeatedLayoutRequestBecomesTheMostRecent)
{
    LuaLayoutRequests requests;
    requests.OnScriptRequest(0, REQUEST_A);
    requests.OnScriptRequest(1, REQUEST_B);
    requests.OnScriptRequest(0, REQUEST_A);

    CHECK_EQ(LAYOUT_A, SOME(requests.OnScriptStopped(2)));
    CHECK_EQ(LAYOUT_B, SOME(requests.OnScriptStopped(0)));
}

TEST(LuaLayoutRequests, thePlayerPickingALayoutDropsEveryRequest)
{
    LuaLayoutRequests requests;
    requests.OnScriptRequest(0, REQUEST_A);

    CHECK_NONE(requests.OnPlayerChanged());
    CHECK_NONE(requests.LayoutId());
    // a later request applies again
    CHECK_EQ(LAYOUT_B, SOME(requests.OnScriptRequest(0, REQUEST_B)));
}

TEST(LuaLayoutRequests, aLateLayoutRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaLayoutRequests requests;
    requests.OnScriptStopped(0);
    CHECK_NONE(requests.OnScriptRequest(0, REQUEST_A));

    requests.OnScriptStarted(0);
    CHECK_EQ(LAYOUT_A, SOME(requests.OnScriptRequest(0, REQUEST_A)));
}

TEST(LuaLayoutRequests, sessionEndForgetsLayoutRequestsAndStoppedScripts)
{
    LuaLayoutRequests requests;
    requests.OnScriptRequest(0, REQUEST_A);
    requests.OnScriptStopped(1);

    CHECK_NONE(requests.OnSessionEnded());
    CHECK_EQ(LAYOUT_B, SOME(requests.OnScriptRequest(1, REQUEST_B)));
}

TEST(LuaLayoutRequests, aLayoutRequestIsResolvedFromTheScriptsOptions)
{
    const std::vector<LuaScriptOption> options {
        LuaScriptOption::Layout("map", "Map"),
        LuaScriptOption::Layout("unset", "Unset"),
        LuaScriptOption::Layout("deleted", "Deleted"),
        LuaScriptOption::Toggle("hud", "HUD", false),
    };
    const std::map<std::string, std::string> values {{"map", LAYOUT_A}, {"unset", ""}, {"deleted", LAYOUT_B}, {"hud", "true"}};
    auto layoutExists = [](const std::string& id) { return id == LAYOUT_A; };

    CHECK_EQ(REQUEST_A, GetLuaLayoutRequest(options, values, "map", layoutExists));
    CHECK_EQ(LuaLayoutRequest::Player(), GetLuaLayoutRequest(options, values, std::nullopt, layoutExists));
    // "Don't change", a deleted layout, a key that isn't a layout option, and an unknown key ask for nothing
    CHECK_EQ(LuaLayoutRequest::Nothing(), GetLuaLayoutRequest(options, values, "unset", layoutExists));
    CHECK_EQ(LuaLayoutRequest::Nothing(), GetLuaLayoutRequest(options, values, "deleted", layoutExists));
    CHECK_EQ(LuaLayoutRequest::Nothing(), GetLuaLayoutRequest(options, values, "hud", layoutExists));
    CHECK_EQ(LuaLayoutRequest::Nothing(), GetLuaLayoutRequest(options, values, "gone", layoutExists));
    // a value that was never stored resolves to "Don't change"
    CHECK_EQ(LuaLayoutRequest::Nothing(), GetLuaLayoutRequest(options, {}, "map", layoutExists));
}

// not in the Kotlin: what the desktop's UUID check takes in place of UUID.fromString
TEST(LuaLayoutRequests, aLayoutValueIsACanonicalUuid)
{
    const std::string upper = "00000000-0000-0000-0000-00000000000A";
    const std::string noDashes = "000000000000000000000000000000000000";
    const std::string notHex = "00000000-0000-0000-0000-00000000000g";
    const std::string empty;

    CHECK_EQ(LAYOUT_A, SOME(GetLuaLayoutOptionLayoutId(&LAYOUT_A)));
    // the app's UUID.toString() is lowercase, as the desktop's layout ids are
    CHECK_EQ(LAYOUT_A, SOME(GetLuaLayoutOptionLayoutId(&upper)));
    CHECK_NONE(GetLuaLayoutOptionLayoutId(&noDashes));
    CHECK_NONE(GetLuaLayoutOptionLayoutId(&notHex));
    CHECK_NONE(GetLuaLayoutOptionLayoutId(&empty));
    CHECK_NONE(GetLuaLayoutOptionLayoutId(nullptr));
}
