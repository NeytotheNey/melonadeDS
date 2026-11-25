// WideCameraControllerTest.kt, case for case
#include <cstdint>

#include "Test.h"
#include "camera/WideCameraController.h"

using namespace Frontend;
using MelonDSAndroid::FrameGuard;

namespace Tests
{

// x, zoom and y, as melon.wide.camera() gets them
struct CameraSent
{
    float X, Zoom, Y;
    bool operator==(const CameraSent&) const = default;
};

std::string Show(const CameraSent& sent)
{
    return "(" + Show(sent.X) + ", " + Show(sent.Zoom) + ", " + Show(sent.Y) + ")";
}

}

namespace
{

using Tests::CameraSent;
using SentList = std::vector<CameraSent>;
// the zooms picked or the render zooms asked for, with their session
using SessionZooms = std::vector<std::pair<int, float>>;

const CameraSent AT_REST {0.f, 1.f, 0.f};
const WideCameraSettings SETTINGS {.ShowWidth = 342, .ReturnsToCenter = true, .MaxWidth = 512};
const WideCameraSettings NO_WIDEST {.ShowWidth = 342, .ReturnsToCenter = true};
constexpr int64_t FRAME_NANOS = 16'666'667;

WideCameraSettings WithReturn(WideCameraSettings settings, bool returnsToCenter)
{
    settings.ReturnsToCenter = returnsToCenter;
    return settings;
}

// the Kotlin test class's fields: a new one for every test
struct Fixture
{
    SentList Sent;
    SessionZooms Picked;
    SessionZooms RenderZooms;
    int64_t Now = 1'000'000'000;
    WideCameraController Controller {{
        .OnZoomPicked = [this](int session, float zoom) { Picked.emplace_back(session, zoom); },
        .OnCameraChanged = [this](float x, float zoom, float y) { Sent.push_back({x, zoom, y}); },
        .OnRenderZoomNeeded = [this](int session, float zoom) { RenderZooms.emplace_back(session, zoom); },
    }};

    // a known Pokemon game: the scene the native field read would give, and how often the controller asked
    int Scene = GameScene::FIELD;
    int SceneAsks = 0;
    WideCameraController KnownGameController {{
        .OnCameraChanged = [](float, float, float) {},
        .GameScene = [this]() { SceneAsks++; return Scene; },
    }};

    std::optional<WideWindow> Frames(int count, int wingLines = 192, int frameWidth = 512, int frameMargin = 0, FrameGuard frameGuard = {})
    {
        std::optional<WideWindow> window;
        for (int i = 0; i < count; i++)
        {
            Now += FRAME_NANOS;
            window = Controller.OnFrame(Now, frameWidth, frameMargin, wingLines, frameGuard);
        }
        return window;
    }

    std::optional<WideWindow> KnownGameFrames(int count, int wingLines = 192)
    {
        std::optional<WideWindow> window;
        for (int i = 0; i < count; i++)
        {
            Now += FRAME_NANOS;
            window = KnownGameController.OnFrame(Now, 512, 0, wingLines);
        }
        return window;
    }

    // [count] frames of [controller] at [frameWidth] and [frameMargin], on the shared clock
    std::optional<WideWindow> PlayOn(WideCameraController& controller, int count, int frameWidth, int frameMargin)
    {
        std::optional<WideWindow> window;
        for (int i = 0; i < count; i++)
        {
            Now += FRAME_NANOS;
            window = controller.OnFrame(Now, frameWidth, frameMargin, 192);
        }
        return window;
    }
};

// the sun's guard band: a frame 64 DS pixels wider on each side and 64 rows taller above and below than the shown one
constexpr int GUARD = 64;
const FrameGuard ALL_SIDES {GUARD, GUARD, GUARD, GUARD};
// the guard band on the left and above only: 64 more columns on the left and 32 more margin rows, all of them above
const FrameGuard LEFT_TOP {.left = 64, .top = 64};

// the same moves on a guarded frame and on its shown frame (512 wide with 48 margin rows): the guarded and the plain windows
struct GuardedAndPlain
{
    std::optional<WideWindow> Guarded, Plain;
};

GuardedAndPlain Both(Fixture& f, WideCameraController& plainController, int64_t& plainNow, int count, int frameWidth, int frameMargin,
    const FrameGuard& guard)
{
    std::optional<WideWindow> plain;
    for (int i = 0; i < count; i++)
    {
        plainNow += FRAME_NANOS;
        plain = plainController.OnFrame(plainNow, 512, 48, 192);
    }
    return {f.Frames(count, 192, frameWidth, frameMargin, guard), plain};
}

}

TEST(WideCameraController, offItGivesNoWindowAndTellsTheScriptsTheRestStateOnce)
{
    Fixture f;
    CHECK_NONE(f.Frames(30));
    // once, on the first frame, so scripts never keep a recreated controller's old camera
    CHECK_EQ(SentList {AT_REST}, f.Sent);
}

TEST(WideCameraController, switchingTheCameraOffDropsHeldInput)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30);
    f.Controller.OnSticks(1.f, 0.f, false);
    f.Controller.SetTouching(true);
    f.Controller.SetSettings(std::nullopt);
    f.Controller.SetSettings(NO_WIDEST);

    // no stick and no touch left over: it settles back at rest
    WideWindow window = SOME(f.Frames(120));
    CHECK_NEAR(0.f, window.Offset(), 0.0001f);
}

TEST(WideCameraController, releasingHeldInputStopsAStuckTouchAndStick)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30);
    f.Controller.OnSticks(1.f, 0.f, false);
    f.Controller.SetTouching(true);
    f.Controller.OnKey(WideCameraDirection::Left, true);
    f.Frames(10);
    f.Controller.ReleaseHeldInput();

    // the stick no longer pans and nothing counts as moving, so the window stays where it got to
    float stopped = SOME(f.Frames(1)).Offset();
    CHECK(stopped > 0.f);
    CHECK_NEAR(stopped, SOME(f.Frames(120)).Offset(), 0.0001f);
}

TEST(WideCameraController, theStickPansAndTheScriptsHearOfIt)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30);
    f.Controller.OnSticks(1.f, 0.f, false);
    WideWindow window = SOME(f.Frames(60));

    CHECK_NEAR(85.f, window.Offset(), 0.01f);
    CHECK_NEAR(85.f, f.Sent.back().X, 0.06f);
    CHECK_NEAR(1.f, f.Sent.back().Zoom, 0.0001f);
}

TEST(WideCameraController, theDpadBringsItBackAndTheLastWordIsTheMiddle)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30);
    f.Controller.OnSticks(1.f, 0.f, false);
    f.Frames(60);
    f.Controller.OnSticks(0.f, 0.f, false);
    f.Controller.OnKey(WideCameraDirection::Right, true);
    f.Frames(600);

    CHECK_EQ(AT_REST, f.Sent.back());
}

TEST(WideCameraController, switchingItOffGoesBackToNoWindowAndTheMiddle)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30);
    f.Controller.OnZoomPress();
    f.Frames(60);
    f.Controller.SetSettings(std::nullopt);

    CHECK_NONE(f.Frames(1));
    CHECK_EQ(AT_REST, f.Sent.back());
}

TEST(WideCameraController, itStartsAtTheRememberedZoomAndReportsPicks)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.25f});
    CHECK_NEAR(1.25f, SOME(f.Frames(60)).Zoom(), 0.0001f);
    // a settings change (a fold, say) keeps the zoom that's there
    f.Controller.SetState({1, WithReturn(SETTINGS, false), 1.f});
    CHECK_NEAR(1.25f, SOME(f.Frames(30)).Zoom(), 0.0001f);

    f.Controller.OnZoomPress();
    CHECK_NEAR(512.f, SOME(f.Frames(60)).Width, 0.01f);
    CHECK_EQ((SessionZooms {{1, 512.f / 342.f}}), f.Picked);
}

TEST(WideCameraController, aRecreatedControllerStartsAtTheLatestPick)
{
    // what the caller's state holds after a pick, replayed to a new controller
    Fixture f;
    f.Controller.SetState({3, SETTINGS, 512.f / 342.f});
    CHECK_NEAR(512.f, SOME(f.Frames(60)).Width, 0.01f);
}

TEST(WideCameraController, aPickComingBackDoesntDropHeldInput)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Frames(30);
    f.Controller.OnSticks(1.f, 0.f, false);
    f.Frames(5);
    // the caller's state after the pick: same settings, a new start zoom
    f.Controller.SetState({1, SETTINGS, 1.25f});
    // still panning to the edge, so the stick wasn't dropped
    CHECK_NEAR(85.f, SOME(f.Frames(120)).Offset(), 0.01f);
}

TEST(WideCameraController, aNewSessionStartsOverEvenWithoutTheOffStateBetween)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Frames(30);
    f.Controller.OnZoomPress();
    f.Controller.OnSticks(1.f, 0.f, false);
    f.Frames(60);
    // the next ROM's state, with the off settings between conflated away
    f.Controller.SetState({2, SETTINGS, 1.f});
    WideWindow window = SOME(f.Frames(60));
    CHECK_NEAR(1.f, window.Zoom(), 0.0001f);
    CHECK_NEAR(0.f, window.Offset(), 0.0001f);

    f.Controller.OnZoomPress();
    f.Frames(30);
    CHECK_EQ((SessionZooms {{1, 1.25f}, {2, 1.25f}}), f.Picked);
}

TEST(WideCameraController, aPressBeforeTheWideFrameWaitsForIt)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Controller.OnZoomPress();
    // frames from before the wide one (at launch, or a width change on its way)
    CHECK_NONE(f.Frames(10, 192, 256));
    CHECK_NEAR(1.25f, SOME(f.Frames(60)).Zoom(), 0.0001f);
}

TEST(WideCameraController, aPressWhileOffIsDropped)
{
    Fixture f;
    f.Controller.OnZoomPress();
    f.Frames(5);
    f.Controller.SetState({1, SETTINGS, 1.f});
    CHECK_NEAR(1.f, SOME(f.Frames(60)).Zoom(), 0.0001f);
    CHECK(f.Picked.empty());
}

TEST(WideCameraController, theStickPansDownIntoTheMarginAndTheScriptsHearOfIt)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30, 192, 512, 48);
    f.Controller.OnSticks(0.f, 1.f, false);
    WideWindow window = SOME(f.Frames(60, 192, 512, 48));

    // at 1x the window's 192 rows can go 48 rows down, to the margin's bottom
    CHECK_NEAR(48.f, window.OffsetY(), 0.01f);
    CHECK_NEAR(0.f, window.Offset(), 0.0001f);
    CHECK_NEAR(48.f, f.Sent.back().Y, 0.06f);
}

TEST(WideCameraController, aVerticalDragMovesThePictureWithTheFinger)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30, 192, 512, 48);
    f.Controller.SetTouching(true);
    // a tenth of the picture's height down, so the window goes up by 19.2 of its 192 rows
    f.Controller.OnTouchMove(0.f, 1.f, 0.1f);
    WideWindow window = SOME(f.Frames(1, 192, 512, 48));

    CHECK_NEAR(-19.2f, window.OffsetY(), 0.01f);
}

TEST(WideCameraController, withoutAMarginTheStickDownDoesNothing)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Frames(30);
    f.Controller.OnSticks(0.f, 1.f, false);
    WideWindow window = SOME(f.Frames(60));

    CHECK_NEAR(0.f, window.Y, 0.f);
    CHECK_NEAR(192.f, window.Height, 0.f);
    CHECK_EQ(AT_REST, f.Sent.back());
}

TEST(WideCameraController, aScriptHoldingNativeKeepsTheMiddle)
{
    Fixture f;
    f.Controller.SetSettings(NO_WIDEST);
    f.Controller.SetScriptRequest({.HoldNative = true});
    for (int i = 0; i < 4; i++)
        f.Controller.OnZoomPress();
    WideWindow window = SOME(f.Frames(60));

    CHECK_NEAR(1.f, window.Zoom(), 0.0001f);
    CHECK_EQ(SentList {AT_REST}, f.Sent);
}

TEST(WideCameraController, aKnownGamesTextBoxPullsInAndTheZoomComesBackAfter)
{
    Fixture f;
    f.KnownGameController.SetState({1, SETTINGS, 1.25f});
    CHECK_NEAR(1.25f, SOME(f.KnownGameFrames(120)).Zoom(), 0.f);
    f.Scene = GameScene::TEXT;
    CHECK_NEAR(1.f, SOME(f.KnownGameFrames(30)).Zoom(), 0.f);
    f.Scene = GameScene::FIELD;
    // still in until 0.5 s after the text box
    CHECK_NEAR(1.f, SOME(f.KnownGameFrames(27)).Zoom(), 0.f);
    CHECK_NEAR(1.25f, SOME(f.KnownGameFrames(60)).Zoom(), 0.f);
}

TEST(WideCameraController, theTextSettingOffLeavesTheZoomOut)
{
    Fixture f;
    WideCameraSettings noText = SETTINGS;
    noText.ZoomsInOnText = false;
    f.KnownGameController.SetState({1, noText, 1.25f});
    f.KnownGameFrames(120);
    f.Scene = GameScene::TEXT;
    CHECK_NEAR(1.25f, SOME(f.KnownGameFrames(60)).Zoom(), 0.f);
    // a battle still pulls in
    f.Scene = GameScene::BATTLE;
    CHECK_NEAR(1.f, SOME(f.KnownGameFrames(60)).Zoom(), 0.f);
}

TEST(WideCameraController, theSceneDecidesInsteadOfTheWingLinesAndAFadeHolds)
{
    Fixture f;
    f.KnownGameController.SetState({1, SETTINGS, 1.25f});
    f.KnownGameFrames(120);
    CHECK_NEAR(1.25f, SOME(f.KnownGameFrames(60, 0)).Zoom(), 0.f);
    f.Scene = GameScene::FADE;
    CHECK_NEAR(1.25f, SOME(f.KnownGameFrames(60, 0)).Zoom(), 0.f);
}

TEST(WideCameraController, swappedScreensAndAScriptsHoldStillWinInAKnownGame)
{
    Fixture f;
    f.KnownGameController.SetState({1, SETTINGS, 1.25f});
    f.KnownGameFrames(120);
    f.KnownGameController.SetScreensSwapped(true);
    CHECK_NEAR(1.f, SOME(f.KnownGameFrames(1)).Zoom(), 0.f);
    f.KnownGameController.SetScreensSwapped(false);
    f.KnownGameController.SetScriptRequest({.HoldNative = true});
    CHECK_NEAR(1.f, SOME(f.KnownGameFrames(120)).Zoom(), 0.f);
}

TEST(WideCameraController, theSceneIsOnlyAskedForWhileTheCameraIsOn)
{
    Fixture f;
    f.KnownGameFrames(30);
    CHECK_EQ(0, f.SceneAsks);
    f.KnownGameController.SetSettings(SETTINGS);
    f.KnownGameFrames(30);
    CHECK_EQ(30, f.SceneAsks);
}

TEST(WideCameraController, anUnknownGameKeepsTheWingRule)
{
    // the default scene provider: no game state, so no wings snaps to 1x at once
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.25f});
    CHECK_NEAR(1.25f, SOME(f.Frames(120)).Zoom(), 0.0001f);
    CHECK_NEAR(1.f, SOME(f.Frames(1, 0)).Zoom(), 0.f);
}

// the render width follows the player's zoom: frames at 342 are the core at 1x, 428 with 25 rows at 1.25x, 512 with 48 at the widest

TEST(WideCameraController, at1xItAsksForThe1xRenderOnceAndDrawsAsWithoutACamera)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    CHECK_NONE(f.Frames(60, 192, 342));
    CHECK_EQ((SessionZooms {{1, 1.f}}), f.RenderZooms);
    CHECK_EQ(SentList {AT_REST}, f.Sent);
}

TEST(WideCameraController, itAsksForTheRememberedZoomsStep)
{
    Fixture f;
    f.Controller.SetState({2, SETTINGS, 512.f / 342.f});
    f.Frames(1);
    CHECK_EQ((SessionZooms {{2, 512.f / 342.f}}), f.RenderZooms);
}

TEST(WideCameraController, aPressAt1xAsksForTheWiderFrameAndShowsItOnceItComes)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Frames(30, 192, 342);
    f.Controller.OnZoomPress();
    // asked for at once; until the frame comes it's drawn at 1x
    CHECK_NONE(f.Frames(1, 192, 342));
    CHECK_EQ((SessionZooms {{1, 1.f}, {1, 1.25f}}), f.RenderZooms);
    CHECK_NONE(f.Frames(3, 192, 342));
    CHECK_NEAR(1.25f, SOME(f.Frames(60, 192, 428, 25)).Zoom(), 0.0001f);

    // the next press goes past the frame's width to the widest view, shown whole frame until that one comes
    f.Controller.OnZoomPress();
    CHECK_NEAR(428.f, SOME(f.Frames(10, 192, 428, 25)).Width, 0.01f);
    CHECK_EQ((std::pair<int, float> {1, 512.f / 342.f}), f.RenderZooms.back());
    CHECK_NEAR(512.f, SOME(f.Frames(60, 192, 512, 48)).Width, 0.01f);

    // then back to 1x, which asks for the 1x render once the zoom has eased in
    f.Controller.OnZoomPress();
    f.Frames(60, 192, 512, 48);
    CHECK_EQ((std::pair<int, float> {1, 1.f}), f.RenderZooms.back());
    CHECK_EQ(4, (int)f.RenderZooms.size());
}

TEST(WideCameraController, aPinchAsksForMoreRoomAtOnceButLessOnlyOnceTheFingersAreOff)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Frames(30, 192, 342);
    f.Controller.SetTouching(true);
    // out to 1.1, which renders for 1.25
    f.Controller.OnTouchMove(0.f, 1.f / 1.1f);
    f.Frames(1, 192, 342);
    CHECK_EQ((std::pair<int, float> {1, 1.25f}), f.RenderZooms.back());
    // back in to 1x with the fingers still down: no resize yet
    f.Controller.OnTouchMove(0.f, 1.1f);
    f.Frames(5, 192, 428, 25);
    CHECK_EQ((std::pair<int, float> {1, 1.25f}), f.RenderZooms.back());
    f.Controller.SetTouching(false);
    // and with them off, after the few frames a narrower one has to hold
    f.Frames(WideCameraController::SHRINK_FRAMES - 1, 192, 428, 25);
    CHECK_EQ((std::pair<int, float> {1, 1.25f}), f.RenderZooms.back());
    f.Frames(1, 192, 428, 25);
    CHECK_EQ((SessionZooms {{1, 1.f}, {1, 1.25f}, {1, 1.f}}), f.RenderZooms);
}

TEST(WideCameraController, pullInsDontChangeTheRenderZoom)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.25f});
    f.Frames(60, 192, 428, 25);
    // no wing lines, swapped screens and a script's hold all show 1x, but the player's zoom stays where it was
    CHECK_NEAR(1.f, SOME(f.Frames(30, 0, 428, 25)).Zoom(), 0.f);
    f.Controller.SetScreensSwapped(true);
    f.Frames(30, 192, 428, 25);
    f.Controller.SetScreensSwapped(false);
    f.Controller.SetScriptRequest({.HoldNative = true});
    f.Frames(30, 192, 428, 25);
    CHECK_EQ((SessionZooms {{1, 1.25f}}), f.RenderZooms);
}

TEST(WideCameraController, aKnownGamesPullInDoesntChangeTheRenderZoom)
{
    Fixture f;
    std::vector<float> asked;
    WideCameraController known {{
        .OnCameraChanged = [](float, float, float) {},
        .GameScene = [&f]() { return f.Scene; },
        .OnRenderZoomNeeded = [&asked](int, float zoom) { asked.push_back(zoom); },
    }};
    known.SetState({1, SETTINGS, 1.25f});
    CHECK_NEAR(1.25f, SOME(f.PlayOn(known, 120, 428, 25)).Zoom(), 0.f);
    for (int pullIn : {GameScene::TEXT, GameScene::BATTLE, GameScene::MENU})
    {
        f.Scene = pullIn;
        CHECK_NEAR(1.f, SOME(f.PlayOn(known, 60, 428, 25)).Zoom(), 0.f);
    }
    CHECK_EQ(std::vector<float> {1.25f}, asked);
}

TEST(WideCameraController, aScriptsZoomOutAsksForTheRoomItNeeds)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Frames(30, 192, 342);
    f.Controller.SetScriptRequest({.Zoom = 1.4f});
    f.Frames(1, 192, 342);
    CHECK_EQ((std::pair<int, float> {1, 512.f / 342.f}), f.RenderZooms.back());
}

TEST(WideCameraController, aNewSessionOrShownWidthAsksAgain)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.25f});
    f.Frames(1, 192, 428, 25);
    // a fold: the shown width changes, and the zoom is clamped to the new range
    WideCameraSettings folded = SETTINGS;
    folded.ShowWidth = 426;
    f.Controller.SetSettings(folded);
    f.Frames(1);
    CHECK_EQ((std::pair<int, float> {1, 512.f / 426.f}), f.RenderZooms.back());
    f.Controller.SetState({2, SETTINGS, 1.f});
    f.Frames(1, 192, 342);
    CHECK_EQ((SessionZooms {{1, 1.25f}, {1, 512.f / 426.f}, {2, 1.f}}), f.RenderZooms);
}

TEST(WideCameraController, aWrapTo1xEasesAllTheWayInBeforeTheNarrowerRenderIsAskedFor)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 512.f / 342.f});
    CHECK_NEAR(512.f, SOME(f.Frames(60, 192, 512, 48)).Width, 0.01f);
    f.Controller.OnZoomPress();
    // each frame's window zoom until the narrower render zoom is asked for
    std::vector<float> zooms;
    while (f.RenderZooms.size() == 1 && zooms.size() < 300)
    {
        std::optional<WideWindow> window = f.Frames(1, 192, 512, 48);
        zooms.push_back(window ? window->Zoom() : 1.f);
    }

    CHECK_EQ((SessionZooms {{1, 512.f / 342.f}, {1, 1.f}}), f.RenderZooms);
    // the ease ran on the wide frame: the zoom came down over several frames and was at 1x for the last few before the ask
    CHECK(zooms.front() > 1.2f);
    CHECK(zooms.size() > 10);
    for (size_t i = zooms.size() - WideCameraController::SHRINK_FRAMES; i < zooms.size(); i++)
        CHECK(zooms[i] <= 1.001f);
    for (size_t i = 1; i < zooms.size(); i++)
        CHECK(zooms[i] <= zooms[i - 1] + 1e-5f);
}

TEST(WideCameraController, aKnownGamesWrapTo1xEasesOverItsPullInBeforeTheNarrowerRender)
{
    Fixture f;
    std::vector<float> asked;
    WideCameraController known {{
        .OnCameraChanged = [](float, float, float) {},
        .GameScene = [&f]() { return f.Scene; },
        .OnRenderZoomNeeded = [&asked](int, float zoom) { asked.push_back(zoom); },
    }};
    known.SetState({1, SETTINGS, 512.f / 342.f});
    CHECK_NEAR(512.f, SOME(f.PlayOn(known, 180, 512, 48)).Width, 0.01f);
    known.OnZoomPress();
    std::vector<float> zooms;
    while (asked.size() == 1 && zooms.size() < 300)
    {
        std::optional<WideWindow> window = f.PlayOn(known, 1, 512, 48);
        zooms.push_back(window ? window->Zoom() : 1.f);
    }

    CHECK_EQ((std::vector<float> {512.f / 342.f, 1.f}), asked);
    // the 0.42 s pull-in ease, then the frames a narrower one holds
    CHECK((int)zooms.size() >= (int)(WideCamera::PULL_IN_SECONDS * 60.f));
    CHECK(zooms[zooms.size() / 4] > 1.1f);
    for (size_t i = zooms.size() - WideCameraController::SHRINK_FRAMES; i < zooms.size(); i++)
        CHECK(zooms[i] <= 1.001f);
}

TEST(WideCameraController, withAGuardTheCameraWorksOnTheShownFrame)
{
    // the same moves on a guarded frame and on its shown frame give the same windows
    Fixture f;
    WideCameraController plainController {{.OnCameraChanged = [](float, float, float) {}}};
    f.Controller.SetState({1, SETTINGS, 1.f});
    plainController.SetState({1, SETTINGS, 1.f});
    int64_t plainNow = f.Now;
    Both(f, plainController, plainNow, 30, 640, 112, ALL_SIDES);
    f.Controller.OnZoomPress();
    plainController.OnZoomPress();
    f.Controller.OnSticks(0.6f, -0.4f, false);
    plainController.OnSticks(0.6f, -0.4f, false);
    GuardedAndPlain both = Both(f, plainController, plainNow, 20, 640, 112, ALL_SIDES);

    CHECK_EQ(both.Plain, both.Guarded);
    CHECK_EQ(512, SOME(both.Guarded).FrameWidth);
    CHECK_EQ(288, SOME(both.Guarded).FrameHeight);
}

TEST(WideCameraController, withAGuardZoomedAllTheWayOutItShowsTheWholeShownFrame)
{
    // no widest view: the range is the shown frame's, not the guarded one's
    Fixture f;
    f.Controller.SetState({1, NO_WIDEST, 1.f});
    f.Frames(30, 192, 640, 112, ALL_SIDES);
    for (int i = 0; i < 2; i++)
        f.Controller.OnZoomPress();
    WideWindow window = SOME(f.Frames(60, 192, 640, 112, ALL_SIDES));

    CHECK_NEAR(512.f / 342.f, window.Zoom(), 0.0001f);
    CHECK_NEAR(0.f, window.X, 0.01f);
    CHECK_NEAR(512.f, window.Width, 0.01f);
    CHECK_EQ(512, window.FrameWidth);
    CHECK_EQ(288, window.FrameHeight);
    // the rows fit inside the shown 288 too
    CHECK(window.Y >= 0.f && window.Y + window.Height <= 288.f);
    CHECK_NEAR(512.f / 342.f, f.RenderZooms.back().second, 0.0001f);
}

TEST(WideCameraController, withAGuardThePanStopsAtTheShownFramesEdges)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    f.Frames(30, 192, 640, 112, ALL_SIDES);
    f.Controller.OnSticks(1.f, 1.f, false);
    WideWindow window = SOME(f.Frames(120, 192, 640, 112, ALL_SIDES));

    // at 1x the 342 columns go to the shown 512's right edge and the 192 rows to the shown 288's bottom, never into the guard
    CHECK_NEAR(170.f, window.X, 0.01f);
    CHECK_NEAR(96.f, window.Y, 0.01f);
    CHECK_NEAR(85.f, window.Offset(), 0.01f);
    CHECK_NEAR(48.f, window.OffsetY(), 0.01f);
}

TEST(WideCameraController, withAGuardAt1xItsDrawnAsWithoutACamera)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    // the shown frame is the show width: 342 + 2 * 64 columns and 64 margin rows that are all guard
    CHECK_NONE(f.Frames(30, 192, 470, GUARD, ALL_SIDES));
    CHECK_EQ((SessionZooms {{1, 1.f}}), f.RenderZooms);
}

TEST(WideCameraController, withALeftAndTopGuardTheCameraWorksOnTheShownFrame)
{
    Fixture f;
    WideCameraController plainController {{.OnCameraChanged = [](float, float, float) {}}};
    f.Controller.SetState({1, SETTINGS, 1.f});
    plainController.SetState({1, SETTINGS, 1.f});
    int64_t plainNow = f.Now;
    Both(f, plainController, plainNow, 30, 576, 80, LEFT_TOP);
    f.Controller.OnZoomPress();
    plainController.OnZoomPress();
    f.Controller.OnSticks(-0.6f, 0.4f, false);
    plainController.OnSticks(-0.6f, 0.4f, false);
    GuardedAndPlain both = Both(f, plainController, plainNow, 20, 576, 80, LEFT_TOP);

    CHECK_EQ(both.Plain, both.Guarded);
    CHECK_EQ(512, SOME(both.Guarded).FrameWidth);
    CHECK_EQ(288, SOME(both.Guarded).FrameHeight);
}

TEST(WideCameraController, withARightAndBottomGuardThePanStopsAtTheShownFramesEdges)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    FrameGuard rightBottom {.right = 64, .bottom = 64};
    f.Frames(30, 192, 576, 80, rightBottom);
    f.Controller.OnSticks(1.f, 1.f, false);
    WideWindow window = SOME(f.Frames(120, 192, 576, 80, rightBottom));

    // the shown frame's own right and bottom edges, never into the guard past them
    CHECK_NEAR(170.f, window.X, 0.01f);
    CHECK_NEAR(96.f, window.Y, 0.01f);
    CHECK_EQ(512, window.FrameWidth);
    CHECK_EQ(288, window.FrameHeight);
}

TEST(WideCameraController, withALeftAndTopGuardAt1xItsDrawnAsWithoutACamera)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.f});
    // the shown frame is the show width: 342 + 64 columns and 32 margin rows that are all guard
    CHECK_NONE(f.Frames(30, 192, 406, 32, LEFT_TOP));
    CHECK_EQ((SessionZooms {{1, 1.f}}), f.RenderZooms);
}

TEST(WideCameraController, aScriptsZoomWobblingAcrossAStepDoesntResizeEveryFrame)
{
    Fixture f;
    f.Controller.SetState({1, SETTINGS, 1.25f});
    f.Frames(60, 192, 428, 25);
    for (int i = 0; i < 120; i++)
    {
        f.Controller.SetScriptRequest({.Zoom = i % 2 == 0 ? 1.26f : 1.24f});
        f.Frames(1, 192, 512, 48);
    }
    // the start zoom's, then one wider for 1.26, and no narrower one back and forth
    CHECK_EQ((SessionZooms {{1, 1.25f}, {1, 512.f / 342.f}}), f.RenderZooms);
}

TEST(WideCameraController, aStartZoomPastTheRangeAsksForTheWidestEvenWhileHeld)
{
    Fixture f;
    f.Controller.SetScriptRequest({.HoldNative = true});
    f.Controller.SetState({1, SETTINGS, 2.f});
    CHECK_NEAR(1.f, SOME(f.Frames(30, 192, 428, 25)).Zoom(), 0.f);
    CHECK_EQ((SessionZooms {{1, 512.f / 342.f}}), f.RenderZooms);
}
