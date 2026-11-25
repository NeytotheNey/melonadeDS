// LuaSunRequestsTest.kt, case for case, over the desktop's Sun (render/PresentFrame.h), whose direction and up are arrays
#include <string>

#include "Test.h"
#include "lua/requests/LuaEffectRequests.h"

namespace Frontend
{

// in Frontend for Tests::Show(std::optional) to find
static std::string Show(const Sun& s)
{
    auto vector = [](const std::array<float, 3>& v) {
        return "(" + Tests::Show(v[0]) + ", " + Tests::Show(v[1]) + ", " + Tests::Show(v[2]) + ")";
    };
    return "Sun(" + Tests::Show(s.Strength) + ", " + vector(s.Direction) + ", " + vector(s.Up) + ", " + Tests::Show(s.Length) + ", " +
        Tests::Show(s.Steps) + ", " + Tests::Show(s.Bias) + ", " + Tests::Show(s.Softness) + ", " + Tests::Show(static_cast<int>(s.Color)) +
        ", " + Tests::Show(s.Near) + ")";
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
Sun With(Sun value, F change)
{
    change(value);
    return value;
}

const Sun SUN_A {.Strength = 0.5f};
const Sun SUN_B {.Strength = 0.8f, .Direction = {0.6f, 0.8f, 0.f}, .Up = {0.f, 1.f, 0.f}, .Length = 64.f, .Steps = 32, .Bias = 2.f,
    .Softness = 3.f, .Color = 0x102030, .Near = 70.f};
const Sun OFF {.Strength = 0.f};

}

TEST(LuaSunRequests, theNewestRequestWinsAndStoppingFallsBack)
{
    LuaSunRequests requests;

    CHECK_EQ(SUN_A, requests.OnScriptRequest(0, SUN_A));
    CHECK_EQ(SUN_B, requests.OnScriptRequest(1, SUN_B));

    CHECK_EQ(SUN_A, requests.OnScriptStopped(1));
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaSunRequests, stoppingAnOlderScriptKeepsTheNewestRequest)
{
    LuaSunRequests requests;
    requests.OnScriptRequest(0, SUN_A);
    requests.OnScriptRequest(1, SUN_B);

    CHECK_EQ(SUN_B, requests.OnScriptStopped(0));
}

TEST(LuaSunRequests, aRepeatedRequestBecomesTheMostRecent)
{
    LuaSunRequests requests;
    requests.OnScriptRequest(0, SUN_A);
    requests.OnScriptRequest(1, SUN_B);

    CHECK_EQ(SUN_A, requests.OnScriptRequest(0, SUN_A));
}

TEST(LuaSunRequests, askingForNoneDropsOnlyThatScriptsRequest)
{
    LuaSunRequests requests;
    requests.OnScriptRequest(0, SUN_A);
    requests.OnScriptRequest(1, SUN_B);

    CHECK_EQ(SUN_A, requests.OnScriptRequest(1, OFF));
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

TEST(LuaSunRequests, aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaSunRequests requests;
    requests.OnScriptStopped(0);

    CHECK_NONE(requests.OnScriptRequest(0, SUN_A));

    requests.OnScriptStarted(0);
    CHECK_EQ(SUN_A, requests.OnScriptRequest(0, SUN_A));
}

TEST(LuaSunRequests, sessionEndForgetsRequestsAndStoppedScripts)
{
    LuaSunRequests requests;
    requests.OnScriptRequest(0, SUN_A);
    requests.OnScriptStopped(1);

    CHECK_NONE(requests.OnSessionEnded());
    CHECK_NONE(requests.Current());
    CHECK_EQ(SUN_B, requests.OnScriptRequest(1, SUN_B));
}

TEST(LuaSunRequests, sunShadowsNeedStrengthStepsAndLength)
{
    CHECK(!SUN_A.IsOff());
    CHECK(With(SUN_A, [](Sun& s) { s.Strength = 0.f; }).IsOff());
    CHECK(With(SUN_A, [](Sun& s) { s.Steps = 0; }).IsOff());
    CHECK(With(SUN_A, [](Sun& s) { s.Length = 0.f; }).IsOff());
    CHECK(OFF.IsOff());

    LuaSunRequests requests;
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

// both the same-frame cache and the flow to the renderers tell changes apart by equality. up counts too: the march doesn't use it, but the
// characters' shadows will
TEST(LuaSunRequests, everyValueCountsAsAChange)
{
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Strength = 0.6f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Direction[0] = 0.5f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Direction[1] = 0.7f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Direction[2] = 0.1f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Up[0] = 0.1f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Up[1] = 0.9f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Up[2] = 0.1f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Length = 96.f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Steps = 64; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Bias = 1.f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Softness = 1.f; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Color = 0x102031; })));
    CHECK(!(SUN_B == With(SUN_B, [](Sun& s) { s.Near = 0.f; })));
    CHECK_EQ(SUN_B, With(SUN_B, [](Sun&) {}));
}
