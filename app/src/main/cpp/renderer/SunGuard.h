#ifndef SUNGUARD_H
#define SUNGUARD_H

#include <array>

namespace MelonDSAndroid
{

// The sun shadows' guard band in a frame's top screen half (melon.screen.sun's guard), in DS pixels: the columns left and right of the shown
// top screen and the rows above and below it that are drawn and filtered, so the sun's march finds casters just off screen, but not shown.
// The shown top screen sits between them with the native 256 x 192 in its middle, so its native column 0 is left + (shown width - 256) / 2
// columns into the frame and its DS line 0 top + shown margin rows down
struct FrameGuard
{
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    bool operator==(const FrameGuard&) const = default;
};

// The sides of the shown top screen the sun's march can leave it through, so the ones that need the guard band
struct SunGuardSides
{
    bool left = true;
    bool right = true;
    bool top = true;
    bool bottom = true;

    bool operator==(const SunGuardSides&) const = default;
};

/**
 * The sides a march towards [direction] (view space, towards the sun) leaves a shown top screen [shownWidth] wide with [shownMargin] rows
 * above and below through, from any of its pixels, under the camera [projection] (column-major, as melonDS::DepthCamera::Proj) with its
 * [viewport] (left, top, width and height in DS pixels from the native top screen's top-left corner). Each step adds P * (direction, 0) to
 * the clip position, so the screen x of a pixel at NDC x n moves right exactly where clip step x - n * clip step w > 0, which is linear in n:
 * the shown screen's two edges settle each side, and a sign change between them needs both. All four for a viewport with no size
 */
SunGuardSides pickSunGuardSides(const std::array<float, 3>& direction, const std::array<float, 16>& projection, const std::array<float, 4>& viewport,
                                int shownWidth, int shownMargin);

// A render view the OpenGL renderer is asked for (GLRenderer::SetViewWidth, SetViewMargin and SetViewShift), with the guard band in it
struct SunGuardView
{
    int width = 256;
    int margin = 0;
    int shiftX = 0;
    int shiftY = 0;
    FrameGuard guard;

    bool operator==(const SunGuardView&) const = default;
};

/**
 * The render view around the shown view [shownWidth] wide with [shownMargin] rows (both sanitized) for the guard band each side asks for
 * ([wanted], 0 for a side that needs none), as far as WideMelon's widest and tallest view take it, so the band shrinks rather than the shown
 * view. The same on all four sides is the four-sided band from before sides were picked, exactly; otherwise each guarded side is rounded up
 * to even, so the shift is whole, and a margin on a view that would stay 256 wide gets 2 columns on the right, as the margin needs a wide view
 */
SunGuardView sunGuardView(int shownWidth, int shownMargin, const FrameGuard& wanted);

/**
 * The view to ask for on the way to [next] while the renderer's polygons were built for [drawn] (GPU3D::RenderViewWidth and the rest) and the
 * ones being built for [building] (GPU3D::GetViewWidth and the rest), whose guards aren't read: [next], unless some of its columns or rows
 * past the native 256 x 192 (on one side) grow while others shrink against the larger of those two on each side. The
 * renderer's targets take the larger of the view's and the polygons' on each side until the game built geometry for the new view, which would
 * be a mix of the two that matches no view asked for, so then it's that mix (one more on the right or at the bottom for an odd count, as the
 * renderer has it) around the shown view of [next], until the polygons are built for it and [next] only shrinks from there. That mix is [next]
 * too when it doesn't fit WideMelon's widest or tallest view
 */
SunGuardView sunGuardStep(const SunGuardView& drawn, const SunGuardView& building, const SunGuardView& next);

/**
 * The guard band in a frame the OpenGL renderer drew [width] wide with [margin] rows and the shift [shiftX], [shiftY] (its output), the one
 * the view was asked with ([noted], or null if none was noted for it) when it fits that view and centres the shown screen on the native one.
 * Otherwise the least that does, the shift's own columns and rows on one side, so the shown screen is the native one with all of the view
 * that's centred on it
 */
FrameGuard frameGuardFor(int width, int margin, int shiftX, int shiftY, const FrameGuard* noted);

}

#endif //SUNGUARD_H
