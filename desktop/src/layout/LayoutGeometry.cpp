#include "layout/LayoutGeometry.h"

#include <algorithm>
#include <cmath>

#include "camera/WideCamera.h"

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

// Kotlin's coerceIn (a NaN goes through), without its throw for an empty range
float CoerceIn(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

}

const WideWindow* UsableWideWindow(const WideWindow* window, int shownFrameWidth)
{
    return window && window->FrameWidth == shownFrameWidth && window->ShowWidth > DS_SCREEN_WIDTH && window->ShowWidth < shownFrameWidth
        ? window : nullptr;
}

ScreenDrawing GetWindowedScreenDrawing(const IntRect& rect, const WideWindow& window)
{
    float frameWidth = (float)window.FrameWidth;
    float windowWidth = CoerceIn(window.Width, 1.f, frameWidth);
    float windowX = CoerceIn(window.X, 0.f, frameWidth - windowWidth);
    float frameHeight = (float)std::max(window.FrameHeight, DS_SCREEN_HEIGHT);
    float windowHeight = CoerceIn(window.Height, 1.f, frameHeight);
    float windowY = CoerceIn(window.Y, 0.f, frameHeight - windowHeight);
    // at the DS's pixel shape the rect's height shows 192 * zoom rows, so fewer rows than that leave bars above and below
    float pictureHeight = rect.H * std::min(1.f, (windowHeight / DS_SCREEN_HEIGHT) * (window.ShowWidth / windowWidth));
    IntRect drawRect = BuildCenteredRect(rect.X + rect.W / 2.f, rect.Y + (rect.H - pictureHeight) / 2.f, (float)rect.W, pictureHeight);

    float pixelsPerColumn = drawRect.W / windowWidth;
    float nativeLeft = drawRect.X + ((frameWidth - DS_SCREEN_WIDTH) / 2.f - windowX) * pixelsPerColumn;
    float nativeRight = nativeLeft + DS_SCREEN_WIDTH * pixelsPerColumn;
    // the native rows as fractions of the window's, so a window of just those (no margin, or 1x) lands on the picture's edges exactly
    float margin = (frameHeight - DS_SCREEN_HEIGHT) / 2.f;
    float nativeTop = drawRect.Y + drawRect.H * ((margin - windowY) / windowHeight);
    float nativeBottom = drawRect.Y + drawRect.H * ((margin + DS_SCREEN_HEIGHT - windowY) / windowHeight);

    ScreenDrawing d;
    d.DrawRect = drawRect;
    d.NativeRect = BuildCenteredRect((nativeLeft + nativeRight) / 2.f, nativeTop, nativeRight - nativeLeft, nativeBottom - nativeTop);
    d.LeftU = windowX / frameWidth;
    d.RightU = (windowX + windowWidth) / frameWidth;
    OverlayDrawing& o = d.Overlay;
    o.Left = CoerceIn(nativeLeft, (float)drawRect.X, (float)drawRect.Right());
    o.Right = CoerceIn(nativeRight, o.Left, (float)drawRect.Right());
    o.Top = CoerceIn(nativeTop, (float)drawRect.Y, (float)drawRect.Bottom());
    o.Bottom = CoerceIn(nativeBottom, o.Top, (float)drawRect.Bottom());
    o.LeftU = (o.Left - nativeLeft) / (nativeRight - nativeLeft);
    o.RightU = (o.Right - nativeLeft) / (nativeRight - nativeLeft);
    o.TopV = (o.Top - nativeTop) / (nativeBottom - nativeTop);
    o.BottomV = (o.Bottom - nativeTop) / (nativeBottom - nativeTop);
    d.NativeRectAt1x = GetNativeScreenRect(rect, window.ShowWidth);
    d.TopRow = windowY;
    d.BottomRow = windowY + windowHeight;
    return d;
}

namespace
{

// getScreensDrawing for a frame without a guard band: the shown one
ScreensDrawing GetShownScreensDrawing(const IntRect* topScreenRect, const IntRect* bottomScreenRect, int frameWidth, bool isWideLayout,
    bool areScreensSwapped, float surfaceWidth, const WideWindow* wideWindow, int frameMargin)
{
    const IntRect* wideScreenRect = areScreensSwapped ? bottomScreenRect : topScreenRect;
    const IntRect* narrowScreenRect = areScreensSwapped ? topScreenRect : bottomScreenRect;
    // a window for another frame width (one presented before a width change) is left out
    const WideWindow* window = UsableWideWindow(wideWindow, frameWidth);
    // the DS top screen's native rows, which a screen shows without the camera's window
    float topScreenRows = (float)std::max(frameMargin, 0);

    std::optional<ScreenDrawing> wideScreen, narrowScreen;
    if (wideScreenRect && window)
    {
        IntRect rect = GetWideScreenDrawRect(*wideScreenRect, narrowScreenRect, window->ShowWidth, isWideLayout, surfaceWidth);
        // the bottom screen has no margin, so swapped (the camera keeps 1x then) it shows all its rows
        WideWindow rowWindow = *window;
        if (areScreensSwapped && window->FrameHeight != DS_SCREEN_HEIGHT)
        {
            rowWindow.FrameHeight = DS_SCREEN_HEIGHT;
            rowWindow.Y = 0.f;
            rowWindow.Height = (float)DS_SCREEN_HEIGHT;
        }
        wideScreen = GetWindowedScreenDrawing(rect, rowWindow);
    }
    else if (wideScreenRect)
    {
        ScreenDrawing d;
        d.DrawRect = GetWideScreenDrawRect(*wideScreenRect, narrowScreenRect, frameWidth, isWideLayout, surfaceWidth);
        d.NativeRect = GetNativeScreenRect(d.DrawRect, frameWidth);
        d.Overlay = OverlayDrawing::Of(d.NativeRect);
        d.NativeRectAt1x = d.NativeRect;
        d.TopRow = areScreensSwapped ? 0.f : topScreenRows;
        d.BottomRow = d.TopRow + DS_SCREEN_HEIGHT;
        wideScreen = d;
    }
    if (narrowScreenRect)
    {
        ScreenDrawing d;
        d.DrawRect = isWideLayout && areScreensSwapped ? GetLetterboxedRect(*narrowScreenRect, CONSOLE_ASPECT_RATIO) : *narrowScreenRect;
        d.NativeRect = d.DrawRect;
        d.Overlay = OverlayDrawing::Of(d.NativeRect);
        d.NativeRectAt1x = d.NativeRect;
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
    bool areScreensSwapped, float surfaceWidth, int frameMargin, const MelonDSAndroid::FrameGuard& frameGuard, const WideWindow* wideWindow)
{
    // MelonInstance keeps the guard within the frame already; kept there here too, so a bad one can't turn the shown frame inside out
    MelonDSAndroid::FrameGuard guard = CoercedFrameGuard(frameGuard, frameWidth, frameMargin);
    int shownWidth = frameWidth - guard.left - guard.right;
    ScreensDrawing shown = GetShownScreensDrawing(topScreenRect, bottomScreenRect, shownWidth, isWideLayout, areScreensSwapped, surfaceWidth,
        wideWindow, frameMargin - (guard.top + guard.bottom) / 2);
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
