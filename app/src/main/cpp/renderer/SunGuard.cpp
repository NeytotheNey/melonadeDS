#include "SunGuard.h"

#include <algorithm>
#include <cmath>
#include "WideMelon.h"

namespace MelonDSAndroid
{

namespace
{

// A view's columns left and right of native column 0 and the 256 and its rows above and below DS line 0 and the 192
struct ViewExtents
{
    int left;
    int right;
    int top;
    int bottom;
};

ViewExtents extentsOf(const SunGuardView& view)
{
    int wing = (view.width - WideMelon::NativeWidth) / 2;
    return { wing + view.shiftX, wing - view.shiftX, view.margin + view.shiftY, view.margin - view.shiftY };
}

int roundUpToEven(int value)
{
    return (value + 1) & ~1;
}

}

SunGuardSides pickSunGuardSides(const std::array<float, 3>& direction, const std::array<float, 16>& projection, const std::array<float, 4>& viewport,
                                int shownWidth, int shownMargin)
{
    const double viewportWidth = viewport[2];
    const double viewportHeight = viewport[3];
    if (!(viewportWidth > 0.0) || !(viewportHeight > 0.0))
        return {};

    // P * (direction, 0), column-major
    const double stepX = projection[0] * direction[0] + projection[4] * direction[1] + projection[8] * direction[2];
    const double stepY = projection[1] * direction[0] + projection[5] * direction[1] + projection[9] * direction[2];
    const double stepW = projection[3] * direction[0] + projection[7] * direction[1] + projection[11] * direction[2];
    if (!std::isfinite(stepX) || !std::isfinite(stepY) || !std::isfinite(stepW))
        return {};

    // The shown screen's edges in DS pixels, then in NDC (y up)
    const double wing = (shownWidth - WideMelon::NativeWidth) * 0.5;
    const double leftNdc = (-wing - viewport[0]) * 2.0 / viewportWidth - 1.0;
    const double rightNdc = (WideMelon::NativeWidth + wing - viewport[0]) * 2.0 / viewportWidth - 1.0;
    const double topNdc = 1.0 - (-shownMargin - viewport[1]) * 2.0 / viewportHeight;
    const double bottomNdc = 1.0 - (WideMelon::NativeHeight + shownMargin - viewport[1]) * 2.0 / viewportHeight;

    const double rightAtLeft = stepX - leftNdc * stepW;
    const double rightAtRight = stepX - rightNdc * stepW;
    const double upAtTop = stepY - topNdc * stepW;
    const double upAtBottom = stepY - bottomNdc * stepW;
    SunGuardSides sides;
    sides.left = rightAtLeft < 0.0 || rightAtRight < 0.0;
    sides.right = rightAtLeft > 0.0 || rightAtRight > 0.0;
    sides.top = upAtTop > 0.0 || upAtBottom > 0.0;
    sides.bottom = upAtTop < 0.0 || upAtBottom < 0.0;
    return sides;
}

int sunGuardForScale(int guard, int scale)
{
    // The largest scale the guard is used whole at
    static constexpr int FULL_GUARD_SCALE = 4;
    if (guard <= 0)
        return 0;
    if (scale <= FULL_GUARD_SCALE)
        return guard;
    const long long scaled = ((long long) guard * FULL_GUARD_SCALE + scale - 1) / scale;
    return (int) std::min<long long>(roundUpToEven((int) scaled), guard);
}

SunGuardView sunGuardView(int shownWidth, int shownMargin, const FrameGuard& wanted)
{
    SunGuardView view { shownWidth, shownMargin, 0, 0, {} };
    // Both even, as WideMelon's widths are
    const int roomX = std::max(WideMelon::MaxWidth - shownWidth, 0);
    const int roomY = std::max(WideMelon::MaxMargin - shownMargin, 0);
    const FrameGuard asked { std::max(wanted.left, 0), std::max(wanted.right, 0), std::max(wanted.top, 0), std::max(wanted.bottom, 0) };

    if (asked.left == asked.right && asked.left == asked.top && asked.left == asked.bottom)
    {
        int guard = std::min({ asked.left, roomX / 2, roomY });
        if (guard > 0)
        {
            view.width += 2 * guard;
            view.margin += guard;
            view.guard = { guard, guard, guard, guard };
        }
        return view;
    }

    // One guarded side of an axis can take all of its room, two share it
    const int capX = asked.left > 0 && asked.right > 0 ? (roomX / 2) & ~1 : roomX;
    const int capY = asked.top > 0 && asked.bottom > 0 ? roomY & ~1 : 2 * roomY;
    FrameGuard guard {
        std::min(roundUpToEven(asked.left), capX),
        std::min(roundUpToEven(asked.right), capX),
        std::min(roundUpToEven(asked.top), capY),
        std::min(roundUpToEven(asked.bottom), capY),
    };
    if (shownWidth == WideMelon::NativeWidth && guard.left == 0 && guard.right == 0 && guard.top + guard.bottom > 0)
        guard.right = 2;

    view.width = shownWidth + guard.left + guard.right;
    view.margin = shownMargin + (guard.top + guard.bottom) / 2;
    view.shiftX = (guard.left - guard.right) / 2;
    view.shiftY = (guard.top - guard.bottom) / 2;
    view.guard = guard;
    return view;
}

SunGuardView sunGuardStep(const SunGuardView& drawn, const SunGuardView& building, const SunGuardView& next)
{
    const ViewExtents drawnExtents = extentsOf(drawn);
    const ViewExtents buildingExtents = extentsOf(building);
    const ViewExtents from { std::max(drawnExtents.left, buildingExtents.left), std::max(drawnExtents.right, buildingExtents.right),
                             std::max(drawnExtents.top, buildingExtents.top), std::max(drawnExtents.bottom, buildingExtents.bottom) };
    const ViewExtents to = extentsOf(next);
    const bool grows = to.left > from.left || to.right > from.right || to.top > from.top || to.bottom > from.bottom;
    const bool shrinks = to.left < from.left || to.right < from.right || to.top < from.top || to.bottom < from.bottom;
    if (!grows || !shrinks)
        return next;

    ViewExtents both { std::max(from.left, to.left), std::max(from.right, to.right), std::max(from.top, to.top), std::max(from.bottom, to.bottom) };
    both.right += (both.left + both.right) & 1;
    both.bottom += (both.top + both.bottom) & 1;
    SunGuardView view;
    view.width = WideMelon::NativeWidth + both.left + both.right;
    view.margin = (both.top + both.bottom) / 2;
    if (view.width > WideMelon::MaxWidth || view.margin > WideMelon::MaxMargin)
        return next;
    view.shiftX = (both.left - both.right) / 2;
    view.shiftY = (both.top - both.bottom) / 2;

    // Around next's shown view, which is at least as big as each side's
    const int shownWing = (next.width - next.guard.left - next.guard.right - WideMelon::NativeWidth) / 2;
    const int shownMargin = next.margin - (next.guard.top + next.guard.bottom) / 2;
    view.guard = { both.left - shownWing, both.right - shownWing, both.top - shownMargin, both.bottom - shownMargin };
    return view;
}

FrameGuard frameGuardFor(int width, int margin, int shiftX, int shiftY, const FrameGuard* noted)
{
    if (noted)
    {
        const FrameGuard& guard = *noted;
        bool isValid = guard.left >= 0 && guard.right >= 0 && guard.top >= 0 && guard.bottom >= 0
            && guard.left - guard.right == 2 * shiftX && guard.top - guard.bottom == 2 * shiftY
            && guard.left + guard.right <= width - WideMelon::NativeWidth && guard.top + guard.bottom <= 2 * margin;
        if (isValid)
            return guard;
    }
    return { std::max(2 * shiftX, 0), std::max(-2 * shiftX, 0), std::max(2 * shiftY, 0), std::max(-2 * shiftY, 0) };
}

}
