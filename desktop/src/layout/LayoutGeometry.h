// The app's layout geometry, ported: where a layout's screen rects are drawn for a frame (ScreensDrawing.kt, WideTopScreen.kt), the
// default layouts' screens (DefaultLayoutProvider.kt's DefaultLayoutBuilder), and which of a layout's variants a window gets
// (UILayoutProvider.kt, with the desktop's rule for window sizes no variant was made for). Pure functions, any thread
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "layout/LayoutModel.h"
#include "renderer/SunGuard.h"

namespace Frontend
{

// NDS.kt
constexpr int DS_SCREEN_WIDTH = 256;
constexpr int DS_SCREEN_HEIGHT = 192;
constexpr float CONSOLE_ASPECT_RATIO = (float)DS_SCREEN_WIDTH / DS_SCREEN_HEIGHT;

// Kotlin's roundToInt (halves up)
int RoundToInt(float value);

// WideTopScreen.kt: a rect centered on [centerX] with its edges rounded (not its size), so it stays centered
IntRect BuildCenteredRect(float centerX, float top, float width, float height);
// where a top screen drawn at [aspectRatio] goes for layout rect [rect]: centered if already wide, else grown sideways up to the surface's
// edges and [bottomRect] (a bottom screen sharing its rows), else shortened
IntRect GetWideTopScreenRect(const IntRect& rect, const IntRect* bottomRect, float aspectRatio, float surfaceWidth);
// the biggest [aspectRatio] rect centered in [rect], or [rect] itself when it's that shape within the tolerance
IntRect GetLetterboxedRect(const IntRect& rect, float aspectRatio);

// camera/WideCamera.h
struct WideWindow;

// ScreensDrawing.kt's OverlayDrawing: where a screen's Lua drawings go, in surface pixels, and the part of their 256 columns and 192 rows
// that shows (LeftU to RightU and TopV to BottomV, 0 to 1)
struct OverlayDrawing
{
    float Left = 0, Top = 0, Right = 0, Bottom = 0;
    float LeftU = 0, RightU = 1, TopV = 0, BottomV = 1;

    static OverlayDrawing Of(const IntRect& rect) { return {(float)rect.X, (float)rect.Y, (float)rect.Right(), (float)rect.Bottom()}; }
    bool operator==(const OverlayDrawing&) const = default;
};

// ScreensDrawing.kt's ScreenDrawing: where a screen's picture goes, where its middle 256 columns end up (the touch screen's area), the
// columns of its half of the frame that are drawn (u) and its rows (DS pixels of its half). [Overlay] is where its Lua drawings go: on
// [NativeRect], cut to [DrawRect] when a camera window leaves part of it out. [NativeRectAt1x] is [NativeRect] with a camera window at 1x,
// the largest any window shows the middle columns, for the output cap (the same without a camera)
struct ScreenDrawing
{
    IntRect DrawRect, NativeRect;
    float LeftU = 0, RightU = 1;
    float TopRow = 0, BottomRow = DS_SCREEN_HEIGHT;
    OverlayDrawing Overlay;
    IntRect NativeRectAt1x;

    bool operator==(const ScreenDrawing&) const = default;
};
struct ScreensDrawing
{
    std::optional<ScreenDrawing> Top, Bottom;

    bool operator==(const ScreensDrawing&) const = default;
};

// getScreensDrawing: both screens from a [frameWidth]-wide frame. The screen in the layout's top rect gets the whole half (grown or
// letterboxed for a wide frame), the other its middle 256 columns. [topScreenRect] is the rect the DS top screen is drawn in (the layout's
// bottom one when [areScreensSwapped]); [frameMargin] the WideMelon camera's rows above and below the top screen, which only its window shows.
// [frameGuard] is the sun's guard band (FrameView::TopGuard), included in [frameWidth] and [frameMargin]: its columns on the left and right of
// both halves and its rows above and below the top screen are never shown, so the screens are placed as from a frame without it, the shown
// one, with their columns moved in by the left guard and the top screen's rows down by the top one (the native screens, the bottom one too,
// are in the middle of the shown frame).
// [wideWindow] is the WideMelon camera's window of the screen in the layout's top rect, or null without a camera: that screen is then
// shaped for the window's show width and shows only the window, letterboxed by as much as the window's height can't cover. It's a window of
// the shown frame (WideCameraController takes the guard band out), so its FrameWidth is [frameWidth] less the guard on the left and right;
// a window for another frame width (one presented before a width change) is left out
ScreensDrawing GetScreensDrawing(const IntRect* topScreenRect, const IntRect* bottomScreenRect, int frameWidth, bool isWideLayout,
    bool areScreensSwapped, float surfaceWidth, int frameMargin, const MelonDSAndroid::FrameGuard& frameGuard = {},
    const WideWindow* wideWindow = nullptr);

// [window] if it's one GetScreensDrawing shows for a shown frame [shownFrameWidth] wide (the guard band left out): made for that width,
// with a show width between the DS's and the frame's; null otherwise
const WideWindow* UsableWideWindow(const WideWindow* window, int shownFrameWidth);
// How a screen shaped for [window]'s show width at [rect] shows that window of its shown frame: the window across the rect's width, as
// tall as that keeps the DS's pixel shape (so shorter, centered, when the window's rows can't cover what its width asks: no margin, or too
// small a one), with the frame's middle 256 columns and 192 rows wherever the window puts them and their Lua drawings cut to the picture.
// Its columns (u) and rows are of the shown frame, without a guard band
ScreenDrawing GetWindowedScreenDrawing(const IntRect& rect, const WideWindow& window);

// FrameGuard.coercedTo: [guard] kept within a frame [frameWidth] wide with [frameMargin] rows (MelonInstance does already), so a bad one can't
// turn the shown frame inside out: the same guard on every side to the least room any side has, otherwise each axis to its own room
MelonDSAndroid::FrameGuard CoercedFrameGuard(const MelonDSAndroid::FrameGuard& guard, int frameWidth, int frameMargin);

// DefaultLayoutBuilder's screens for a [width] x [height] surface with no insets (a window has none): both screens stacked and as big as
// they fit (portrait), the top screen at two thirds of the width with the bottom one beside it at the top (landscape), or one screen
// centered. The on-screen buttons around them aren't built: the desktop draws none
std::vector<LayoutComponent> BuildDefaultPortraitScreens(int width, int height);
std::vector<LayoutComponent> BuildDefaultLandscapeScreens(int width, int height);
LayoutComponent BuildSingleScreen(int width, int height, const char* component);
// DefaultLayoutProvider.buildDefaultLayout for a window: no folds, no second display, portrait unless it's wider than tall
UILayout BuildDefaultLayout(int width, int height);

// the variant a window gets, and how
struct PickedLayout
{
    enum class From { Exact, Scaled, Default };
    UILayout Layout;
    From Source = From::Default;
    // the variant used (an index into the list), -1 for the default layout
    int Variant = -1;
};

// The desktop's rule for which of [variants] a [width] x [height] window shows (desktop/README.md, Layouts):
//   1. variants made for a second display (the phone's external display or dual screens) are left out;
//   2. one made for exactly this size and orientation is used as it is (UILayoutProvider's match, insets and folds aside);
//   3. otherwise the closest one of the same orientation (the nearest aspect ratio, then the nearest area, then the first in the file) is
//      scaled to fit the window, keeping its shape, and centered;
//   4. without one of that orientation, [defaults] for the window's size (the app's default layout, as the app does for a size it has no
//      variant for).
// A variant without screens placed (components null: only a background) gets [defaults]' components, as populateLayoutIfRequired does
PickedLayout PickVariant(const std::vector<LayoutEntry>& variants, int width, int height,
    const std::function<UILayout(int width, int height)>& defaults);

}
