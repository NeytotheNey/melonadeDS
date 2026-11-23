// The app's layout geometry, ported: where a layout's screen rects are drawn for a frame (ScreensDrawing.kt, WideTopScreen.kt), the
// default layouts' screens (DefaultLayoutProvider.kt's DefaultLayoutBuilder), and which of a layout's variants a window gets
// (UILayoutProvider.kt, with the desktop's rule for window sizes no variant was made for). Pure functions, any thread
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "layout/LayoutModel.h"

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

// ScreensDrawing.kt's ScreenDrawing, without the WideMelon camera's window (phase 3 step C) and the Lua drawings' place (phase 4): where a
// screen's picture goes, where its middle 256 columns end up (the touch screen's area), the columns of its half of the frame that are drawn
// (u) and its rows (DS pixels of its half)
struct ScreenDrawing
{
    IntRect DrawRect, NativeRect;
    float LeftU = 0, RightU = 1;
    float TopRow = 0, BottomRow = DS_SCREEN_HEIGHT;
};
struct ScreensDrawing
{
    std::optional<ScreenDrawing> Top, Bottom;
};

// getScreensDrawing: both screens from a [frameWidth]-wide frame. The screen in the layout's top rect gets the whole half (grown or
// letterboxed for a wide frame), the other its middle 256 columns. [topScreenRect] is the rect the DS top screen is drawn in (the layout's
// bottom one when [areScreensSwapped]); [frameMargin] the WideMelon camera's rows above and below the top screen, which only its window shows.
// [frameGuard] is the sun's guard band (FrameView::TopGuard), included in [frameWidth] and [frameMargin]: its columns on each side of both
// halves and its rows above and below the top screen are never shown, so the screens are placed as from a frame without it, the shown one,
// with their columns and rows moved in by the guard
ScreensDrawing GetScreensDrawing(const IntRect* topScreenRect, const IntRect* bottomScreenRect, int frameWidth, bool isWideLayout,
    bool areScreensSwapped, float surfaceWidth, int frameMargin, int frameGuard = 0);

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
