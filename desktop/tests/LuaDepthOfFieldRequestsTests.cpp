// LuaDepthOfFieldRequestsTest.kt, case for case, over the desktop's DepthOfField (render/PresentFrame.h)
#include <string>

#include "Test.h"
#include "lua/requests/LuaEffectRequests.h"

namespace Frontend
{

// in Frontend for Tests::Show(std::optional) to find
static std::string Show(const DepthOfField& d)
{
    return "DepthOfField(" + Tests::Show(d.Strength) + ", " + Tests::Show(d.Radius) + ", " + Tests::Show(d.FocusX) + ", " +
        Tests::Show(d.FocusY) + ", " + Tests::Show(d.FocusDepth) + ", " + Tests::Show(d.Range) + ", " + Tests::Show(d.Falloff) + ")";
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
DepthOfField With(DepthOfField value, F change)
{
    change(value);
    return value;
}

const DepthOfField DOF_A {.Strength = 1.f, .Radius = 4.f};
const DepthOfField DOF_B {.Strength = 0.5f, .Radius = 6.f, .FocusX = 100.f, .FocusY = 120.f, .FocusDepth = 0.4f, .Range = 0.01f, .Falloff = 0.05f};
const DepthOfField OFF {.Strength = 0.f, .Radius = 4.f};

}

TEST(LuaDepthOfFieldRequests, theNewestRequestWinsAndStoppingFallsBack)
{
    LuaDepthOfFieldRequests requests;

    CHECK_EQ(DOF_A, requests.OnScriptRequest(0, DOF_A));
    CHECK_EQ(DOF_B, requests.OnScriptRequest(1, DOF_B));

    CHECK_EQ(DOF_A, requests.OnScriptStopped(1));
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaDepthOfFieldRequests, stoppingAnOlderScriptKeepsTheNewestRequest)
{
    LuaDepthOfFieldRequests requests;
    requests.OnScriptRequest(0, DOF_A);
    requests.OnScriptRequest(1, DOF_B);

    CHECK_EQ(DOF_B, requests.OnScriptStopped(0));
}

TEST(LuaDepthOfFieldRequests, aRepeatedRequestBecomesTheMostRecent)
{
    LuaDepthOfFieldRequests requests;
    requests.OnScriptRequest(0, DOF_A);
    requests.OnScriptRequest(1, DOF_B);

    CHECK_EQ(DOF_A, requests.OnScriptRequest(0, DOF_A));
}

TEST(LuaDepthOfFieldRequests, askingForNoneDropsOnlyThatScriptsRequest)
{
    LuaDepthOfFieldRequests requests;
    requests.OnScriptRequest(0, DOF_A);
    requests.OnScriptRequest(1, DOF_B);

    CHECK_EQ(DOF_A, requests.OnScriptRequest(1, OFF));
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

TEST(LuaDepthOfFieldRequests, aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaDepthOfFieldRequests requests;
    requests.OnScriptStopped(0);

    CHECK_NONE(requests.OnScriptRequest(0, DOF_A));

    requests.OnScriptStarted(0);
    CHECK_EQ(DOF_A, requests.OnScriptRequest(0, DOF_A));
}

TEST(LuaDepthOfFieldRequests, sessionEndForgetsRequestsAndStoppedScripts)
{
    LuaDepthOfFieldRequests requests;
    requests.OnScriptRequest(0, DOF_A);
    requests.OnScriptStopped(1);

    CHECK_NONE(requests.OnSessionEnded());
    CHECK_NONE(requests.Current());
    CHECK_EQ(DOF_B, requests.OnScriptRequest(1, DOF_B));
}

TEST(LuaDepthOfFieldRequests, aBlurNeedsStrengthAndRadius)
{
    CHECK(!DOF_A.IsOff());
    CHECK(With(DOF_A, [](DepthOfField& d) { d.Strength = 0.f; }).IsOff());
    CHECK(With(DOF_A, [](DepthOfField& d) { d.Radius = 0.f; }).IsOff());
    CHECK(OFF.IsOff());

    LuaDepthOfFieldRequests requests;
    CHECK_NONE(requests.OnScriptRequest(0, With(DOF_A, [](DepthOfField& d) { d.Radius = 0.f; })));
}

// both the same-frame cache and the flow to the renderers tell changes apart by equality
TEST(LuaDepthOfFieldRequests, everyValueCountsAsAChange)
{
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.Strength = 0.6f; })));
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.Radius = 5.f; })));
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.FocusX = 101.f; })));
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.FocusY = 121.f; })));
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.FocusDepth = -1.f; })));
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.Range = 0.02f; })));
    CHECK(!(DOF_B == With(DOF_B, [](DepthOfField& d) { d.Falloff = 0.06f; })));
    CHECK_EQ(DOF_B, With(DOF_B, [](DepthOfField&) {}));
}
