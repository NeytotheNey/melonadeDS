// WideCameraTest.kt, case for case
#include <cmath>

#include "Test.h"
#include "camera/WideCamera.h"

using namespace Frontend;

namespace
{

// 16:9 shown, 2x rendered
const WideCameraFrame FRAME {342, 512, 192, false};
const WideWindow CENTERED = WideWindow::Centered(342, 512);

// frame.copy(...)
WideCameraFrame WithWidth(WideCameraFrame frame, int frameWidth)
{
    frame.FrameWidth = frameWidth;
    return frame;
}

WideCameraFrame WithWings(WideCameraFrame frame, int topWingLines)
{
    frame.TopWingLines = topWingLines;
    return frame;
}

WideCameraFrame WithMargin(WideCameraFrame frame, int frameMargin)
{
    frame.FrameMargin = frameMargin;
    return frame;
}

WideCameraFrame WithScene(WideCameraFrame frame, int gameScene)
{
    frame.GameScene = gameScene;
    return frame;
}

WideCameraFrame Swapped(WideCameraFrame frame)
{
    frame.AreScreensSwapped = true;
    return frame;
}

// camera.update(frame, WideCameraInput(...), request, dt) with an input of its own
std::optional<WideWindow> Update(WideCamera& camera, const WideCameraFrame& frame, WideCameraInput input, const WideCameraRequest& request,
    float dt)
{
    return camera.Update(frame, input, request, dt);
}

float KotlinExp(float x)
{
    return (float)std::exp((double)x);
}

float KotlinPow(float base, float exponent)
{
    return (float)std::pow((double)base, (double)exponent);
}

// past the wait after the start (RESUME_SECONDS), with nothing pressed
WideCamera ReadyCamera()
{
    WideCamera camera;
    for (int i = 0; i < 3; i++)
        Update(camera, FRAME, {}, {}, 0.1f);
    return camera;
}

// long enough for any easing to land; one input for all the frames, as the Kotlin test has it
std::optional<WideWindow> Settle(WideCamera& camera, WideCameraInput input = {}, const WideCameraRequest& request = {})
{
    std::optional<WideWindow> window;
    for (int i = 0; i < 60; i++)
        window = camera.Update(FRAME, input, request, 0.05f);
    return window;
}

// stage 2: the same frame with 48 rows of margin above and below the top screen, 288 rows in all (what 342 shown from 512 gets)
const WideCameraFrame TALL_FRAME = WithMargin(FRAME, 48);
const WideWindow TALL_CENTERED = WideWindow::Centered(342, 512, 288);

std::optional<WideWindow> SettleTall(WideCamera& camera, WideCameraInput input = {}, const WideCameraRequest& request = {})
{
    std::optional<WideWindow> window;
    for (int i = 0; i < 60; i++)
        window = camera.Update(TALL_FRAME, input, request, 0.05f);
    return window;
}

// [seconds] of 60 fps frames showing [scene], the last window
std::optional<WideWindow> Play(WideCamera& camera, int scene, float seconds, WideCameraInput input = {}, const WideCameraRequest& request = {},
    const WideCameraFrame& sceneFrame = FRAME)
{
    std::optional<WideWindow> window;
    int frames = RoundToInt(seconds * 60.f);
    for (int i = 0; i < frames; i++)
        window = camera.Update(WithScene(sceneFrame, scene), input, request, 1.f / 60.f);
    return window;
}

// a known game's camera at the player's zoom of 1.25 on the field, past the start's close-up and its return
WideCamera FieldCamera()
{
    WideCamera camera;
    camera.SetPlayerZoom(1.25f);
    Play(camera, GameScene::FIELD, 2.f);
    return camera;
}

// [frames] 60 fps frames of [scene], every zoom shown
std::vector<float> Zooms(WideCamera& camera, int scene, int frames)
{
    std::vector<float> zooms;
    for (int i = 0; i < frames; i++)
        zooms.push_back(SOME(Update(camera, WithScene(FRAME, scene), {}, {}, 1.f / 60.f)).Zoom());
    return zooms;
}

bool AllEqual(const std::vector<float>& values, float value)
{
    for (float v : values)
        if (v != value)
            return false;
    return true;
}

std::vector<float> Concat(std::vector<float> a, const std::vector<float>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// 768 rendered, so at 1x there's more room to pan than the player keep allows
const WideCameraFrame ROOMY_FRAME {342, 768, 192, false};

// The render width follows the player's zoom (WidescreenLaunch::GetRenderWidth): the widest view is 512, but the core renders only as wide
// as the zoom needs, so frames can be narrower than that. 342 shown at 1x renders at 342; 1.25x renders 428 wide with 25 rows of margin
const WideCameraFrame ONE_X_FRAME {342, 342, 192, false, 0, GameScene::UNKNOWN, 512};
const WideCameraFrame STEP_FRAME {342, 428, 192, false, 25, GameScene::UNKNOWN, 512};
const WideCameraFrame WIDEST_FRAME {342, 512, 192, false, 48, GameScene::UNKNOWN, 512};

std::optional<WideWindow> SettleOn(WideCamera& camera, const WideCameraFrame& sceneFrame, WideCameraInput input = {})
{
    std::optional<WideWindow> window;
    for (int i = 0; i < 60; i++)
        window = camera.Update(sceneFrame, input, {}, 0.05f);
    return window;
}

}

TEST(WideCamera, noWiderFrameMeansNoWindow)
{
    WideCamera camera;
    CHECK_NONE(Update(camera, WithWidth(FRAME, 342), {}, {}, 0.016f));
    WideCameraFrame narrowShow = FRAME;
    narrowShow.ShowWidth = 256;
    narrowShow.FrameWidth = 512;
    CHECK_NONE(Update(camera, narrowShow, {}, {}, 0.016f));
}

TEST(WideCamera, atRestTheWindowIsTheMiddleAtTheShowWidth)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(Settle(camera));
    CHECK_EQ(CENTERED, window);
    CHECK_EQ(85.f, window.X);
    CHECK_EQ(0.f, window.Offset());
    CHECK_EQ(1.f, window.Zoom());
}

TEST(WideCamera, theZoomKeyCyclesThroughTheStepsAndBackTo1x)
{
    CHECK_NEAR(1.25f, WideCamera::CycleZoom(1.f, 1.5f), 1e-5f);
    CHECK_NEAR(1.5f, WideCamera::CycleZoom(1.25f, 1.5f), 1e-5f);
    CHECK_NEAR(1.f, WideCamera::CycleZoom(1.5f, 1.5f), 1e-5f);
    // a range that doesn't end on a step has the widest view as its last one
    float maxZoom = 512 / 342.f;
    CHECK_NEAR(maxZoom, WideCamera::CycleZoom(1.25f, maxZoom), 1e-5f);
    CHECK_NEAR(1.f, WideCamera::CycleZoom(maxZoom, maxZoom), 1e-5f);
    // from a pinched zoom, the next step above it
    CHECK_NEAR(1.25f, WideCamera::CycleZoom(1.1f, maxZoom), 1e-5f);
    CHECK_NEAR(maxZoom, WideCamera::CycleZoom(1.4f, maxZoom), 1e-5f);
}

TEST(WideCamera, eachPressShowsTheNextStepAndIsReportedOnce)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    CHECK_NEAR(1.25f, SOME(Settle(camera)).Zoom(), 1e-4f);
    CHECK_NEAR(1.25f, SOME(camera.TakePickedZoom()), 1e-5f);
    CHECK_NONE(camera.TakePickedZoom());

    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    CHECK_NEAR(512.f, SOME(Settle(camera)).Width, 0.01f);
    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    CHECK_NEAR(1.f, SOME(Settle(camera)).Zoom(), 1e-4f);
}

TEST(WideCamera, aPressIsLeftForTheNextFrameOnlyWhenThisOneCantApplyIt)
{
    WideCamera camera = ReadyCamera();
    WideCameraInput input {.ZoomPresses = 1};
    CHECK_NONE(camera.Update(WithWidth(FRAME, 342), input, {}, 0.016f));
    CHECK_EQ(1, input.ZoomPresses);

    camera.Update(FRAME, input, {}, 0.016f);
    CHECK_EQ(0, input.ZoomPresses);

    // a script's zoom drops it instead
    input.ZoomPresses = 1;
    camera.Update(FRAME, input, {.Zoom = 1.f}, 0.016f);
    CHECK_EQ(0, input.ZoomPresses);
}

TEST(WideCamera, aPinchThenAPressGoesToTheStepAbove)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.PinchScale = 1 / 1.1f, .IsTouching = true}, {}, 0.016f);
    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    CHECK_NEAR(1.25f, SOME(Settle(camera)).Zoom(), 1e-4f);
}

TEST(WideCamera, aPressDuringAHoldShowsOnceTheHoldEndsAndAScriptsZoomIgnoresIt)
{
    WideCamera camera = ReadyCamera();
    WideCameraFrame menu = WithWings(FRAME, 0);
    CHECK_EQ(CENTERED, Update(camera, menu, {.ZoomPresses = 1}, {}, 0.016f));
    CHECK_NEAR(1.25f, SOME(camera.TakePickedZoom()), 1e-5f);
    CHECK_NEAR(1.25f, SOME(Settle(camera)).Zoom(), 1e-4f);

    Update(camera, FRAME, {.ZoomPresses = 1}, {.Zoom = 1.f}, 0.016f);
    CHECK_NONE(camera.TakePickedZoom());
}

TEST(WideCamera, theRememberedZoomIsWhereItStartsEvenAfterNarrowFrames)
{
    WideCamera camera;
    camera.SetPlayerZoom(1.25f);
    // frames from before the wide one, at launch
    CHECK_NONE(Update(camera, WithWidth(FRAME, 342), {}, {}, 0.016f));
    CHECK_NEAR(1.25f, SOME(Settle(camera)).Zoom(), 1e-4f);
    // a remembered zoom past this game's range is clamped
    camera.SetPlayerZoom(3.f);
    CHECK_NEAR(512.f, SOME(Settle(camera)).Width, 0.01f);
}

TEST(WideCamera, zoomingOutWidensTheWindowUpToTheFrame)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 2}, {}, 0.016f);
    WideWindow window = SOME(Settle(camera));
    CHECK_NEAR(0.f, window.X, 0.01f);
    CHECK_NEAR(512.f, window.Width, 0.01f);
}

TEST(WideCamera, theStickPansAndStopsAtTheEdge)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(Settle(camera, {.StickX = 1.f}));
    // clamped to the frame's right edge
    CHECK_NEAR(512.f - 342.f, window.X, 0.01f);
    CHECK_NEAR(85.f, window.Offset(), 0.01f);

    WideWindow left = SOME(Settle(camera, {.StickX = -1.f}));
    CHECK_NEAR(0.f, left.X, 0.01f);
}

TEST(WideCamera, theSticksDeadZoneDoesNothing)
{
    WideCamera camera = ReadyCamera();
    CHECK_EQ(CENTERED, Settle(camera, {.StickX = WideCamera::STICK_DEAD_ZONE}));
}

TEST(WideCamera, panningEasesInsteadOfJumping)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(Update(camera, FRAME, {.StickX = 1.f}, {}, 0.016f));
    float target = WideCamera::PAN_SPEED * 0.016f;
    CHECK(window.Offset() > 0.f && window.Offset() < target);
}

TEST(WideCamera, zoomingInClampsThePan)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    Settle(camera);
    // at 1.25x the window is 427.5 wide, so it can only sit 42.25 off center
    WideWindow window = SOME(Settle(camera, {.StickX = 1.f}));
    CHECK_NEAR(42.25f, window.Offset(), 0.01f);
    CHECK_NEAR(512.f, window.X + window.Width, 0.01f);
}

TEST(WideCamera, aDragMovesThePictureWithTheFinger)
{
    WideCamera camera = ReadyCamera();
    // a tenth of the screen's width to the right moves the window a tenth of its width to the left, at once
    WideWindow window = SOME(Update(camera, FRAME, {.DragFraction = 0.1f, .IsTouching = true}, {}, 0.016f));
    CHECK_NEAR(-34.2f, window.Offset(), 0.01f);
}

TEST(WideCamera, aPinchZoomsAtOnce)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(Update(camera, FRAME, {.PinchScale = 0.8f, .IsTouching = true}, {}, 0.016f));
    CHECK_NEAR(1.25f, window.Zoom(), 1e-4f);
}

TEST(WideCamera, movingWithTheStickIdleEasesBackToTheMiddle)
{
    WideCamera camera = ReadyCamera();
    Settle(camera, {.StickX = 1.f});
    WideWindow window = SOME(Settle(camera, {.IsPlayerMoving = true}));
    CHECK_NEAR(0.f, window.Offset(), 0.5f);
}

TEST(WideCamera, withoutReturnToCenterMovingKeepsThePan)
{
    WideCamera camera = ReadyCamera();
    camera.ReturnsToCenter = false;
    Settle(camera, {.StickX = 1.f});
    WideWindow window = SOME(Settle(camera, {.IsPlayerMoving = true}));
    CHECK_NEAR(85.f, window.Offset(), 0.01f);
}

TEST(WideCamera, standingStillKeepsThePan)
{
    WideCamera camera = ReadyCamera();
    Settle(camera, {.StickX = 1.f});
    CHECK_NEAR(85.f, SOME(Settle(camera)).Offset(), 0.01f);
}

TEST(WideCamera, noWingsSnapToTheMiddleAt1xAndTheZoomComesBackLater)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    Settle(camera, {.StickX = 1.f});

    WideCameraFrame menu = WithWings(FRAME, 0);
    CHECK_EQ(CENTERED, Update(camera, menu, {}, {}, 0.016f));
    // still the middle right after the wings come back
    CHECK_EQ(CENTERED, Update(camera, FRAME, {}, {}, 0.1f));

    // then the player's zoom again, in the middle
    WideWindow window = SOME(Settle(camera));
    CHECK_NEAR(1.25f, window.Zoom(), 1e-4f);
    CHECK_NEAR(0.f, window.Offset(), 0.01f);
}

TEST(WideCamera, swappedScreensAndHoldNativeKeep1x)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 2}, {}, 0.016f);
    Settle(camera);

    CHECK_EQ(CENTERED, Update(camera, Swapped(FRAME), {.StickX = 1.f}, {}, 0.016f));
    CHECK_EQ(CENTERED, Settle(camera, {}, {.Zoom = 1.4f, .HoldNative = true}));
}

TEST(WideCamera, aDualScreenGameWithHeldWingsZoomsAndPans)
{
    // the core counts the wing hold's held lines, so a dual-screen game's top screen has wings on every frame, if not always all 192
    WideCamera camera;
    std::optional<WideWindow> window;
    for (int i = 0; i < 120; i++)
    {
        int lines = i % 2 == 0 ? 192 : 150;
        window = Update(camera, WithWings(FRAME, lines), {.StickX = 1.f, .ZoomPresses = i == 30 ? 1 : 0}, {}, 0.016f);
    }
    CHECK_NEAR(1.25f, SOME(window).Zoom(), 1e-4f);
    CHECK_NEAR(42.25f, SOME(window).Offset(), 0.01f);
}

TEST(WideCamera, framesThatAlternateWithNoWingsNeverLeave1x)
{
    // what a dual-screen game would look like without the held lines counted (or one flashing its 3D): it stays at 1x
    WideCamera camera;
    for (int i = 0; i < 120; i++)
    {
        int lines = i % 2 == 0 ? 192 : 0;
        CHECK_EQ(CENTERED, Update(camera, WithWings(FRAME, lines), {.StickX = 1.f, .ZoomPresses = 1}, {}, 0.016f));
    }
}

// The Kotlin test checks that a camera at rest hands out the same object; a window here is a value, so it checks it's the same window
TEST(WideCamera, aWindowAtRestIsTheSameObject)
{
    WideCamera camera = ReadyCamera();
    std::optional<WideWindow> first = Settle(camera);
    CHECK(first.has_value());
    CHECK_EQ(first, Update(camera, FRAME, {}, {}, 0.016f));
}

TEST(WideCamera, theZoomLandsCloseToItsTarget)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    WideWindow window = SOME(Update(camera, FRAME, {}, {}, 0.016f));
    // bounded here, where the Kotlin loop isn't, so a zoom that never lands fails instead of hanging
    for (int frames = 0; window.Zoom() != 1.25f; frames++)
    {
        CHECK(frames < 10000);
        // never stuck more than a thousandth away
        CHECK(1.25f - window.Zoom() >= 0.001f - 1e-6f);
        window = SOME(Update(camera, FRAME, {}, {}, 0.016f));
    }
}

TEST(WideCamera, aScriptsZoomAndPanReplaceThePlayersAndAreClamped)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(Settle(camera, {.StickX = -1.f, .ZoomPresses = 1}, {.Zoom = 3.f, .Pan = 40.f}));
    // the widest view leaves no room to pan
    CHECK_NEAR(512.f, window.Width, 0.01f);
    CHECK_NEAR(0.f, window.Offset(), 0.01f);

    WideWindow panned = SOME(Settle(camera, {}, {.Zoom = 1.f, .Pan = 40.f}));
    CHECK_NEAR(1.f, panned.Zoom(), 1e-4f);
    CHECK_NEAR(40.f, panned.Offset(), 0.01f);
}

TEST(WideCamera, handingControlBackKeepsWhereTheScriptLeftIt)
{
    WideCamera camera = ReadyCamera();
    Settle(camera, {}, {.Zoom = 1.25f, .Pan = 20.f});
    WideWindow window = SOME(Settle(camera));
    CHECK_NEAR(1.25f, window.Zoom(), 1e-4f);
    CHECK_NEAR(20.f, window.Offset(), 0.01f);
}

TEST(WideCamera, aLongGapCountsAsAShortOne)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(Update(camera, FRAME, {.StickX = 1.f, .IsTouching = true}, {}, 5.f));
    CHECK_NEAR(WideCamera::PAN_SPEED * WideCamera::MAX_DELTA_SECONDS, window.Offset(), 0.01f);
}

TEST(WideCamera, withoutAMarginTheWindowKeepsTheScreensRowsAtEveryZoom)
{
    WideCamera camera = ReadyCamera();
    Update(camera, FRAME, {.ZoomPresses = 2}, {}, 0.016f);
    WideWindow window = SOME(Settle(camera, {.StickY = 1.f}));

    CHECK_NEAR(512.f, window.Width, 0.01f);
    // exactly the 192 rows, so the screen letterboxes as in stage 1
    CHECK_EQ(192, window.FrameHeight);
    CHECK_NEAR(0.f, window.Y, 0.f);
    CHECK_NEAR(192.f, window.Height, 0.f);
    CHECK_NEAR(0.f, window.OffsetY(), 0.f);
}

TEST(WideCamera, atRestTheTallWindowIsTheMiddleRows)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(SettleTall(camera));

    CHECK_EQ(TALL_CENTERED, window);
    CHECK_NEAR(48.f, window.Y, 0.f);
    CHECK_NEAR(192.f, window.Height, 0.f);
    CHECK_EQ(48, window.Margin());
}

TEST(WideCamera, zoomingOutGrowsTheWindowsRowsIntoTheMargin)
{
    WideCamera camera = ReadyCamera();
    Update(camera, TALL_FRAME, {.ZoomPresses = 2}, {}, 0.016f);
    WideWindow window = SOME(SettleTall(camera));

    CHECK_NEAR(512.f, window.Width, 0.01f);
    // 192 * 512 / 342 rows, under the frame's 288, centered
    CHECK_NEAR(192.f * 512.f / 342.f, window.Height, 0.01f);
    CHECK_NEAR(0.f, window.OffsetY(), 0.01f);
    CHECK(window.Y >= 0.f && window.Y + window.Height <= 288.f);
}

TEST(WideCamera, aMarginTooSmallForTheZoomStopsAtTheFrame)
{
    WideCamera camera = ReadyCamera();
    Update(camera, WithMargin(FRAME, 20), {.ZoomPresses = 2}, {}, 0.016f);
    std::optional<WideWindow> window;
    for (int i = 0; i < 60; i++)
        window = Update(camera, WithMargin(FRAME, 20), {}, {}, 0.05f);

    // all 232 rows, and the rest is letterboxed
    CHECK_NEAR(0.f, SOME(window).Y, 0.f);
    CHECK_NEAR(232.f, SOME(window).Height, 0.f);
}

TEST(WideCamera, theStickPansVerticallyAndStopsAtTheMarginsEdge)
{
    WideCamera camera = ReadyCamera();
    WideWindow down = SOME(SettleTall(camera, {.StickY = 1.f}));
    CHECK_NEAR(48.f, down.OffsetY(), 0.01f);
    CHECK_NEAR(288.f, down.Y + down.Height, 0.01f);
    // the other axis stays put
    CHECK_NEAR(0.f, down.Offset(), 0.0001f);

    WideWindow up = SOME(SettleTall(camera, {.StickY = -1.f}));
    CHECK_NEAR(-48.f, up.OffsetY(), 0.01f);
    CHECK_NEAR(0.f, up.Y, 0.01f);
}

TEST(WideCamera, zoomingOutLeavesLessRoomToPanVertically)
{
    WideCamera camera = ReadyCamera();
    SettleTall(camera, {.StickY = 1.f});
    Update(camera, TALL_FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    WideWindow window = SOME(SettleTall(camera, {.StickY = 1.f}));

    // at 1.25x the window is 240 rows, so it can go (288 - 240) / 2 down
    CHECK_NEAR(1.25f, window.Zoom(), 1e-4f);
    CHECK_NEAR(24.f, window.OffsetY(), 0.01f);
}

TEST(WideCamera, aVerticalDragMovesThePictureWithTheFinger)
{
    WideCamera camera = ReadyCamera();
    Update(camera, TALL_FRAME, {}, {}, 0.016f);
    // a tenth of the picture's height down: the window goes up a tenth of its 192 rows
    WideWindow window = SOME(Update(camera, TALL_FRAME, {.IsTouching = true, .DragFractionY = 0.1f}, {}, 0.016f));

    CHECK_NEAR(-19.2f, window.OffsetY(), 0.01f);
}

TEST(WideCamera, movingEasesTheVerticalPanBackToTheMiddleToo)
{
    WideCamera camera = ReadyCamera();
    SettleTall(camera, {.StickX = 1.f, .StickY = 1.f});
    WideWindow window = SOME(SettleTall(camera, {.IsPlayerMoving = true}));

    CHECK_NEAR(0.f, window.Offset(), 0.5f);
    CHECK_NEAR(0.f, window.OffsetY(), 0.5f);
}

TEST(WideCamera, aHoldCentersBothAxes)
{
    WideCamera camera = ReadyCamera();
    SettleTall(camera, {.StickY = 1.f});
    std::optional<WideWindow> window = Update(camera, WithWings(TALL_FRAME, 0), {.StickY = 1.f}, {}, 0.016f);

    CHECK_EQ(TALL_CENTERED, window);
}

TEST(WideCamera, aScriptsVerticalPanIsClampedAndLeavesXToThePlayer)
{
    WideCamera camera = ReadyCamera();
    WideWindow window = SOME(SettleTall(camera, {.StickX = 1.f}, {.PanY = 100.f}));

    CHECK_NEAR(48.f, window.OffsetY(), 0.01f);
    // pan(x) wasn't asked for, so the stick still has x
    CHECK_NEAR(85.f, window.Offset(), 0.01f);

    // and the other way round
    WideWindow xOnly = SOME(SettleTall(camera, {.StickY = -1.f}, {.Pan = -20.f}));
    CHECK_NEAR(-20.f, xOnly.Offset(), 0.01f);
    CHECK_NEAR(-48.f, xOnly.OffsetY(), 0.01f);
}

// known games (a GameScene from the native field read)

TEST(WideCamera, aKnownGameStartsAt1xAndZoomsOutHalfASecondAfterTheFieldShows)
{
    WideCamera camera;
    camera.SetPlayerZoom(1.25f);
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::OFF, 1.f)).Zoom(), 0.f);
    // not yet: the return waits 0.5 s
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::FIELD, 0.45f)).Zoom(), 0.f);
    // then eases out over 0.65 s
    float easing = SOME(Play(camera, GameScene::FIELD, 0.4f)).Zoom();
    CHECK(easing > 1.f && easing < 1.25f);
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::FIELD, 0.5f)).Zoom(), 0.f);
}

TEST(WideCamera, theZoomOutFollowsALogSpaceSmoothstepOver650ms)
{
    // the 16:9 shown, 512 rendered case from the Fold run: the widest step is 512 / 427 (about 1.2)
    const WideCameraFrame wide {427, 512, 192, false};
    float to = 512.f / 427.f;
    WideCamera camera;
    camera.SetPlayerZoom(to);
    Play(camera, GameScene::TEXT, 1.f, {}, {}, wide);
    // the text box closes; well inside the 0.5 s wait, then frame by frame
    Play(camera, GameScene::FIELD, 0.45f, {}, {}, wide);
    std::vector<float> zooms;
    for (int i = 0; i <= 60; i++)
        zooms.push_back(SOME(Update(camera, WithScene(wide, GameScene::FIELD), {}, {}, 1.f / 60.f)).Zoom());
    // the move starts on the last frame still at 1x, with no step
    int start = -1;
    for (int i = 0; i < (int)zooms.size(); i++)
        if (zooms[i] == 1.f)
            start = i;
    CHECK(start >= 1 && start <= 5);
    for (int k : {10, 20, 30})
    {
        float p = k / 60.f / 0.65f;
        CHECK_NEAR(KotlinPow(to, p * p * (3.f - 2.f * p)), zooms[start + k], 1e-4f);
    }
    // slow at both ends, fastest in the middle, and not there before 0.65 s (39 frames)
    CHECK(zooms[start + 1] - zooms[start] < zooms[start + 20] - zooms[start + 19]);
    CHECK(zooms[start + 37] - zooms[start + 36] < zooms[start + 20] - zooms[start + 19]);
    CHECK(zooms[start + 36] < to);
    CHECK_NEAR(to, zooms[start + 40], 0.f);
}

TEST(WideCamera, aTextBoxPullsInTo1xOverItsEase)
{
    WideCamera camera = FieldCamera();
    float easing = SOME(Play(camera, GameScene::TEXT, 0.2f)).Zoom();
    CHECK(easing > 1.f && easing < 1.25f);
    // in within 0.42 s
    WideWindow window = SOME(Play(camera, GameScene::TEXT, 0.25f));
    CHECK_NEAR(1.f, window.Zoom(), 0.f);
    CHECK_NEAR(0.f, window.Offset(), 0.f);
}

TEST(WideCamera, aTextBoxDoesNothingWithTheSettingOff)
{
    WideCamera camera = FieldCamera();
    camera.ZoomsInOnText = false;
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::TEXT, 1.f)).Zoom(), 0.f);
}

TEST(WideCamera, battlesMenusAndOffTheFieldAlwaysPullIn)
{
    for (int scene : {GameScene::BATTLE, GameScene::MENU, GameScene::OFF})
    {
        WideCamera camera = FieldCamera();
        camera.ZoomsInOnText = false;
        CHECK_NEAR(1.f, SOME(Play(camera, scene, 1.f)).Zoom(), 0.f);
    }
}

TEST(WideCamera, aBattlePullsInAsFastAsATextBox)
{
    // PokeDaisy's DS path calls a battle OFF, so it pulls in over the same 0.42 s
    WideCamera textCamera = FieldCamera(), battleCamera = FieldCamera(), laterCamera = FieldCamera();
    float text = SOME(Play(textCamera, GameScene::TEXT, 0.3f)).Zoom();
    float battle = SOME(Play(battleCamera, GameScene::BATTLE, 0.3f)).Zoom();
    CHECK_NEAR(text, battle, 0.f);
    CHECK(battle > 1.f);
    CHECK_NEAR(1.f, SOME(Play(laterCamera, GameScene::BATTLE, 0.45f)).Zoom(), 0.f);
}

TEST(WideCamera, anHgssBuildingExitDoesntPumpTheZoom)
{
    // the desktop's SoulSilver exit (logs/expand-phase1-ss-exit-debug.log): 92 frames of fade, 46 of text (the wipe-in's field task with
    // the town's banner on BG3), then the field
    WideCamera camera = FieldCamera();
    std::vector<float> fade = Zooms(camera, GameScene::FADE, 92);
    std::vector<float> text = Zooms(camera, GameScene::TEXT, 46);
    std::vector<float> field = Zooms(camera, GameScene::FIELD, 120);
    CHECK(AllEqual(Concat(Concat(fade, text), field), 1.25f));
}

TEST(WideCamera, aTextBoxRightAfterAFieldFramePullsIn)
{
    WideCamera camera = FieldCamera();
    Zooms(camera, GameScene::FADE, 92);
    Zooms(camera, GameScene::FIELD, 1);
    CHECK_NEAR(1.f, Zooms(camera, GameScene::TEXT, 30).back(), 0.f);
}

TEST(WideCamera, textThatOutlastsTheWindowAfterAFadePullsIn)
{
    WideCamera camera = FieldCamera();
    Zooms(camera, GameScene::FADE, 30);
    // held for the first 1.5 s, then pulled in over 0.42 s
    std::vector<float> held = Zooms(camera, GameScene::TEXT, 89);
    CHECK(AllEqual(held, 1.25f));
    CHECK_NEAR(1.f, Zooms(camera, GameScene::TEXT, 30).back(), 0.f);
    // and a fade then text after a close-up keeps it in
    Zooms(camera, GameScene::FADE, 30);
    CHECK(AllEqual(Zooms(camera, GameScene::TEXT, 46), 1.f));
}

TEST(WideCamera, aFadeHoldsTheZoomEitherWay)
{
    // a door's fade on the field: no pull-in
    WideCamera camera = FieldCamera();
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::FADE, 2.f)).Zoom(), 0.f);

    // a fade after a menu screen: still in, until the field has shown for 0.5 s
    Play(camera, GameScene::MENU, 1.f);
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::FADE, 2.f)).Zoom(), 0.f);
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::FIELD, 0.45f)).Zoom(), 0.f);
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::FIELD, 1.f)).Zoom(), 0.f);
}

TEST(WideCamera, theZoomComesBackHalfASecondAfterTheTextBoxCloses)
{
    WideCamera camera = FieldCamera();
    Play(camera, GameScene::TEXT, 1.f);
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::FIELD, 0.45f)).Zoom(), 0.f);
    float easing = SOME(Play(camera, GameScene::FIELD, 0.2f)).Zoom();
    CHECK(easing > 1.f && easing < 1.25f);
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::FIELD, 0.6f)).Zoom(), 0.f);
    // another text box within the half second keeps it in
    Play(camera, GameScene::TEXT, 1.f);
    Play(camera, GameScene::FIELD, 0.3f);
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::TEXT, 0.5f)).Zoom(), 0.f);
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::FIELD, 0.45f)).Zoom(), 0.f);
}

TEST(WideCamera, aKnownGameIgnoresTheWingLines)
{
    WideCamera camera = FieldCamera();
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::FIELD, 1.f, {}, {}, WithWings(FRAME, 0))).Zoom(), 0.f);
}

TEST(WideCamera, unknownGamesKeepTheWingRule)
{
    WideCamera camera = ReadyCamera();
    camera.SetPlayerZoom(1.25f);
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::UNKNOWN, 1.f)).Zoom(), 1e-4f);
    // no wings snaps to 1x at once, and comes back as it always did
    CHECK_EQ(CENTERED, Update(camera, WithWings(FRAME, 0), {}, {}, 1.f / 60.f));
    CHECK_EQ(CENTERED, Play(camera, GameScene::UNKNOWN, 0.2f));
    CHECK_NEAR(1.25f, SOME(Play(camera, GameScene::UNKNOWN, 1.f)).Zoom(), 1e-4f);
}

TEST(WideCamera, swappedScreensAndHoldNativeStillWinInAKnownGame)
{
    WideCamera swapped = FieldCamera();
    CHECK_EQ(CENTERED, Update(swapped, WithScene(Swapped(FRAME), GameScene::FIELD), {}, {}, 1.f / 60.f));
    WideCamera held = FieldCamera();
    CHECK_EQ(CENTERED, Update(held, WithScene(FRAME, GameScene::FIELD), {}, {.HoldNative = true}, 1.f / 60.f));
    // afterwards the player's zoom comes back like after any close-up
    CHECK_NEAR(1.f, SOME(Play(held, GameScene::FIELD, 0.45f)).Zoom(), 0.f);
    CHECK_NEAR(1.25f, SOME(Play(held, GameScene::FIELD, 1.f)).Zoom(), 0.f);
}

TEST(WideCamera, pulledInTheLookAroundSpringsBackToTheMiddle)
{
    WideCamera camera = FieldCamera();
    // at 1.25 the window is 427.5 wide, so it pans at most (512 - 427.5) / 2
    WideWindow panned = SOME(Play(camera, GameScene::FIELD, 1.f, {.StickX = 1.f}));
    CHECK_NEAR(42.25f, panned.Offset(), 1e-3f);
    // one frame in: a spring with a 0.12 s time constant, not a snap (the zoom's ease hasn't moved yet on its first frame)
    WideWindow firstFrame = SOME(Play(camera, GameScene::TEXT, 1.f / 60.f, {.StickX = 1.f}));
    CHECK_NEAR(42.25f * KotlinExp(-(1.f / 60.f) / 0.12f), firstFrame.Offset(), 0.01f);
    // about 1/e of the way left after one more time constant
    WideWindow afterTau = SOME(Play(camera, GameScene::TEXT, 0.12f, {.StickX = 1.f}));
    CHECK_NEAR(firstFrame.Offset() * KotlinExp(-1.f), afterTau.Offset(), 0.5f);
    // and the stick does nothing while pulled in: it lands in the middle
    CHECK_NEAR(0.f, SOME(Play(camera, GameScene::TEXT, 1.f, {.StickX = 1.f})).Offset(), 0.f);
}

TEST(WideCamera, anUnknownFrameInAKnownGameKeepsTheLastScene)
{
    // the read gives UNKNOWN for a frame after a reset or a state load; with no wing lines that frame would snap to 1x by the wing rule
    WideCamera reference = FieldCamera();
    WideCamera camera = FieldCamera();
    Play(reference, GameScene::TEXT, 0.1f);
    Play(camera, GameScene::TEXT, 0.1f);
    std::vector<float> referenceZooms, zooms;
    for (int i = 1; i <= 30; i++)
        referenceZooms.push_back(SOME(Update(reference, WithScene(FRAME, GameScene::TEXT), {}, {}, 1.f / 60.f)).Zoom());
    for (int i = 1; i <= 30; i++)
    {
        int scene = i == 1 ? GameScene::UNKNOWN : GameScene::TEXT;
        int wingLines = i == 1 ? 0 : 192;
        zooms.push_back(SOME(Update(camera, WithWings(WithScene(FRAME, scene), wingLines), {}, {}, 1.f / 60.f)).Zoom());
    }
    // the same ease, frame for frame: no blip to 1x and no restart
    CHECK_EQ(referenceZooms, zooms);
    CHECK(zooms.front() > 1.f);
}

TEST(WideCamera, standingStillTheViewStaysAndWalkingSpringsItBack)
{
    WideCamera camera;
    Play(camera, GameScene::FIELD, 1.f, {}, {}, ROOMY_FRAME);
    Play(camera, GameScene::FIELD, 0.3f, {.StickX = 1.f}, {}, ROOMY_FRAME);
    float panned = SOME(Play(camera, GameScene::FIELD, 1.f, {}, {}, ROOMY_FRAME)).Offset();
    CHECK(panned > 50.f);
    // standing with the stick let go: it stays
    CHECK_NEAR(panned, SOME(Play(camera, GameScene::FIELD, 2.f, {}, {}, ROOMY_FRAME)).Offset(), 0.f);
    // walking: back to the middle with a 0.18 s time constant, so about 1/e of the way left after 0.18 s
    float afterTau = SOME(Play(camera, GameScene::FIELD, 0.18f, {.IsPlayerMoving = true}, {}, ROOMY_FRAME)).Offset();
    CHECK_NEAR(panned * KotlinExp(-1.f), afterTau, panned * 0.05f);
    CHECK_NEAR(0.f, SOME(Play(camera, GameScene::FIELD, 2.f, {.IsPlayerMoving = true}, {}, ROOMY_FRAME)).Offset(), 0.f);
}

TEST(WideCamera, walkingWithTheStickFollowsItButKeepsThePlayerInside)
{
    WideCamera camera;
    Play(camera, GameScene::FIELD, 1.f, {}, {}, ROOMY_FRAME);
    WideWindow window = SOME(Play(camera, GameScene::FIELD, 2.f, {.StickX = 1.f, .IsPlayerMoving = true, .StickY = 0.f}, {}, ROOMY_FRAME));
    // the window is 342 wide, so the middle stays 28 inside its edge: 342 / 2 - 28
    CHECK_NEAR(143.f, window.Offset(), 0.01f);
    // half the stick past the dead zone, half as far
    WideWindow half = SOME(Play(camera, GameScene::FIELD, 2.f, {.StickX = 0.6f, .IsPlayerMoving = true}, {}, ROOMY_FRAME));
    CHECK_NEAR(71.5f, half.Offset(), 0.01f);
}

TEST(WideCamera, withoutReturnToCenterWalkingLeavesTheView)
{
    WideCamera camera;
    camera.ReturnsToCenter = false;
    Play(camera, GameScene::FIELD, 1.f, {}, {}, ROOMY_FRAME);
    Play(camera, GameScene::FIELD, 0.3f, {.StickX = 1.f}, {}, ROOMY_FRAME);
    float panned = SOME(Play(camera, GameScene::FIELD, 1.f, {}, {}, ROOMY_FRAME)).Offset();
    CHECK(panned > 50.f);
    CHECK_NEAR(panned, SOME(Play(camera, GameScene::FIELD, 2.f, {.IsPlayerMoving = true}, {}, ROOMY_FRAME)).Offset(), 0.f);
}

TEST(WideCamera, aScriptsZoomMovingEveryFrameStillGetsThereInAKnownGame)
{
    WideCamera camera = FieldCamera();
    std::optional<WideWindow> window;
    for (int i = 1; i <= 60; i++)
        window = Update(camera, WithScene(FRAME, GameScene::FIELD), {}, {.Zoom = 1.25f + i * 0.004f}, 1.f / 60.f);
    // close behind it (the ease lags a moving target by about 0.12 s), not stuck where it started
    CHECK_NEAR(1.49f, SOME(window).Zoom(), 0.04f);
    // and pulled in, the scene still wins
    CHECK_NEAR(1.f, SOME(Play(camera, GameScene::MENU, 1.f, {}, {.Zoom = 1.4f})).Zoom(), 0.f);
}

TEST(WideCamera, aPinchStillFollowsTheFingersInAKnownGame)
{
    WideCamera camera = FieldCamera();
    WideWindow window = SOME(Update(camera, WithScene(FRAME, GameScene::FIELD), {.PinchScale = 1.25f / 1.4f, .IsTouching = true}, {},
        1.f / 60.f));
    CHECK_NEAR(1.4f, window.Zoom(), 1e-4f);
}

TEST(WideCamera, theRenderZoomIsTheNextStepAtOrAboveTheZoom)
{
    float maxZoom = 512 / 342.f;
    CHECK_NEAR(1.f, WideCamera::GetRenderZoom(1.f, maxZoom), 0.f);
    CHECK_NEAR(1.25f, WideCamera::GetRenderZoom(1.1f, maxZoom), 0.f);
    CHECK_NEAR(1.25f, WideCamera::GetRenderZoom(1.25f, maxZoom), 0.f);
    // a float error past a step isn't the next one
    CHECK_NEAR(1.f, WideCamera::GetRenderZoom(1.0001f, maxZoom), 0.f);
    CHECK_NEAR(1.25f, WideCamera::GetRenderZoom(1.2501f, maxZoom), 0.f);
    // capped at the widest view, and never under 1
    CHECK_NEAR(maxZoom, WideCamera::GetRenderZoom(1.3f, maxZoom), 0.f);
    CHECK_NEAR(maxZoom, WideCamera::GetRenderZoom(3.f, maxZoom), 0.f);
    CHECK_NEAR(1.f, WideCamera::GetRenderZoom(0.5f, maxZoom), 0.f);
}

TEST(WideCamera, aFrameAtTheShownWidthGivesNoWindowButTheHotkeyStillZoomsOut)
{
    WideCamera camera;
    SettleOn(camera, ONE_X_FRAME);
    WideCameraInput input {.ZoomPresses = 1};
    // drawn as without a camera, but the press counts, so the core can be asked for a wider frame
    CHECK_NONE(camera.Update(ONE_X_FRAME, input, {}, 0.016f));
    CHECK_EQ(0, input.ZoomPresses);
    CHECK_NEAR(1.25f, camera.PlayerZoom(), 0.f);
    CHECK_NEAR(1.25f, SOME(camera.TakePickedZoom()), 0.f);
}

TEST(WideCamera, theHotkeyZoomsOutPastTheFramesWidthToTheWidestView)
{
    WideCamera camera;
    SettleOn(camera, STEP_FRAME);
    // from 1x on a frame that's only 1.25x wide: the range comes from the widest view, not the frame
    Update(camera, STEP_FRAME, {.ZoomPresses = 2}, {}, 0.016f);
    CHECK_NEAR(512.f / 342.f, camera.PlayerZoom(), 1e-5f);
    // and round to 1x after it
    Update(camera, STEP_FRAME, {.ZoomPresses = 1}, {}, 0.016f);
    CHECK_NEAR(1.f, camera.PlayerZoom(), 0.f);
}

TEST(WideCamera, whileTheWiderFrameIsOnItsWayTheWindowShowsAllOfTheOneThereIs)
{
    WideCamera camera;
    SettleOn(camera, STEP_FRAME, {.StickX = 1.f, .StickY = 1.f});
    Update(camera, STEP_FRAME, {.ZoomPresses = 2}, {}, 0.016f);
    WideWindow pending = SOME(SettleOn(camera, STEP_FRAME, {.StickX = 1.f, .StickY = 1.f}));

    // the whole 428 columns and the rows that keep the pixel shape, inside the frame's 242
    CHECK_NEAR(0.f, pending.X, 0.f);
    CHECK_NEAR(428.f, pending.Width, 0.01f);
    CHECK_NEAR(192.f * 428.f / 342.f, pending.Height, 0.01f);
    CHECK(pending.Y >= 0.f && pending.Y + pending.Height <= 242.f);

    // the widest frame comes: the zoom eases on from the frame instead of jumping to the end
    WideWindow first = SOME(Update(camera, WIDEST_FRAME, {}, {}, 0.016f));
    CHECK(first.Zoom() > 428.f / 342.f - 1e-4f && first.Zoom() < 1.45f);
    WideWindow window = SOME(SettleOn(camera, WIDEST_FRAME));
    CHECK_NEAR(512.f, window.Width, 0.01f);
    CHECK_NEAR(192.f * 512.f / 342.f, window.Height, 0.01f);
}

TEST(WideCamera, aKnownGamesZoomOutAheadOfTheFrameStartsItsEaseOnceTheFrameComes)
{
    WideCamera camera;
    camera.SetPlayerZoom(512.f / 342.f);
    // off the field, then the field on a frame that's only 1.25x wide
    Play(camera, GameScene::OFF, 1.f, {}, {}, STEP_FRAME);
    CHECK_NEAR(428.f / 342.f, SOME(Play(camera, GameScene::FIELD, 2.f, {}, {}, STEP_FRAME)).Zoom(), 1e-4f);
    // the widest frame: a fresh 0.65 s ease from the frame's zoom, not a jump
    float first = SOME(Play(camera, GameScene::FIELD, 1.f / 60.f, {}, {}, WIDEST_FRAME)).Zoom();
    CHECK(first < 1.27f);
    CHECK_NEAR(512.f / 342.f, SOME(Play(camera, GameScene::FIELD, 1.f, {}, {}, WIDEST_FRAME)).Zoom(), 1e-4f);
}

TEST(WideCamera, pullInsLeaveThePlayersZoomAlone)
{
    // no wing lines in an unknown game
    WideCamera camera;
    camera.SetPlayerZoom(1.25f);
    SettleOn(camera, STEP_FRAME);
    CHECK_NEAR(1.f, SOME(Update(camera, WithWings(STEP_FRAME, 0), {}, {}, 0.016f)).Zoom(), 0.f);
    CHECK_NEAR(1.25f, camera.PlayerZoom(), 0.f);
    // swapped screens and a script's hold
    Update(camera, Swapped(STEP_FRAME), {}, {}, 0.016f);
    Update(camera, STEP_FRAME, {}, {.HoldNative = true}, 0.016f);
    CHECK_NEAR(1.25f, camera.PlayerZoom(), 0.f);

    // a known game's text box, battle and menu
    WideCamera known;
    known.SetPlayerZoom(1.25f);
    Play(known, GameScene::FIELD, 2.f, {}, {}, STEP_FRAME);
    for (int scene : {GameScene::TEXT, GameScene::BATTLE, GameScene::MENU, GameScene::OFF})
    {
        CHECK_NEAR(1.f, SOME(Play(known, scene, 1.f, {}, {}, STEP_FRAME)).Zoom(), 0.f);
        CHECK_NEAR(1.25f, known.PlayerZoom(), 0.f);
    }
}

TEST(WideCamera, aScriptsZoomIsThePlayersZoomToo)
{
    // so a script's zoom-out gets a frame wide enough for it
    WideCamera camera = ReadyCamera();
    Update(camera, STEP_FRAME, {}, {.Zoom = 1.4f}, 0.016f);
    CHECK_NEAR(1.4f, camera.PlayerZoom(), 1e-5f);
}

TEST(WideCamera, aKnownRangeClampsThePlayersZoomEvenWhileHeld)
{
    WideCamera camera;
    camera.SetPlayerZoom(2.f);
    // held to 1x by a script and by a frame from before the wide one: the player's zoom is still kept inside the range
    Update(camera, STEP_FRAME, {}, {.HoldNative = true}, 0.016f);
    CHECK_NEAR(512.f / 342.f, camera.PlayerZoom(), 0.f);
    CHECK_NEAR(1.f, camera.ShownZoom(), 0.f);
    camera.SetPlayerZoom(2.f);
    CHECK_NONE(Update(camera, WithWidth(STEP_FRAME, 256), {}, {}, 0.016f));
    CHECK_NEAR(512.f / 342.f, camera.PlayerZoom(), 0.f);
}
