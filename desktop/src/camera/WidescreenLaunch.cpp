#include "camera/WidescreenLaunch.h"

#include <algorithm>
#include <cmath>

#include "camera/WideCamera.h"

namespace Frontend
{

namespace
{

bool IsWideViewWidth(int width)
{
    return width >= MIN_WIDE_VIEW_WIDTH && width <= MAX_WIDE_VIEW_WIDTH && width % 2 == 0;
}

}

std::optional<int> ValidWideViewWidth(std::optional<int> wideViewWidth)
{
    if (wideViewWidth && IsWideViewWidth(*wideViewWidth))
        return wideViewWidth;
    return std::nullopt;
}

int ValidWideCameraMaxWidth(std::optional<int> wideCameraMaxWidth)
{
    if (wideCameraMaxWidth && IsWideViewWidth(*wideCameraMaxWidth))
        return *wideCameraMaxWidth;
    return DEFAULT_WIDE_CAMERA_MAX_WIDTH;
}

int WidescreenLaunch::GetMaxRenderWidth(int showWidth) const
{
    return IsCameraOn() && showWidth > 0 ? std::max(showWidth, CameraMaxWidth) : showWidth;
}

int WidescreenLaunch::GetRenderWidth(int showWidth, float zoom) const
{
    int maxWidth = GetMaxRenderWidth(showWidth);
    if (maxWidth <= showWidth)
        return showWidth;
    float renderZoom = WideCamera::GetRenderZoom(zoom, maxWidth / (float)showWidth);
    // less a little, so a step that lands on a column exactly isn't rounded up by float error
    int width = (int)std::ceil(showWidth * renderZoom - 0.01f);
    return std::clamp(width + (width & 1), showWidth, maxWidth);
}

int WidescreenLaunch::GetRenderMargin(int showWidth, float zoom) const
{
    int renderWidth = GetRenderWidth(showWidth, zoom);
    if (!IsCameraOn() || showWidth <= DS_SCREEN_WIDTH || renderWidth <= showWidth)
        return 0;
    // in whole numbers, so a ratio that lands on a row exactly isn't rounded up by float error
    int halfHeight = DS_SCREEN_HEIGHT / 2;
    int margin = (halfHeight * (renderWidth - showWidth) + showWidth - 1) / showWidth;
    return std::min(margin, MAX_WIDE_VIEW_MARGIN);
}

WidescreenLaunch ResolveWidescreenLaunch(std::optional<int> wideViewWidth, bool wideCamera, std::optional<int> wideCameraMaxWidth,
    bool returnsToCenter, bool zoomsInOnText, bool isWideRenderer)
{
    std::optional<int> automaticWidth = ValidWideViewWidth(wideViewWidth);
    if (!isWideRenderer || !automaticWidth)
        return {};
    int cameraMaxWidth = wideCamera ? ValidWideCameraMaxWidth(wideCameraMaxWidth) : 0;
    return {*automaticWidth, *automaticWidth, cameraMaxWidth, returnsToCenter, zoomsInOnText};
}

}
