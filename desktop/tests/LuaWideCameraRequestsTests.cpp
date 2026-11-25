// LuaWideCameraRequestsTest.kt, case for case
#include "Test.h"
#include "camera/LuaWideCameraRequests.h"

using namespace Frontend;

namespace
{

const WideCameraRequest NONE {};
const WideCameraRequest ZOOM_OUT {.Zoom = 1.5f};
const WideCameraRequest PAN_RIGHT {.Pan = 40.f};
const WideCameraRequest HOLD {.HoldNative = true};

}

TEST(LuaWideCameraRequests, theNewestRequestWinsAndStoppingFallsBack)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, {.Zoom = 1.25f});
    CHECK_EQ(ZOOM_OUT, requests.OnScriptRequest(1, ZOOM_OUT));

    CHECK_EQ((WideCameraRequest {.Zoom = 1.25f}), requests.OnScriptStopped(1));
    CHECK_EQ(NONE, requests.OnScriptStopped(0));
}

TEST(LuaWideCameraRequests, eachAxisOfThePanComesFromTheNewestScriptAskingForIt)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, {.Pan = 10.f, .PanY = 20.f});
    // pan(30) with y left out: y stays the older script's
    CHECK_EQ((WideCameraRequest {.Pan = 30.f, .PanY = 20.f}), requests.OnScriptRequest(1, {.Pan = 30.f}));
    CHECK_EQ((WideCameraRequest {.Pan = 10.f, .PanY = 20.f}), requests.OnScriptStopped(1));
    // only a vertical pan still counts as a request
    CHECK_EQ((WideCameraRequest {.PanY = -5.f}), requests.OnScriptRequest(0, {.PanY = -5.f}));
}

TEST(LuaWideCameraRequests, zoomAndPanCanComeFromDifferentScripts)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, ZOOM_OUT);

    CHECK_EQ((WideCameraRequest {.Zoom = 1.5f, .Pan = 40.f}), requests.OnScriptRequest(1, PAN_RIGHT));
}

TEST(LuaWideCameraRequests, anyScriptHoldingNativeHoldsIt)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, HOLD);
    WideCameraRequest request = requests.OnScriptRequest(1, ZOOM_OUT);

    CHECK_EQ((WideCameraRequest {.Zoom = 1.5f, .HoldNative = true}), request);
    CHECK_EQ(ZOOM_OUT, requests.OnScriptStopped(0));
}

TEST(LuaWideCameraRequests, aChangedRequestBecomesTheMostRecent)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, ZOOM_OUT);
    requests.OnScriptRequest(1, {.Zoom = 2.f});

    CHECK_EQ((WideCameraRequest {.Zoom = 1.5f, .Pan = 10.f}), requests.OnScriptRequest(0, {.Zoom = 1.5f, .Pan = 10.f}));
}

TEST(LuaWideCameraRequests, askingForNothingDropsOnlyThatScriptsRequest)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, ZOOM_OUT);
    requests.OnScriptRequest(1, PAN_RIGHT);

    CHECK_EQ(ZOOM_OUT, requests.OnScriptRequest(1, NONE));
    CHECK_EQ(NONE, requests.OnScriptRequest(0, NONE));
}

TEST(LuaWideCameraRequests, aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain)
{
    LuaWideCameraRequests requests;
    requests.OnScriptStopped(0);

    CHECK_EQ(NONE, requests.OnScriptRequest(0, HOLD));

    requests.OnScriptStarted(0);
    CHECK_EQ(HOLD, requests.OnScriptRequest(0, HOLD));
}

TEST(LuaWideCameraRequests, aSessionEndDropsEverything)
{
    LuaWideCameraRequests requests;
    requests.OnScriptRequest(0, ZOOM_OUT);
    requests.OnScriptStopped(1);

    CHECK_EQ(NONE, requests.OnSessionEnded());
    // script 1's stop belonged to the old session
    CHECK_EQ(PAN_RIGHT, requests.OnScriptRequest(1, PAN_RIGHT));
}
