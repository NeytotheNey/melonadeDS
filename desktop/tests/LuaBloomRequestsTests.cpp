// LuaBloomRequestsTest.kt, case for case, over the desktop's Bloom (render/PresentFrame.h)
#include <string>

#include "Test.h"
#include "lua/requests/LuaEffectRequests.h"

namespace Frontend
{

// in Frontend for Tests::Show(std::optional) to find
static std::string Show(const Bloom& b)
{
    return "Bloom(" + Tests::Show(b.Strength) + ", " + Tests::Show(b.Threshold) + ", " + Tests::Show(b.Radius) + ", " +
        Tests::Show(b.OnlyThreeD) + ", " + Tests::Show(b.Vignette) + ", " + Tests::Show(b.VignetteSize) + ", " + Tests::Show(b.Haze) + ", " +
        Tests::Show(static_cast<int>(b.HazeColor)) + ", " + Tests::Show(b.HazeStart) + ", " + Tests::Show(b.HazeFalloff) + ", " +
        Tests::Show(b.Lights) + ", " + Tests::Show(b.LightThreshold) + ", " + Tests::Show(b.LightStrength) + ")";
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
Bloom With(Bloom value, F change)
{
    change(value);
    return value;
}

const Bloom BLOOM_A {.Strength = 1.f, .Threshold = 0.7f, .Radius = 3.f, .Vignette = 0.f, .VignetteSize = 0.6f};
const Bloom BLOOM_B {.Strength = 0.5f, .Threshold = 0.5f, .Radius = 4.f, .Vignette = 0.4f, .VignetteSize = 0.5f};
const Bloom OFF {.Strength = 0.f, .Threshold = 0.7f, .Radius = 3.f, .Vignette = 0.f, .VignetteSize = 0.6f};

}

TEST(LuaBloomRequests, theNewestRequestWinsAndStoppingFallsBack)
{
    LuaBloomRequests requests;

    CHECK_EQ(BLOOM_A, requests.OnScriptRequest(0, BLOOM_A));
    CHECK_EQ(BLOOM_B, requests.OnScriptRequest(1, BLOOM_B));

    CHECK_EQ(BLOOM_A, requests.OnScriptStopped(1));
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaBloomRequests, stoppingAnOlderScriptKeepsTheNewestRequest)
{
    LuaBloomRequests requests;
    requests.OnScriptRequest(0, BLOOM_A);
    requests.OnScriptRequest(1, BLOOM_B);

    CHECK_EQ(BLOOM_B, requests.OnScriptStopped(0));
}

TEST(LuaBloomRequests, aRepeatedRequestBecomesTheMostRecent)
{
    LuaBloomRequests requests;
    requests.OnScriptRequest(0, BLOOM_A);
    requests.OnScriptRequest(1, BLOOM_B);

    CHECK_EQ(BLOOM_A, requests.OnScriptRequest(0, BLOOM_A));
}

TEST(LuaBloomRequests, askingForNeitherDropsOnlyThatScriptsRequest)
{
    LuaBloomRequests requests;
    requests.OnScriptRequest(0, BLOOM_A);
    requests.OnScriptRequest(1, BLOOM_B);

    CHECK_EQ(BLOOM_A, requests.OnScriptRequest(1, OFF));
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

TEST(LuaBloomRequests, aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaBloomRequests requests;
    requests.OnScriptStopped(0);

    CHECK_NONE(requests.OnScriptRequest(0, BLOOM_A));

    requests.OnScriptStarted(0);
    CHECK_EQ(BLOOM_A, requests.OnScriptRequest(0, BLOOM_A));
}

TEST(LuaBloomRequests, sessionEndForgetsRequestsAndStoppedScripts)
{
    LuaBloomRequests requests;
    requests.OnScriptRequest(0, BLOOM_A);
    requests.OnScriptStopped(1);

    CHECK_NONE(requests.OnSessionEnded());
    CHECK_NONE(requests.Current());
    CHECK_EQ(BLOOM_B, requests.OnScriptRequest(1, BLOOM_B));
}

TEST(LuaBloomRequests, aGlowNeedsStrengthAndRadius)
{
    CHECK(BLOOM_A.HasBloom());
    CHECK(!With(BLOOM_A, [](Bloom& b) { b.Strength = 0.f; }).HasBloom());
    CHECK(!With(BLOOM_A, [](Bloom& b) { b.Radius = 0.f; }).HasBloom());
    CHECK(OFF.IsOff());
    CHECK(With(BLOOM_A, [](Bloom& b) { b.Radius = 0.f; }).IsOff());
}

TEST(LuaBloomRequests, aVignetteAloneKeepsItOn)
{
    Bloom vignetteOnly = With(OFF, [](Bloom& b) { b.Vignette = 0.5f; });

    CHECK(!vignetteOnly.HasBloom());
    CHECK(vignetteOnly.HasVignette());
    CHECK(!vignetteOnly.IsOff());

    LuaBloomRequests requests;
    CHECK_EQ(vignetteOnly, requests.OnScriptRequest(0, vignetteOnly));
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaBloomRequests, aHazeAloneKeepsItOn)
{
    Bloom hazeOnly = With(OFF, [](Bloom& b) { b.Haze = 0.5f; });

    CHECK(!hazeOnly.HasBloom());
    CHECK(!hazeOnly.HasVignette());
    CHECK(hazeOnly.HasHaze());
    CHECK(!hazeOnly.IsOff());
    CHECK(!OFF.HasHaze());

    LuaBloomRequests requests;
    CHECK_EQ(hazeOnly, requests.OnScriptRequest(0, hazeOnly));
    CHECK_NONE(requests.OnScriptStopped(0));
}

// the glow, vignette and haze are one request, so the newest script's replaces all three, even one that only asks for the haze
TEST(LuaBloomRequests, theNewestRequestWinsForAllThreeTogether)
{
    Bloom hazeOnly = With(OFF, [](Bloom& b) {
        b.Haze = 0.8f;
        b.HazeColor = 0x102030;
    });
    Bloom hazyB = With(BLOOM_B, [](Bloom& b) { b.Haze = 0.3f; });
    LuaBloomRequests requests;
    requests.OnScriptRequest(0, hazyB);

    CHECK_EQ(hazeOnly, requests.OnScriptRequest(1, hazeOnly));
    CHECK_EQ(hazyB, requests.OnScriptStopped(1));
}

TEST(LuaBloomRequests, onlyThreeDIsOnByDefault)
{
    CHECK(BLOOM_A.OnlyThreeD);
    // it doesn't turn anything on by itself
    CHECK(With(OFF, [](Bloom& b) { b.OnlyThreeD = false; }).IsOff());
}

TEST(LuaBloomRequests, lightsAreOffByDefault)
{
    CHECK(!BLOOM_A.Lights);
    CHECK(!BLOOM_A.HasLights());
    CHECK_NEAR(0.65f, BLOOM_A.LightThreshold, 0.f);
    CHECK_NEAR(1.f, BLOOM_A.LightStrength, 0.f);
}

TEST(LuaBloomRequests, lightsOnlyCountWithAGlow)
{
    CHECK(With(BLOOM_A, [](Bloom& b) { b.Lights = true; }).HasLights());
    CHECK(!With(BLOOM_A, [](Bloom& b) {
        b.Lights = true;
        b.Strength = 0.f;
    }).HasLights());
    CHECK(!With(BLOOM_A, [](Bloom& b) {
        b.Lights = true;
        b.Radius = 0.f;
    }).HasLights());
    // they don't turn anything on by themselves
    CHECK(With(OFF, [](Bloom& b) { b.Lights = true; }).IsOff());
    CHECK(!With(OFF, [](Bloom& b) {
        b.Lights = true;
        b.Vignette = 0.5f;
    }).HasLights());
}

// both the same-frame cache and the flow to the renderers tell changes apart by equality
TEST(LuaBloomRequests, everyValueCountsAsAChange)
{
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.Strength = 1.5f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.Threshold = 0.6f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.Radius = 4.f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.Vignette = 0.2f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.VignetteSize = 0.5f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.OnlyThreeD = false; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.Haze = 0.5f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.HazeColor = 0x102030; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.HazeStart = 0.99f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.HazeFalloff = 0.01f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.Lights = true; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.LightThreshold = 0.5f; })));
    CHECK(!(BLOOM_A == With(BLOOM_A, [](Bloom& b) { b.LightStrength = 2.f; })));
    CHECK_EQ(BLOOM_A, With(BLOOM_A, [](Bloom&) {}));
}
