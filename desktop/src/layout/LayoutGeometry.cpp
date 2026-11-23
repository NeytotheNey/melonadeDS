#include "layout/LayoutGeometry.h"

#include <algorithm>
#include <cmath>

namespace Frontend
{

namespace
{

// WideTopScreen.kt: tolerance in pixels below which a rect counts as already wide, so the editor's rounding doesn't widen a rect again
constexpr int WIDE_RECT_TOLERANCE = 2;

IntRect GetNativeScreenRect(const IntRect& drawRect, int frameWidth)
{
    if (frameWidth <= DS_SCREEN_WIDTH)
        return drawRect;
    float nativeWidth = drawRect.W * DS_SCREEN_WIDTH / (float)frameWidth;
    return BuildCenteredRect(drawRect.X + drawRect.W / 2.f, (float)drawRect.Y, nativeWidth, (float)drawRect.H);
}

// the draw rect of the screen in the layout's top rect: letterboxed in a wide layout, grown by GetWideTopScreenRect otherwise
IntRect GetWideScreenDrawRect(const IntRect& rect, const IntRect* otherRect, int frameWidth, bool isWideLayout, float surfaceWidth)
{
    if (isWideLayout)
        return GetLetterboxedRect(rect, std::max(frameWidth, DS_SCREEN_WIDTH) / (float)DS_SCREEN_HEIGHT);
    if (frameWidth <= DS_SCREEN_WIDTH)
        return rect;
    return GetWideTopScreenRect(rect, otherRect, frameWidth / (float)DS_SCREEN_HEIGHT, surfaceWidth);
}

LayoutComponent Component(const IntRect& rect, const char* name)
{
    LayoutComponent c;
    c.Rect = rect;
    c.Component = name;
    return c;
}

// centerScreenIn: the biggest 4:3 rect centered in the area
IntRect CenterScreenIn(int width, int height)
{
    float areaAspectRatio = (float)width / height;
    if (areaAspectRatio > CONSOLE_ASPECT_RATIO)
    {
        float scale = (float)height / DS_SCREEN_HEIGHT;
        int scaledWidth = (int)(DS_SCREEN_WIDTH * scale);
        return {(width - scaledWidth) / 2, 0, scaledWidth, height};
    }
    float scale = (float)width / DS_SCREEN_WIDTH;
    int scaledHeight = (int)(DS_SCREEN_HEIGHT * scale);
    return {0, (height - scaledHeight) / 2, width, scaledHeight};
}

// [layout] scaled by [scale] from its variant's origin and moved by [dx], [dy]: every rect's edges rounded, so neighbors stay neighbors
void ScaleComponents(ScreenLayout& layout, float scale, float dx, float dy)
{
    if (!layout.Components)
        return;
    for (LayoutComponent& c : *layout.Components)
    {
        int left = RoundToInt(dx + c.Rect.X * scale), top = RoundToInt(dy + c.Rect.Y * scale);
        int right = RoundToInt(dx + c.Rect.Right() * scale), bottom = RoundToInt(dy + c.Rect.Bottom() * scale);
        c.Rect = {left, top, right - left, bottom - top};
    }
}

// getScreensDrawing for a frame without a guard band: the shown one
ScreensDrawing GetShownScreensDrawing(const IntRect* topScreenRect, const IntRect* bottomScreenRect, int frameWidth, bool isWideLayout,
    bool areScreensSwapped, float surfaceWidth, int frameMargin)
{
    const IntRect* wideScreenRect = areScreensSwapped ? bottomScreenRect : topScreenRect;
    const IntRect* narrowScreenRect = areScreensSwapped ? topScreenRect : bottomScreenRect;
    // the DS top screen's native rows, which a screen shows without the camera's window
    float topScreenRows = (float)std::max(frameMargin, 0);

    std::optional<ScreenDrawing> wideScreen, narrowScreen;
    if (wideScreenRect)
    {
        ScreenDrawing d;
        d.DrawRect = GetWideScreenDrawRect(*wideScreenRect, narrowScreenRect, frameWidth, isWideLayout, surfaceWidth);
        d.NativeRect = GetNativeScreenRect(d.DrawRect, frameWidth);
        d.TopRow = areScreensSwapped ? 0.f : topScreenRows;
        d.BottomRow = d.TopRow + DS_SCREEN_HEIGHT;
        wideScreen = d;
    }
    if (narrowScreenRect)
    {
        ScreenDrawing d;
        d.DrawRect = isWideLayout && areScreensSwapped ? GetLetterboxedRect(*narrowScreenRect, CONSOLE_ASPECT_RATIO) : *narrowScreenRect;
        d.NativeRect = d.DrawRect;
        int width = std::max(frameWidth, DS_SCREEN_WIDTH);
        d.LeftU = (width - DS_SCREEN_WIDTH) / (2.f * width);
        d.RightU = 1.f - d.LeftU;
        d.TopRow = areScreensSwapped ? topScreenRows : 0.f;
        d.BottomRow = d.TopRow + DS_SCREEN_HEIGHT;
        narrowScreen = d;
    }
    ScreensDrawing drawing;
    drawing.Top = areScreensSwapped ? narrowScreen : wideScreen;
    drawing.Bottom = areScreensSwapped ? wideScreen : narrowScreen;
    return drawing;
}

}

int RoundToInt(float value)
{
    return (int)std::floor((double)value + 0.5);
}

IntRect BuildCenteredRect(float centerX, float top, float width, float height)
{
    int left = RoundToInt(centerX - width / 2.f);
    int right = RoundToInt(centerX + width / 2.f);
    int y = RoundToInt(top);
    return {left, y, right - left, RoundToInt(top + height) - y};
}

IntRect GetWideTopScreenRect(const IntRect& rect, const IntRect* bottomRect, float aspectRatio, float surfaceWidth)
{
    float centerX = rect.X + rect.W / 2.f;
    float wantedWidth = rect.H * aspectRatio;
    if (rect.W >= wantedWidth)
        return BuildCenteredRect(centerX, (float)rect.Y, wantedWidth, (float)rect.H);
    if (rect.W >= wantedWidth - WIDE_RECT_TOLERANCE)
        return rect;

    // a bottom screen that shares rows stops the growth at its near edge, on whichever side its center is
    float minX = 0, maxX = surfaceWidth;
    if (bottomRect && bottomRect->Y < rect.Bottom() && rect.Y < bottomRect->Bottom())
    {
        if (bottomRect->X + bottomRect->W / 2.f >= centerX)
            maxX = std::min(maxX, (float)std::max(bottomRect->X, rect.Right()));
        else
            minX = std::max(minX, (float)std::min(bottomRect->Right(), rect.X));
    }

    // never narrower than the layout's own rect; the height is rounded first so the result lands within the tolerance of the ratio
    float drawWidth = std::max(std::min(wantedWidth, 2.f * std::min(centerX - minX, maxX - centerX)), (float)rect.W);
    float drawHeight = (float)RoundToInt(drawWidth / aspectRatio);
    return BuildCenteredRect(centerX, rect.Y + (rect.H - drawHeight) / 2.f, drawHeight * aspectRatio, drawHeight);
}

IntRect GetLetterboxedRect(const IntRect& rect, float aspectRatio)
{
    if (std::fabs(rect.W - rect.H * aspectRatio) <= WIDE_RECT_TOLERANCE)
        return rect;
    float fittedWidth = std::min((float)rect.W, rect.H * aspectRatio);
    float fittedHeight = fittedWidth / aspectRatio;
    return BuildCenteredRect(rect.X + rect.W / 2.f, rect.Y + (rect.H - fittedHeight) / 2.f, fittedWidth, fittedHeight);
}

MelonDSAndroid::FrameGuard CoercedFrameGuard(const MelonDSAndroid::FrameGuard& guard, int frameWidth, int frameMargin)
{
    int roomX = std::max(0, frameWidth - DS_SCREEN_WIDTH);
    int roomY = std::max(0, 2 * frameMargin);
    if (guard.left == guard.right && guard.left == guard.top && guard.left == guard.bottom)
    {
        int all = std::clamp(guard.left, 0, std::min(roomX / 2, roomY / 2));
        return {all, all, all, all};
    }
    int left = std::clamp(guard.left, 0, roomX);
    int top = std::clamp(guard.top, 0, roomY);
    return {left, std::clamp(guard.right, 0, roomX - left), top, std::clamp(guard.bottom, 0, roomY - top)};
}

ScreensDrawing GetScreensDrawing(const IntRect* topScreenRect, const IntRect* bottomScreenRect, int frameWidth, bool isWideLayout,
    bool areScreensSwapped, float surfaceWidth, int frameMargin, const MelonDSAndroid::FrameGuard& frameGuard)
{
    // MelonInstance keeps the guard within the frame already; kept there here too, so a bad one can't turn the shown frame inside out
    MelonDSAndroid::FrameGuard guard = CoercedFrameGuard(frameGuard, frameWidth, frameMargin);
    int shownWidth = frameWidth - guard.left - guard.right;
    ScreensDrawing shown = GetShownScreensDrawing(topScreenRect, bottomScreenRect, shownWidth, isWideLayout, areScreensSwapped, surfaceWidth,
        frameMargin - (guard.top + guard.bottom) / 2);
    if (guard == MelonDSAndroid::FrameGuard {})
        return shown;

    // The shown frame's columns sit left guard columns into the frame's, in both halves, and its rows top guard rows into the DS top screen's
    // half
    auto inFrame = [&](std::optional<ScreenDrawing>& screen, float rowOffset)
    {
        if (!screen)
            return;
        screen->LeftU = (guard.left + screen->LeftU * shownWidth) / frameWidth;
        screen->RightU = (guard.left + screen->RightU * shownWidth) / frameWidth;
        screen->TopRow += rowOffset;
        screen->BottomRow += rowOffset;
    };
    inFrame(shown.Top, (float)guard.top);
    inFrame(shown.Bottom, 0.f);
    return shown;
}

std::vector<LayoutComponent> BuildDefaultPortraitScreens(int width, int height)
{
    int screenWidth = width;
    int screenHeight = (int)(width / CONSOLE_ASPECT_RATIO);
    int screenMargin = 0;
    if (screenHeight * 2 > height)
    {
        screenWidth = (int)(height / 2 * CONSOLE_ASPECT_RATIO);
        screenHeight = height / 2;
        screenMargin = (width - screenWidth) / 2;
    }
    return {Component({screenMargin, 0, screenWidth, screenHeight}, "TOP_SCREEN"),
        Component({screenMargin, screenHeight, screenWidth, screenHeight}, "BOTTOM_SCREEN")};
}

std::vector<LayoutComponent> BuildDefaultLandscapeScreens(int width, int height)
{
    int topScreenWidth = RoundToInt(width * 0.66f);
    int topScreenHeight = (int)(topScreenWidth / CONSOLE_ASPECT_RATIO);
    if (topScreenHeight > height)
    {
        topScreenWidth = (int)(height * CONSOLE_ASPECT_RATIO);
        topScreenHeight = height;
    }
    int bottomScreenWidth = width - topScreenWidth;
    int bottomScreenHeight = (int)(bottomScreenWidth / CONSOLE_ASPECT_RATIO);
    return {Component({0, 0, topScreenWidth, topScreenHeight}, "TOP_SCREEN"),
        Component({topScreenWidth, 0, bottomScreenWidth, bottomScreenHeight}, "BOTTOM_SCREEN")};
}

LayoutComponent BuildSingleScreen(int width, int height, const char* component)
{
    return Component(CenterScreenIn(width, height), component);
}

UILayout BuildDefaultLayout(int width, int height)
{
    UILayout layout;
    layout.Main.Components = width > height ? BuildDefaultLandscapeScreens(width, height) : BuildDefaultPortraitScreens(width, height);
    return layout;
}

PickedLayout PickVariant(const std::vector<LayoutEntry>& variants, int width, int height,
    const std::function<UILayout(int width, int height)>& defaults)
{
    PickedLayout picked;
    if (width <= 0 || height <= 0)
        return picked;
    bool landscape = width > height;
    int best = -1;
    double bestAspect = 0, bestArea = 0;
    for (int i = 0; i < (int)variants.size(); i++)
    {
        const LayoutVariant& v = variants[i].Variant;
        if (v.SecondDisplay || v.W <= 0 || v.H <= 0 || v.IsLandscape() != landscape)
            continue;
        if (v.W == width && v.H == height)
        {
            best = i;
            picked.Source = PickedLayout::From::Exact;
            break;
        }
        double aspect = std::fabs(std::log(((double)v.W / v.H) / ((double)width / height)));
        double area = std::fabs(std::log((double)v.W * v.H / ((double)width * height)));
        if (best < 0 || aspect < bestAspect - 1e-9 || (std::fabs(aspect - bestAspect) <= 1e-9 && area < bestArea - 1e-9))
        {
            best = i;
            bestAspect = aspect;
            bestArea = area;
            picked.Source = PickedLayout::From::Scaled;
        }
    }
    if (best < 0)
    {
        picked.Layout = defaults(width, height);
        picked.Source = PickedLayout::From::Default;
        return picked;
    }

    picked.Variant = best;
    picked.Layout = variants[best].Layout;
    const LayoutVariant& v = variants[best].Variant;
    if (picked.Source == PickedLayout::From::Scaled)
    {
        float scale = std::min((float)width / v.W, (float)height / v.H);
        float dx = (width - v.W * scale) / 2.f, dy = (height - v.H * scale) / 2.f;
        ScaleComponents(picked.Layout.Main, scale, dx, dy);
        ScaleComponents(picked.Layout.Secondary, scale, dx, dy);
    }
    // only a background: the screens where the default layout has them
    if (!picked.Layout.Main.Components)
        picked.Layout.Main.Components = defaults(width, height).Main.Components;
    return picked;
}

}
