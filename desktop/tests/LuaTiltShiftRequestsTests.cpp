// LuaTiltShiftRequestsTest.kt, case for case, over the desktop's TiltShift (render/PresentFrame.h) and IsOffRequest for its isOff
#include <optional>
#include <string>

#include "Test.h"
#include "lua/requests/LuaEffectRequests.h"

namespace Frontend
{

// in Frontend for Tests::Show(std::optional) to find
static std::string Show(const TiltShift& t)
{
    return "TiltShift(" + Tests::Show(t.Top) + ", " + Tests::Show(t.Bottom) + ", " + Tests::Show(t.Center) + ", " + Tests::Show(t.Width) +
        ", " + Tests::Show(t.Radius) + ", " + Tests::Show(t.Saturation) + ", " + Tests::Show(t.Contrast) + ", " + Tests::Show(t.Color) + ")";
}

}

namespace Tests
{
using Frontend::Show;
}

using namespace Frontend;

namespace
{

TiltShift Make(float top, float bottom, float center, float width, float radius, float saturation = 1.f, float contrast = 1.f,
    std::optional<float> color = std::nullopt)
{
    return {top, bottom, center, width, radius, saturation, contrast, color};
}

// Kotlin's copy(): [value] with [change] made to it
template <class F>
TiltShift With(TiltShift value, F change)
{
    change(value);
    return value;
}

const TiltShift BLUR_A = Make(1.f, 1.f, 0.5f, 0.3f, 6.f);
const TiltShift BLUR_B = Make(0.5f, 0.f, 0.4f, 0.2f, 4.f);
const TiltShift OFF = Make(0.f, 0.f, 0.5f, 0.3f, 6.f);

TiltShift ColorOnly(std::optional<float> color)
{
    return Make(0.f, 0.f, 0.5f, 0.3f, 6.f, 1.3f, 1.1f, color);
}

}

TEST(LuaTiltShiftRequests, theNewestRequestWinsAndStoppingFallsBack)
{
    LuaTiltShiftRequests requests;

    CHECK_EQ(BLUR_A, requests.OnScriptRequest(0, BLUR_A));
    CHECK_EQ(BLUR_B, requests.OnScriptRequest(1, BLUR_B));

    CHECK_EQ(BLUR_A, requests.OnScriptStopped(1));
    CHECK_NONE(requests.OnScriptStopped(0));
}

TEST(LuaTiltShiftRequests, stoppingAnOlderScriptKeepsTheNewestRequest)
{
    LuaTiltShiftRequests requests;
    requests.OnScriptRequest(0, BLUR_A);
    requests.OnScriptRequest(1, BLUR_B);

    CHECK_EQ(BLUR_B, requests.OnScriptStopped(0));
}

TEST(LuaTiltShiftRequests, aRepeatedRequestBecomesTheMostRecent)
{
    LuaTiltShiftRequests requests;
    requests.OnScriptRequest(0, BLUR_A);
    requests.OnScriptRequest(1, BLUR_B);

    CHECK_EQ(BLUR_A, requests.OnScriptRequest(0, BLUR_A));
}

TEST(LuaTiltShiftRequests, askingForNoBlurDropsOnlyThatScriptsRequest)
{
    LuaTiltShiftRequests requests;
    requests.OnScriptRequest(0, BLUR_A);
    requests.OnScriptRequest(1, BLUR_B);

    CHECK_EQ(BLUR_A, requests.OnScriptRequest(1, OFF));
    CHECK_NONE(requests.OnScriptRequest(0, OFF));
}

TEST(LuaTiltShiftRequests, aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaTiltShiftRequests requests;
    requests.OnScriptStopped(0);

    CHECK_NONE(requests.OnScriptRequest(0, BLUR_A));

    requests.OnScriptStarted(0);
    CHECK_EQ(BLUR_A, requests.OnScriptRequest(0, BLUR_A));
}

TEST(LuaTiltShiftRequests, sessionEndForgetsRequestsAndStoppedScripts)
{
    LuaTiltShiftRequests requests;
    requests.OnScriptRequest(0, BLUR_A);
    requests.OnScriptStopped(1);

    CHECK_NONE(requests.OnSessionEnded());
    CHECK_NONE(requests.Current());
    CHECK_EQ(BLUR_B, requests.OnScriptRequest(1, BLUR_B));
}

TEST(LuaTiltShiftRequests, theBandStaysOnTheScreen)
{
    TiltShift blur = Make(1.f, 1.f, 0.1f, 0.5f, 6.f);

    CHECK_NEAR(0.f, blur.BandTop(), 0.f);
    CHECK_NEAR(0.35f, blur.BandBottom(), 0.0001f);
}

TEST(LuaTiltShiftRequests, aBandReachingAnEdgeLeavesNoBlurOnThatSide)
{
    TiltShift blur = Make(1.f, 0.5f, 0.1f, 0.5f, 6.f);

    CHECK_NEAR(0.f, blur.TopRadius(), 0.f);
    CHECK_NEAR(3.f, blur.BottomRadius(), 0.0001f);
    CHECK(!IsOffRequest(blur));
}

TEST(LuaTiltShiftRequests, aBlurWithNothingLeftToBlurIsOff)
{
    CHECK(IsOffRequest(OFF));
    CHECK(IsOffRequest(Make(1.f, 1.f, 0.5f, 1.f, 6.f)));
    CHECK(IsOffRequest(Make(1.f, 1.f, 0.5f, 0.3f, 0.f)));
}

TEST(LuaTiltShiftRequests, theColorBoostFollowsTheStrongerBand)
{
    TiltShift full = Make(1.f, 0.f, 0.5f, 0.3f, 6.f, 1.5f, 0.5f);
    CHECK_NEAR(1.5f, full.AppliedSaturation(), 0.0001f);
    CHECK_NEAR(0.5f, full.AppliedContrast(), 0.0001f);

    TiltShift half = Make(0.25f, 0.5f, 0.5f, 0.3f, 6.f, 1.5f, 0.5f);
    CHECK_NEAR(1.25f, half.AppliedSaturation(), 0.0001f);
    CHECK_NEAR(0.75f, half.AppliedContrast(), 0.0001f);
}

TEST(LuaTiltShiftRequests, noStrengthMeansNoBoostAndOff)
{
    TiltShift blur = Make(0.f, 0.f, 0.5f, 0.3f, 6.f, 1.5f, 1.5f);

    CHECK_NEAR(1.f, blur.AppliedSaturation(), 0.f);
    CHECK_NEAR(1.f, blur.AppliedContrast(), 0.f);
    CHECK(IsOffRequest(blur));
}

TEST(LuaTiltShiftRequests, aColorBoostKeepsItOnWithoutABlur)
{
    CHECK(!IsOffRequest(Make(1.f, 1.f, 0.5f, 1.f, 6.f, 1.3f)));
    CHECK(!IsOffRequest(Make(1.f, 1.f, 0.5f, 0.3f, 0.f, 1.f, 1.1f)));

    LuaTiltShiftRequests requests;
    TiltShift boostOnly = Make(1.f, 1.f, 0.5f, 0.3f, 0.f, 1.3f);
    CHECK_EQ(boostOnly, requests.OnScriptRequest(0, boostOnly));
}

// both the same-frame cache and the flow to the renderers tell changes apart by equality
TEST(LuaTiltShiftRequests, theColorValuesCountAsAChange)
{
    CHECK(!(BLUR_A == With(BLUR_A, [](TiltShift& t) { t.Saturation = 1.3f; })));
    CHECK(!(BLUR_A == With(BLUR_A, [](TiltShift& t) { t.Contrast = 1.1f; })));
    CHECK_EQ(BLUR_A, With(BLUR_A, [](TiltShift&) {}));
}

TEST(LuaTiltShiftRequests, aColorStrengthKeepsTheBoostOnWithoutBands)
{
    TiltShift colorOnly = ColorOnly(0.5f);

    CHECK_NEAR(1.15f, colorOnly.AppliedSaturation(), 0.0001f);
    CHECK_NEAR(1.05f, colorOnly.AppliedContrast(), 0.0001f);
    CHECK(colorOnly.HasColorBoost());
    CHECK(!colorOnly.HasBlur());
    CHECK(!IsOffRequest(colorOnly));
}

TEST(LuaTiltShiftRequests, noColorKeepsFollowingTheStrongerBand)
{
    TiltShift noColor = Make(0.25f, 0.5f, 0.5f, 0.3f, 6.f, 1.5f, 0.5f);

    CHECK_NONE(noColor.Color);
    CHECK_NEAR(1.25f, noColor.AppliedSaturation(), 0.f);
    CHECK_NEAR(0.75f, noColor.AppliedContrast(), 0.f);
    CHECK(IsOffRequest(ColorOnly(std::nullopt)));
}

// blur fading in or out mustn't show in the colors
TEST(LuaTiltShiftRequests, theSameColorGivesTheSameBoostWhateverTheBands)
{
    TiltShift colorOnly = ColorOnly(0.6f);
    TiltShift blurred = With(colorOnly, [](TiltShift& t) {
        t.Top = 1.f;
        t.Bottom = 0.25f;
    });

    CHECK_NEAR(colorOnly.AppliedSaturation(), blurred.AppliedSaturation(), 0.f);
    CHECK_NEAR(colorOnly.AppliedContrast(), blurred.AppliedContrast(), 0.f);
    CHECK(!colorOnly.HasBlur());
    CHECK(blurred.HasBlur());
}

TEST(LuaTiltShiftRequests, fullColorLooksLikeFullBandsWithoutOne)
{
    TiltShift noColor = Make(1.f, 1.f, 0.5f, 0.3f, 6.f, 1.3f, 1.1f);
    TiltShift withColor = With(noColor, [](TiltShift& t) { t.Color = 1.f; });

    CHECK_NEAR(noColor.AppliedSaturation(), withColor.AppliedSaturation(), 0.f);
    CHECK_NEAR(noColor.AppliedContrast(), withColor.AppliedContrast(), 0.f);
}

TEST(LuaTiltShiftRequests, colorZeroTurnsTheBoostOffButLeavesTheBlur)
{
    TiltShift blurOnly = Make(1.f, 1.f, 0.5f, 0.3f, 6.f, 1.3f, 1.1f, 0.f);

    CHECK_NEAR(1.f, blurOnly.AppliedSaturation(), 0.f);
    CHECK_NEAR(1.f, blurOnly.AppliedContrast(), 0.f);
    CHECK(!blurOnly.HasColorBoost());
    CHECK(!IsOffRequest(blurOnly));
}

TEST(LuaTiltShiftRequests, aColorWithNothingToDoIsOff)
{
    CHECK(IsOffRequest(ColorOnly(0.f)));
    CHECK(IsOffRequest(With(ColorOnly(1.f), [](TiltShift& t) {
        t.Saturation = 1.f;
        t.Contrast = 1.f;
    })));
}

TEST(LuaTiltShiftRequests, aBlurRadiusWithBandsAtTheEdgesLeavesOnlyTheColor)
{
    TiltShift noRoom = Make(1.f, 1.f, 0.5f, 1.f, 6.f, 1.3f, 1.f, 1.f);

    CHECK(!noRoom.HasBlur());
    CHECK(!IsOffRequest(noRoom));
}

TEST(LuaTiltShiftRequests, aColorOnlyRequestStaysUntilItsScriptStops)
{
    LuaTiltShiftRequests requests;
    TiltShift colorOnly = ColorOnly(1.f);

    CHECK_EQ(colorOnly, requests.OnScriptRequest(0, colorOnly));
    CHECK_EQ(colorOnly, requests.Current());
    CHECK_NONE(requests.OnScriptStopped(0));
    CHECK_NONE(requests.Current());
}

TEST(LuaTiltShiftRequests, aColorOnlyRequestFollowsTheSameRulesAsABlur)
{
    LuaTiltShiftRequests requests;
    TiltShift colorOnly = ColorOnly(1.f);

    requests.OnScriptRequest(0, BLUR_A);
    CHECK_EQ(colorOnly, requests.OnScriptRequest(1, colorOnly));
    CHECK_EQ(BLUR_A, requests.OnScriptRequest(1, OFF));

    requests.OnScriptStopped(2);
    CHECK_EQ(BLUR_A, requests.OnScriptRequest(2, colorOnly));

    requests.OnScriptRequest(1, colorOnly);
    CHECK_NONE(requests.OnSessionEnded());
}

TEST(LuaTiltShiftRequests, theColorStrengthCountsAsAChange)
{
    CHECK(!(BLUR_A == With(BLUR_A, [](TiltShift& t) { t.Color = 1.f; })));
    CHECK(!(With(BLUR_A, [](TiltShift& t) { t.Color = 1.f; }) == With(BLUR_A, [](TiltShift& t) { t.Color = 0.5f; })));
}
