// ScreensDrawingTest.kt, case for case: the windowed cases and the rest, which pin GetScreensDrawing without a window too. Rects are the DS
// top screen's then the DS bottom screen's, as given to the renderer (swapped, the top one is the layout's bottom rect), and a 16:9 frame
// is 342 DS pixels wide
#include "Test.h"
#include "camera/WideCamera.h"
#include "layout/LayoutGeometry.h"

using namespace Frontend;
using MelonDSAndroid::FrameGuard;

namespace
{

constexpr int WIDE_FRAME = 342;
// sides of a 342 wide frame's half, as u coordinates: (342 - 256) / 2 / 342
const float NARROW_LEFT_U = 43.f / 342.f;

const IntRect WIDE_TOP_SLOT {0, 0, 640, 360};
const IntRect BOTTOM_SLOT {640, 0, 320, 240};
// the middle 256 of 342 columns of the top slot
const IntRect WIDE_TOP_SLOT_NATIVE {80, 0, 480, 360};

// the camera: 16:9 shown from a 512 wide frame
constexpr int CAMERA_FRAME = 512;
const WideWindow CAMERA_CENTERED = WideWindow::Centered(342, CAMERA_FRAME);
// stage 2: 48 rows of margin above and below, so the top screen's half is 288 rows
const WideWindow TALL_CENTERED = WideWindow::Centered(342, CAMERA_FRAME, 288);

// the sun's guard band: 64 DS pixels on each side and 64 rows above and below, which are never shown
constexpr int GUARD = 64;
const FrameGuard ALL_SIDES {GUARD, GUARD, GUARD, GUARD};

// getScreensDrawing with the Kotlin argument order and defaults
ScreensDrawing Drawing(std::optional<IntRect> top, std::optional<IntRect> bottom, int frameWidth, bool isWideLayout, bool areScreensSwapped,
    float surfaceWidth, const WideWindow* wideWindow = nullptr, int frameMargin = 0, const FrameGuard& frameGuard = {})
{
    return GetScreensDrawing(top ? &*top : nullptr, bottom ? &*bottom : nullptr, frameWidth, isWideLayout, areScreensSwapped, surfaceWidth,
        frameMargin, frameGuard, wideWindow);
}

// ScreensDrawing.touchScreenRect
std::optional<IntRect> TouchScreenRect(const ScreensDrawing& drawing)
{
    if (!drawing.Bottom)
        return std::nullopt;
    return drawing.Bottom->NativeRect;
}

// a zoomed-out window of the whole 512 columns, with the rows that keep the pixel shape in a 288-row frame
WideWindow ZoomedOutTall()
{
    float height = 192.f * 512.f / 342.f;
    return {342, CAMERA_FRAME, 0.f, 512.f, 288, (288.f - height) / 2.f, height};
}

}

#define CHECK_SCREEN(screen, drawRect, nativeRect, leftU, rightU) \
    do \
    { \
        const std::optional<ScreenDrawing>& screen_ = (screen); \
        CHECK(screen_.has_value()); \
        CHECK_EQ(drawRect, screen_->DrawRect); \
        CHECK_EQ(nativeRect, screen_->NativeRect); \
        CHECK_NEAR(leftU, screen_->LeftU, 0.0001f); \
        CHECK_NEAR(rightU, screen_->RightU, 0.0001f); \
    } while (0)

#define CHECK_ROWS(screen, topRow, bottomRow) \
    do \
    { \
        const std::optional<ScreenDrawing>& screen_ = (screen); \
        CHECK(screen_.has_value()); \
        CHECK_NEAR(topRow, screen_->TopRow, 0.001f); \
        CHECK_NEAR(bottomRow, screen_->BottomRow, 0.001f); \
    } while (0)

// the same places on the surface: rects and Lua drawings
#define CHECK_SAME_PLACES_OF(want, got) \
    do \
    { \
        CHECK((want).has_value() && (got).has_value()); \
        CHECK_EQ((want)->DrawRect, (got)->DrawRect); \
        CHECK_EQ((want)->NativeRect, (got)->NativeRect); \
        CHECK_EQ((want)->NativeRectAt1x, (got)->NativeRectAt1x); \
        CHECK_EQ((want)->Overlay, (got)->Overlay); \
    } while (0)

#define CHECK_SAME_PLACES(expected, actual) \
    do \
    { \
        CHECK_SAME_PLACES_OF((expected).Top, (actual).Top); \
        CHECK_SAME_PLACES_OF((expected).Bottom, (actual).Bottom); \
    } while (0)

TEST(ScreensDrawing, notSwappedTheTopScreenIsWideAndTheBottomOneNarrow)
{
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, WIDE_FRAME, true, false, 960.f);

    CHECK_SCREEN(drawing.Top, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 0.f, 1.f);
    CHECK_SCREEN(drawing.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, NARROW_LEFT_U, 1.f - NARROW_LEFT_U);
    CHECK_EQ(BOTTOM_SLOT, TouchScreenRect(drawing));
}

TEST(ScreensDrawing, notSwappedAStretchedBottomScreenIsUsedAsItIs)
{
    // made 16:9 by the player in the editor
    IntRect stretchedBottomSlot {640, 0, 320, 180};
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, stretchedBottomSlot, WIDE_FRAME, true, false, 960.f);

    CHECK_SCREEN(drawing.Bottom, stretchedBottomSlot, stretchedBottomSlot, NARROW_LEFT_U, 1.f - NARROW_LEFT_U);
    CHECK_EQ(stretchedBottomSlot, TouchScreenRect(drawing));
}

TEST(ScreensDrawing, swappedTheBottomScreenIsWideAndTouchesGoToItsMiddle)
{
    ScreensDrawing drawing = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, WIDE_FRAME, true, true, 960.f);

    CHECK_SCREEN(drawing.Bottom, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 0.f, 1.f);
    CHECK_SCREEN(drawing.Top, BOTTOM_SLOT, BOTTOM_SLOT, NARROW_LEFT_U, 1.f - NARROW_LEFT_U);
    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, TouchScreenRect(drawing));
}

TEST(ScreensDrawing, swappedInAWideLayoutTheTopScreenKeeps4By3InAStretchedRect)
{
    IntRect stretchedBottomSlot {640, 0, 320, 180};
    ScreensDrawing drawing = Drawing(stretchedBottomSlot, WIDE_TOP_SLOT, WIDE_FRAME, true, true, 960.f);

    CHECK_SCREEN(drawing.Top, (IntRect {680, 0, 240, 180}), (IntRect {680, 0, 240, 180}), NARROW_LEFT_U, 1.f - NARROW_LEFT_U);
}

TEST(ScreensDrawing, swappedInAWideLayoutTheTopScreenKeeps4By3InATallRect)
{
    IntRect tallBottomSlot {0, 0, 300, 485};
    ScreensDrawing drawing = Drawing(tallBottomSlot, WIDE_TOP_SLOT, WIDE_FRAME, true, true, 960.f);

    CHECK(drawing.Top.has_value());
    CHECK_EQ((IntRect {0, 130, 300, 225}), drawing.Top->DrawRect);
}

TEST(ScreensDrawing, swappedWithANarrowFrameTheBottomScreenIsFittedAt4By3)
{
    // widescreen off, or a frame from before the width changed
    ScreensDrawing drawing = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, 256, true, true, 960.f);

    CHECK_SCREEN(drawing.Bottom, (IntRect {80, 0, 480, 360}), (IntRect {80, 0, 480, 360}), 0.f, 1.f);
    CHECK_SCREEN(drawing.Top, BOTTOM_SLOT, BOTTOM_SLOT, 0.f, 1.f);
    CHECK_EQ((IntRect {80, 0, 480, 360}), TouchScreenRect(drawing));
}

TEST(ScreensDrawing, swappedInALayoutThatIsNotWideTheBottomScreenGrowsUpToTheOtherScreen)
{
    // a 4:3 top slot right beside the bottom slot can't grow sideways, so the wide picture gets shorter instead
    IntRect topSlot {0, 0, 480, 360};
    IntRect besideSlot {480, 0, 320, 240};
    ScreensDrawing drawing = Drawing(besideSlot, topSlot, WIDE_FRAME, false, true, 1000.f);

    CHECK_SCREEN(drawing.Bottom, (IntRect {0, 46, 480, 269}), (IntRect {60, 46, 360, 269}), 0.f, 1.f);
    CHECK_SCREEN(drawing.Top, besideSlot, besideSlot, NARROW_LEFT_U, 1.f - NARROW_LEFT_U);
    CHECK_EQ((IntRect {60, 46, 360, 269}), TouchScreenRect(drawing));
}

TEST(ScreensDrawing, missingScreensAreLeftOut)
{
    // a screen on another display, say
    ScreensDrawing drawing = Drawing(std::nullopt, WIDE_TOP_SLOT, WIDE_FRAME, true, true, 960.f);

    CHECK_NONE(drawing.Top);
    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, TouchScreenRect(drawing));
    CHECK_NONE(TouchScreenRect(Drawing(WIDE_TOP_SLOT, std::nullopt, WIDE_FRAME, true, false, 960.f)));
}

TEST(ScreensDrawing, aCenteredCameraWindowLooksLikeAFrameAtTheShowWidth)
{
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &CAMERA_CENTERED);

    CHECK_SCREEN(drawing.Top, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 85.f / 512.f, 427.f / 512.f);
    // the bottom screen is still the frame's middle 256
    CHECK_SCREEN(drawing.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 0.25f, 0.75f);
    OverlayDrawing overlay = drawing.Top->Overlay;
    CHECK_NEAR(0.f, overlay.LeftU, 0.0001f);
    CHECK_NEAR(1.f, overlay.RightU, 0.0001f);
    CHECK_NEAR(80.47f, overlay.Left, 0.01f);
}

TEST(ScreensDrawing, aZoomedOutWindowIsLetterboxed)
{
    WideWindow window {342, CAMERA_FRAME, 0.f, 512.f};
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &window);

    // 360 * 342 / 512 tall, with the middle 256 of 512 columns in the middle half
    CHECK_SCREEN(drawing.Top, (IntRect {0, 60, 640, 240}), (IntRect {160, 60, 320, 240}), 0.f, 1.f);
    // the output cap still sizes the frame for 1x, so zooming doesn't change the internal scale
    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, drawing.Top->NativeRectAt1x);
    // without a margin it's all 192 rows
    CHECK_ROWS(drawing.Top, 0.f, 192.f);
}

TEST(ScreensDrawing, aZoomedOutWindowWithAMarginFillsTheScreen)
{
    float height = 192.f * 512.f / 342.f;
    WideWindow window = ZoomedOutTall();
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &window);

    // the whole rect, with the middle 256 of 512 columns and 192 of the window's 287.4 rows in the middle
    CHECK_SCREEN(drawing.Top, WIDE_TOP_SLOT, (IntRect {160, 60, 320, 240}), 0.f, 1.f);
    CHECK_ROWS(drawing.Top, (288.f - height) / 2.f, (288.f + height) / 2.f);
    OverlayDrawing overlay = drawing.Top->Overlay;
    CHECK_NEAR(59.77f, overlay.Top, 0.01f);
    CHECK_NEAR(300.23f, overlay.Bottom, 0.01f);
    CHECK_NEAR(0.f, overlay.TopV, 0.0001f);
    CHECK_NEAR(1.f, overlay.BottomV, 0.0001f);
    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, drawing.Top->NativeRectAt1x);
}

TEST(ScreensDrawing, atOneXWithAMarginItsTheSameRectsShowingTheMiddleRows)
{
    ScreensDrawing withMargin = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &TALL_CENTERED, 48);
    ScreensDrawing without = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &CAMERA_CENTERED);

    CHECK_SCREEN(withMargin.Top, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 85.f / 512.f, 427.f / 512.f);
    CHECK_ROWS(withMargin.Top, 48.f, 240.f);
    CHECK(without.Top.has_value());
    CHECK_EQ(without.Top->Overlay, withMargin.Top->Overlay);
    CHECK_EQ(without.Bottom, withMargin.Bottom);
    CHECK_ROWS(withMargin.Bottom, 0.f, 192.f);
}

TEST(ScreensDrawing, aWindowPannedUpCutsTheDrawingsAtThePicturesBottom)
{
    WideWindow window {342, CAMERA_FRAME, 85.f, 342.f, 288, 0.f, 192.f};
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &window);

    // the top 48 rows are margin, so DS line 0 is a quarter of the way down and the screen runs past the picture
    CHECK_SCREEN(drawing.Top, WIDE_TOP_SLOT, (IntRect {80, 90, 480, 360}), 85.f / 512.f, 427.f / 512.f);
    CHECK_ROWS(drawing.Top, 0.f, 192.f);
    OverlayDrawing overlay = drawing.Top->Overlay;
    CHECK_NEAR(90.f, overlay.Top, 0.01f);
    CHECK_NEAR(360.f, overlay.Bottom, 0.01f);
    CHECK_NEAR(0.f, overlay.TopV, 0.0001f);
    CHECK_NEAR(0.75f, overlay.BottomV, 0.0001f);
}

TEST(ScreensDrawing, withoutAWindowTheMarginIsLeftOut)
{
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, nullptr, 48);
    ScreensDrawing without = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f);

    CHECK_ROWS(drawing.Top, 48.f, 240.f);
    CHECK_ROWS(drawing.Bottom, 0.f, 192.f);
    CHECK(without.Top.has_value());
    CHECK_EQ(without.Top->DrawRect, drawing.Top->DrawRect);
    CHECK_ROWS(without.Top, 0.f, 192.f);

    // swapped, the DS top screen is the narrow one and still shows its middle rows
    ScreensDrawing swapped = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, CAMERA_FRAME, true, true, 960.f, nullptr, 48);
    CHECK_ROWS(swapped.Top, 48.f, 240.f);
    CHECK_ROWS(swapped.Bottom, 0.f, 192.f);
}

TEST(ScreensDrawing, swappedTheWindowLeavesTheBottomScreensRowsAlone)
{
    ScreensDrawing drawing = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, CAMERA_FRAME, true, true, 960.f, &TALL_CENTERED, 48);

    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, TouchScreenRect(drawing));
    CHECK_ROWS(drawing.Bottom, 0.f, 192.f);
    CHECK_ROWS(drawing.Top, 48.f, 240.f);
}

TEST(ScreensDrawing, aPannedWindowCutsTheDrawingsToThePicture)
{
    WideWindow window {342, CAMERA_FRAME, 0.f, 342.f};
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &window);

    CHECK(drawing.Top.has_value());
    OverlayDrawing overlay = drawing.Top->Overlay;
    CHECK_NEAR(640.f, overlay.Right, 0.01f);
    CHECK_NEAR(0.f, overlay.LeftU, 0.0001f);
    // columns 128 to 341 of the frame are the middle 256's first 214
    CHECK_NEAR(214.f / 256.f, overlay.RightU, 0.0001f);
    CHECK_NEAR(0.f, drawing.Top->LeftU, 0.0001f);
    CHECK_NEAR(342.f / 512.f, drawing.Top->RightU, 0.0001f);
}

TEST(ScreensDrawing, swappedTouchesFollowTheCenteredWindow)
{
    ScreensDrawing drawing = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, CAMERA_FRAME, true, true, 960.f, &CAMERA_CENTERED);

    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, TouchScreenRect(drawing));
}

TEST(ScreensDrawing, aWindowForAnotherFrameWidthIsLeftOut)
{
    ScreensDrawing withWindow = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, WIDE_FRAME, true, false, 960.f, &CAMERA_CENTERED);
    ScreensDrawing without = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, WIDE_FRAME, true, false, 960.f);

    CHECK_EQ(without, withWindow);
}

TEST(ScreensDrawing, aGuardOf0ChangesNothing)
{
    ScreensDrawing plain = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &TALL_CENTERED, 48);
    ScreensDrawing guarded = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &TALL_CENTERED, 48, FrameGuard {});

    CHECK_EQ(plain, guarded);
}

TEST(ScreensDrawing, aGuardAt256ShownIsCutOffAndTheScreensLandAsWithout)
{
    IntRect topSlot {0, 0, 640, 480};
    ScreensDrawing plain = Drawing(topSlot, BOTTOM_SLOT, 256, false, false, 960.f);
    // WideMelon off: 256 + 2 * 64 columns and 64 margin rows, all of them guard
    ScreensDrawing guarded = Drawing(topSlot, BOTTOM_SLOT, 384, false, false, 960.f, nullptr, GUARD, ALL_SIDES);

    CHECK_SCREEN(guarded.Top, topSlot, topSlot, 64.f / 384.f, 320.f / 384.f);
    CHECK_ROWS(guarded.Top, 64.f, 256.f);
    CHECK_SCREEN(guarded.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 64.f / 384.f, 320.f / 384.f);
    CHECK_ROWS(guarded.Bottom, 0.f, 192.f);
    CHECK_SAME_PLACES(plain, guarded);
}

TEST(ScreensDrawing, aGuardAt342ShownIsCutOffAndTheScreensLandAsWithout)
{
    ScreensDrawing plain = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, WIDE_FRAME, true, false, 960.f);
    ScreensDrawing guarded = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 470, true, false, 960.f, nullptr, GUARD, ALL_SIDES);

    // the shown 342 columns are 64 to 406 of 470, and the bottom screen is still the middle 256
    CHECK_SCREEN(guarded.Top, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 64.f / 470.f, 406.f / 470.f);
    CHECK_ROWS(guarded.Top, 64.f, 256.f);
    CHECK_SCREEN(guarded.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 107.f / 470.f, 363.f / 470.f);
    CHECK_SAME_PLACES(plain, guarded);
    // the output cap sizes the frame for the shown screen, not the guard
    CHECK_EQ(plain.Top->NativeRectAt1x, guarded.Top->NativeRectAt1x);
}

TEST(ScreensDrawing, swappedAGuardIsCutOffBothScreens)
{
    ScreensDrawing plain = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, WIDE_FRAME, true, true, 960.f);
    ScreensDrawing guarded = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, 470, true, true, 960.f, nullptr, GUARD, ALL_SIDES);

    // the wide DS bottom screen shows the shown 342 columns and its own rows, the narrow DS top screen the middle 256 and its 192 rows
    CHECK_SCREEN(guarded.Bottom, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 64.f / 470.f, 406.f / 470.f);
    CHECK_ROWS(guarded.Bottom, 0.f, 192.f);
    CHECK_SCREEN(guarded.Top, BOTTOM_SLOT, BOTTOM_SLOT, 107.f / 470.f, 363.f / 470.f);
    CHECK_ROWS(guarded.Top, 64.f, 256.f);
    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, TouchScreenRect(guarded));
    CHECK_SAME_PLACES(plain, guarded);
}

TEST(ScreensDrawing, aZoomedOutWindowWithAGuardShowsTheWholeShownFrameAndNoneOfTheGuard)
{
    // the camera's shown frame is 512 wide with 48 margin rows; the guard makes it 640 wide with 112
    float height = 192.f * 512.f / 342.f;
    WideWindow window = ZoomedOutTall();
    ScreensDrawing plain = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &window, 48);
    ScreensDrawing guarded = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 640, true, false, 960.f, &window, 112, ALL_SIDES);

    CHECK_SCREEN(guarded.Top, WIDE_TOP_SLOT, (IntRect {160, 60, 320, 240}), 64.f / 640.f, 576.f / 640.f);
    CHECK_ROWS(guarded.Top, 64.f + (288.f - height) / 2.f, 64.f + (288.f + height) / 2.f);
    CHECK_SCREEN(guarded.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 192.f / 640.f, 448.f / 640.f);
    CHECK_SAME_PLACES(plain, guarded);
}

TEST(ScreensDrawing, aWindowPannedToTheLimitsWithAGuardStopsAtTheGuard)
{
    // panned all the way left and up at 1x, and all the way right and down
    WideWindow topLeft {342, CAMERA_FRAME, 0.f, 342.f, 288, 0.f, 192.f};
    WideWindow bottomRight {342, CAMERA_FRAME, 170.f, 342.f, 288, 96.f, 192.f};
    ScreensDrawing atTopLeft = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 640, true, false, 960.f, &topLeft, 112, ALL_SIDES);
    ScreensDrawing atBottomRight = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 640, true, false, 960.f, &bottomRight, 112, ALL_SIDES);

    CHECK(atTopLeft.Top.has_value() && atBottomRight.Top.has_value());
    CHECK_NEAR(64.f / 640.f, atTopLeft.Top->LeftU, 0.0001f);
    CHECK_NEAR(406.f / 640.f, atTopLeft.Top->RightU, 0.0001f);
    CHECK_ROWS(atTopLeft.Top, 64.f, 256.f);
    CHECK_NEAR(234.f / 640.f, atBottomRight.Top->LeftU, 0.0001f);
    CHECK_NEAR(576.f / 640.f, atBottomRight.Top->RightU, 0.0001f);
    CHECK_ROWS(atBottomRight.Top, 160.f, 352.f);
    ScreensDrawing plain = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &bottomRight, 48);
    CHECK_SAME_PLACES(plain, atBottomRight);
}

TEST(ScreensDrawing, aGuardTooBigForTheFrameIsKeptToIt)
{
    // 300 columns leave room for 22 on each side, and the 10 margin rows for 10 of them
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 300, true, false, 960.f, nullptr, 10, ALL_SIDES);

    CHECK(drawing.Top.has_value());
    CHECK_NEAR(10.f / 300.f, drawing.Top->LeftU, 0.0001f);
    CHECK_NEAR(290.f / 300.f, drawing.Top->RightU, 0.0001f);
    CHECK_ROWS(drawing.Top, 10.f, 202.f);
}

// the guard band on some sides only, the ones the sun's march can leave the screen through: 64 columns and rows on each of those

TEST(ScreensDrawing, aLeftAndTopGuardAt342IsCutOffAndTheBottomScreenSitsUnderTheNativeColumns)
{
    ScreensDrawing plain = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, WIDE_FRAME, true, false, 960.f);
    // 342 + 64 columns, and 32 margin rows that are all the top guard's
    ScreensDrawing guarded = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 406, true, false, 960.f, nullptr, 32, FrameGuard {.left = 64, .top = 64});

    // the shown 342 columns are 64 to 406 of 406, and the native 256 (the bottom screen's too) 107 to 363
    CHECK_SCREEN(guarded.Top, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 64.f / 406.f, 1.f);
    CHECK_ROWS(guarded.Top, 64.f, 256.f);
    CHECK_SCREEN(guarded.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 107.f / 406.f, 363.f / 406.f);
    CHECK_ROWS(guarded.Bottom, 0.f, 192.f);
    CHECK_SAME_PLACES(plain, guarded);
    CHECK_EQ(plain.Top->NativeRectAt1x, guarded.Top->NativeRectAt1x);
}

TEST(ScreensDrawing, aRightAndBottomGuardAt256ShownIsCutOff)
{
    IntRect topSlot {0, 0, 640, 480};
    ScreensDrawing plain = Drawing(topSlot, BOTTOM_SLOT, 256, false, false, 960.f);
    ScreensDrawing guarded = Drawing(topSlot, BOTTOM_SLOT, 320, false, false, 960.f, nullptr, 32, FrameGuard {.right = 64, .bottom = 64});

    CHECK_SCREEN(guarded.Top, topSlot, topSlot, 0.f, 256.f / 320.f);
    CHECK_ROWS(guarded.Top, 0.f, 192.f);
    CHECK_SCREEN(guarded.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 0.f, 256.f / 320.f);
    CHECK_SAME_PLACES(plain, guarded);
}

TEST(ScreensDrawing, swappedALeftAndTopGuardIsCutOffBothScreens)
{
    ScreensDrawing plain = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, WIDE_FRAME, true, true, 960.f);
    ScreensDrawing guarded = Drawing(BOTTOM_SLOT, WIDE_TOP_SLOT, 406, true, true, 960.f, nullptr, 32, FrameGuard {.left = 64, .top = 64});

    // the wide DS bottom screen shows the shown 342 columns and its own rows, the narrow DS top screen the native 256 and its 192 rows
    CHECK_SCREEN(guarded.Bottom, WIDE_TOP_SLOT, WIDE_TOP_SLOT_NATIVE, 64.f / 406.f, 1.f);
    CHECK_ROWS(guarded.Bottom, 0.f, 192.f);
    CHECK_SCREEN(guarded.Top, BOTTOM_SLOT, BOTTOM_SLOT, 107.f / 406.f, 363.f / 406.f);
    CHECK_ROWS(guarded.Top, 64.f, 256.f);
    CHECK_EQ(WIDE_TOP_SLOT_NATIVE, TouchScreenRect(guarded));
    CHECK_SAME_PLACES(plain, guarded);
}

TEST(ScreensDrawing, aZoomedOutWindowWithALeftAndBottomGuardShowsTheWholeShownFrameAndNoneOfTheGuard)
{
    // the camera's shown frame is 512 wide with 48 margin rows; the guard makes it 576 wide with 80
    float height = 192.f * 512.f / 342.f;
    WideWindow window = ZoomedOutTall();
    ScreensDrawing plain = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, CAMERA_FRAME, true, false, 960.f, &window, 48);
    ScreensDrawing guarded = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 576, true, false, 960.f, &window, 80, FrameGuard {.left = 64, .bottom = 64});

    CHECK_SCREEN(guarded.Top, WIDE_TOP_SLOT, (IntRect {160, 60, 320, 240}), 64.f / 576.f, 1.f);
    CHECK_ROWS(guarded.Top, (288.f - height) / 2.f, (288.f + height) / 2.f);
    CHECK_SCREEN(guarded.Bottom, BOTTOM_SLOT, BOTTOM_SLOT, 192.f / 576.f, 448.f / 576.f);
    CHECK_SAME_PLACES(plain, guarded);
}

TEST(ScreensDrawing, aOneSidedGuardTooBigForTheFrameIsKeptToItsAxis)
{
    // 300 columns leave room for 44 on one side, and the 10 margin rows for 20 rows above
    ScreensDrawing drawing = Drawing(WIDE_TOP_SLOT, BOTTOM_SLOT, 300, true, false, 960.f, nullptr, 10, FrameGuard {.left = 64, .top = 64});

    CHECK(drawing.Top.has_value());
    CHECK_NEAR(44.f / 300.f, drawing.Top->LeftU, 0.0001f);
    CHECK_NEAR(1.f, drawing.Top->RightU, 0.0001f);
    CHECK_ROWS(drawing.Top, 20.f, 212.f);
}
