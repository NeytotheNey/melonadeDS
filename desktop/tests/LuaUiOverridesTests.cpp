// LuaUiOverridesTest.kt's LuaUiOverrides cases, case for case (its layout cases are in LuaLayoutRequestsTests.cpp)
#include <map>
#include <string>

#include "Test.h"
#include "lua/requests/LuaUiOverrides.h"

using namespace Frontend;

namespace Tests
{

static std::string Show(LuaUiSetting setting)
{
    switch (setting)
    {
    case LuaUiSetting::SwapScreens: return "SwapScreens";
    case LuaUiSetting::ShowControls: return "ShowControls";
    case LuaUiSetting::FastForward: return "FastForward";
    }
    return "?";
}

static std::string Show(const std::map<LuaUiSetting, bool>& values)
{
    std::string text = "{";
    for (const auto& [setting, value] : values)
        text += (text.size() > 1 ? ", " : "") + Show(setting) + "=" + Show(value);
    return text + "}";
}

}

namespace
{

using Values = std::map<LuaUiSetting, bool>;

constexpr LuaUiSetting SWAP = LuaUiSetting::SwapScreens;
constexpr LuaUiSetting FAST_FORWARD = LuaUiSetting::FastForward;
constexpr LuaUiSetting SHOW_CONTROLS = LuaUiSetting::ShowControls;

}

TEST(LuaUiOverrides, stoppedScriptRestoresThePlayersValue)
{
    LuaUiOverrides overrides;
    CHECK_EQ(true, overrides.OnScriptRequest(SWAP, 0, true, false));

    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(0));
    CHECK_NONE(overrides.ScriptValue(SWAP));
}

TEST(LuaUiOverrides, baselineIsTakenBeforeTheFirstRequestOnly)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    // the UI now shows the script's value, which must not become the baseline
    overrides.OnScriptRequest(SWAP, 1, false, true);

    overrides.OnScriptStopped(1);
    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, stoppingOneScriptAppliesTheMostRecentRemainingRequest)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptRequest(SWAP, 1, false, true);
    overrides.OnScriptRequest(SWAP, 2, true, false);

    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(2));
    CHECK_EQ(false, overrides.ScriptValue(SWAP));
}

TEST(LuaUiOverrides, aRepeatedRequestBecomesTheMostRecent)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptRequest(SWAP, 1, false, true);
    overrides.OnScriptRequest(SWAP, 0, true, false);

    CHECK_EQ((Values {{SWAP, true}}), overrides.OnScriptStopped(1));
}

TEST(LuaUiOverrides, settingsAreTrackedSeparately)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptRequest(FAST_FORWARD, 1, true, false);

    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(0));
    CHECK_EQ(true, overrides.ScriptValue(FAST_FORWARD));
}

TEST(LuaUiOverrides, stoppingAScriptThatHeldNothingChangesNothing)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);

    CHECK(overrides.OnScriptStopped(1).empty());
}

TEST(LuaUiOverrides, playerChangeWinsAndBecomesTheNewBaseline)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(FAST_FORWARD, 0, true, false);
    overrides.OnPlayerChanged(FAST_FORWARD);

    // nothing to undo once the player took over
    CHECK(overrides.OnScriptStopped(0).empty());
    CHECK_NONE(overrides.ScriptValue(FAST_FORWARD));

    // the player's value (on) is the baseline for the next request
    overrides.OnScriptRequest(FAST_FORWARD, 1, false, true);
    CHECK_EQ((Values {{FAST_FORWARD, true}}), overrides.OnScriptStopped(1));
}

TEST(LuaUiOverrides, aLateRequestFromAStoppedScriptIsIgnored)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptStopped(0);

    CHECK_EQ(false, overrides.OnScriptRequest(SWAP, 0, true, false));
    CHECK_NONE(overrides.ScriptValue(SWAP));
}

TEST(LuaUiOverrides, aScriptSwitchedBackOnCanRequestAgain)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptStopped(0);
    overrides.OnScriptStarted(0);

    // its new on_load() request applies, and the player's value is the baseline again
    CHECK_EQ(true, overrides.OnScriptRequest(SWAP, 0, true, false));
    CHECK_EQ(true, overrides.ScriptValue(SWAP));
    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, aNewSessionsFastForwardBaselineIsOff)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(FAST_FORWARD, 0, true, false);
    overrides.OnSessionEnded();

    // the toggle still shows the last game's value when the next game's script asks
    overrides.OnScriptRequest(FAST_FORWARD, 0, false, true);
    CHECK_EQ((Values {{FAST_FORWARD, false}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, playersFastForwardChangeBeforeAnyRequestIsTheBaseline)
{
    LuaUiOverrides overrides;
    overrides.OnPlayerChanged(FAST_FORWARD);

    overrides.OnScriptRequest(FAST_FORWARD, 0, false, true);
    CHECK_EQ((Values {{FAST_FORWARD, true}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, aScriptStoppedInOneSessionCanRequestInTheNext)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptStopped(0);
    overrides.OnSessionEnded();

    CHECK_EQ(true, overrides.OnScriptRequest(SWAP, 0, true, false));
    CHECK_EQ(true, overrides.ScriptValue(SWAP));
}

TEST(LuaUiOverrides, hiddenControlsComeBackWhenTheScriptStops)
{
    LuaUiOverrides overrides;
    CHECK_EQ(false, overrides.OnScriptRequest(SHOW_CONTROLS, 0, false, true));

    CHECK_EQ((Values {{SHOW_CONTROLS, true}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, sessionEndRestoresEveryHeldSettingAndForgetsThem)
{
    LuaUiOverrides overrides;
    overrides.OnScriptRequest(SWAP, 0, true, false);
    overrides.OnScriptRequest(FAST_FORWARD, 1, true, false);

    CHECK_EQ((Values {{SWAP, false}, {FAST_FORWARD, false}}), overrides.OnSessionEnded());
    CHECK_NONE(overrides.ScriptValue(FAST_FORWARD));
    CHECK(overrides.OnSessionEnded().empty());
}

TEST(LuaUiOverrides, aSavedSwapIsTheBaselineOfTheSessionsScripts)
{
    LuaUiOverrides overrides;
    // the game was left swapped, so the saved value is the first request's baseline even if the screen's value doesn't match it
    overrides.OnSessionStarted({{SWAP, true}});
    CHECK_EQ(false, overrides.OnScriptRequest(SWAP, 0, false, false));

    CHECK_EQ((Values {{SWAP, true}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, aSavedSwapIsPutBackWhenTheSessionEnds)
{
    LuaUiOverrides overrides;
    overrides.OnSessionStarted({{SWAP, true}});
    overrides.OnScriptRequest(SWAP, 0, false, true);

    CHECK_EQ((Values {{SWAP, true}}), overrides.OnSessionEnded());
}

TEST(LuaUiOverrides, scriptsStillWinOverASavedSwapUntilThePlayerSwaps)
{
    LuaUiOverrides overrides;
    overrides.OnSessionStarted({{SWAP, true}});
    overrides.OnScriptRequest(SWAP, 0, false, true);
    CHECK_EQ(false, overrides.OnScriptRequest(SWAP, 1, false, false));

    // the player swaps back to swapped, then a script asks again: the player's new value is its baseline
    overrides.OnPlayerChanged(SWAP);
    CHECK_NONE(overrides.ScriptValue(SWAP));
    CHECK_EQ(false, overrides.OnScriptRequest(SWAP, 0, false, true));
    CHECK_EQ((Values {{SWAP, true}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, thePlayersSwapBeforeAnyRequestReplacesTheSavedOne)
{
    LuaUiOverrides overrides;
    overrides.OnSessionStarted({{SWAP, true}});
    // the player unswaps before any script asks, so the screen's value is the player's from then on
    overrides.OnPlayerChanged(SWAP);
    CHECK_EQ(true, overrides.OnScriptRequest(SWAP, 0, true, false));

    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(0));
}

TEST(LuaUiOverrides, theNextSessionsSavedSwapReplacesTheLastOne)
{
    LuaUiOverrides overrides;
    overrides.OnSessionStarted({{SWAP, true}});
    overrides.OnSessionEnded();

    // the next game wasn't left swapped, so a value the screen still showed from the last game doesn't count
    overrides.OnSessionStarted({{SWAP, false}});
    overrides.OnScriptRequest(SWAP, 0, true, true);
    CHECK_EQ((Values {{SWAP, false}}), overrides.OnScriptStopped(0));
}
