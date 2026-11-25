// LuaShadeRequestsTest.kt, case for case, over the desktop's Shade (render/PresentFrame.h)
#include <string>

#include "Test.h"
#include "lua/requests/LuaEffectRequests.h"

namespace Frontend
{

// in Frontend for Tests::Show(std::optional) to find
static std::string Show(const Shade& s)
{
    return "Shade(" + Tests::Show(s.Strength) + ", " + Tests::Show(s.Radius) + ", " + Tests::Show(s.Range) + ", " +
        Tests::Show(static_cast<int>(s.Color)) + ")";
}

}

namespace Tests
{
using Frontend::Show;
}

using namespace Frontend;

namespace
{

// Kotlin's copy(): [value] with [change] made to it
template <class F>
Shade With(Shade value, F change)
{
    change(value);
    return value;
}

const Shade SHADE_A {.Strength = 0.5f};
const Shade SHADE_B {.Strength = 0.8f, .Radius = 10.f, .Range = 0.001f, .Color = 0x102030};
const Shade OFF {.Strength = 0.f};

}

TEST(LuaShadeRequests, theNewestRequestWinsAndStoppingFallsBack)
{
    LuaShadeRequests requests;

    CHECK_EQ(SHADE_A, requests.OnScriptRequest(0, SHADE_A));
    CHECK_EQ(SHADE_B, requests.OnScriptRequest(1, SHADE_B));

    CHECK_EQ(SHADE_A, requests.OnScriptStopped(1));
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaShadeRequests, stoppingAnOlderScriptKeepsTheNewestRequest)
{
    LuaShadeRequests requests;
    requests.OnScriptRequest(0, SHADE_A);
    requests.OnScriptRequest(1, SHADE_B);

    CHECK_EQ(SHADE_B, requests.OnScriptStopped(0));
}

TEST(LuaShadeRequests, aRepeatedRequestBecomesTheMostRecent)
{
    LuaShadeRequests requests;
    requests.OnScriptRequest(0, SHADE_A);
    requests.OnScriptRequest(1, SHADE_B);

    CHECK_EQ(SHADE_A, requests.OnScriptRequest(0, SHADE_A));
}

TEST(LuaShadeRequests, askingForNoneDropsOnlyThatScriptsRequest)
{
    LuaShadeRequests requests;
    requests.OnScriptRequest(0, SHADE_A);
    requests.OnScriptRequest(1, SHADE_B);

    CHECK_EQ(SHADE_A, requests.OnScriptRequest(1, OFF));
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

TEST(LuaShadeRequests, aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaShadeRequests requests;
    requests.OnScriptStopped(0);

    CHECK_NONE(requests.OnScriptRequest(0, SHADE_A));

    requests.OnScriptStarted(0);
    CHECK_EQ(SHADE_A, requests.OnScriptRequest(0, SHADE_A));
}

TEST(LuaShadeRequests, sessionEndForgetsRequestsAndStoppedScripts)
{
    LuaShadeRequests requests;
    requests.OnScriptRequest(0, SHADE_A);
    requests.OnScriptStopped(1);

    CHECK_NONE(requests.OnSessionEnded());
    CHECK_NONE(requests.Current());
    CHECK_EQ(SHADE_B, requests.OnScriptRequest(1, SHADE_B));
}

TEST(LuaShadeRequests, shadingNeedsStrength)
{
    CHECK(!SHADE_A.IsOff());
    CHECK(With(SHADE_A, [](Shade& s) { s.Strength = 0.f; }).IsOff());
    CHECK(OFF.IsOff());

    LuaShadeRequests requests;
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

// both the same-frame cache and the flow to the renderers tell changes apart by equality
TEST(LuaShadeRequests, everyValueCountsAsAChange)
{
    CHECK(!(SHADE_B == With(SHADE_B, [](Shade& s) { s.Strength = 0.6f; })));
    CHECK(!(SHADE_B == With(SHADE_B, [](Shade& s) { s.Radius = 5.f; })));
    CHECK(!(SHADE_B == With(SHADE_B, [](Shade& s) { s.Range = 0.002f; })));
    CHECK(!(SHADE_B == With(SHADE_B, [](Shade& s) { s.Color = 0x102031; })));
    CHECK_EQ(SHADE_B, With(SHADE_B, [](Shade&) {}));
}
