// How widescreen and the WideMelon camera start for a session, ported from the app's WidescreenLaunch.kt (its render width and margin) and
// the camera's part of RomModsConfig.kt: the core renders only as wide as the camera's player zoom needs, up to the widest view, with
// margin rows above and below the top screen so the widest window fills the screen's rect. The app's picked wide layout is left out: the
// desktop picks its layouts its own way
#pragma once

#include <optional>

namespace Frontend
{

// NDS.kt: the even widths of the top screen's 3D view that the core takes, and the most 3D rows the camera renders above and below the
// top screen (its margin); must match the core's range and cap
constexpr int MIN_WIDE_VIEW_WIDTH = 256;
constexpr int MAX_WIDE_VIEW_WIDTH = 768;
constexpr int MAX_WIDE_VIEW_MARGIN = 128;
// RomModsConfig.kt: the camera's widest view when the game's config has none (or one the core doesn't take)
constexpr int DEFAULT_WIDE_CAMERA_MAX_WIDTH = 512;

// RomModsConfig.validWideViewWidth: [wideViewWidth] if the core takes it (even, 256 to 768), otherwise none
std::optional<int> ValidWideViewWidth(std::optional<int> wideViewWidth);
// RomModsConfig.validWideCameraMaxWidth: [wideCameraMaxWidth] if the core takes it, otherwise DEFAULT_WIDE_CAMERA_MAX_WIDTH
int ValidWideCameraMaxWidth(std::optional<int> wideCameraMaxWidth);

// A session's widescreen: [AutomaticViewWidth] is the game's width (0 when the session is never wide), [ViewWidth] the width it's shown at,
// [CameraMaxWidth] the widest the camera zooms out to, the widest the core renders at (0 when the camera is off)
struct WidescreenLaunch
{
    int AutomaticViewWidth = 0;
    int ViewWidth = 0;
    int CameraMaxWidth = 0;
    bool CameraReturnsToCenter = true;
    bool CameraZoomsInOnText = true;

    bool IsWideSession() const { return AutomaticViewWidth > 0; }
    bool IsCameraOn() const { return IsWideSession() && CameraMaxWidth > 0; }

    // The widest the core renders at for a layout shown at [showWidth]: the same without the camera or for a normal width, the camera's
    // widest view otherwise (never narrower than the shown width). More than [showWidth] means the camera has room to zoom out
    int GetMaxRenderWidth(int showWidth) const;
    // The width the core renders at for a layout shown at [showWidth] while the camera's player zoom (WideCamera::PlayerZoom) is [zoom]:
    // that zoom rounded up to the next hotkey step (WideCamera::GetRenderZoom), so a pinch resizes the core only a few times, in whole even
    // columns, between the shown width and GetMaxRenderWidth. At 1x that's the shown width, the same as without the camera
    int GetRenderWidth(int showWidth, float zoom) const;
    // The camera's margin for a layout shown at [showWidth] at player zoom [zoom]: the 3D rows the core renders above and below the top
    // screen, so the widest window the render width allows fills the screen's rect instead of being letterboxed. ceil(96 * (Wr / Wd - 1)),
    // at most MAX_WIDE_VIEW_MARGIN (342 shown with 512 rendered gives 48), and 0 without the camera, for a normal width, at 1x or with no
    // room to zoom out
    int GetRenderMargin(int showWidth, float zoom) const;
};

// WidescreenLaunch.resolve without a picked wide layout: off without a wide renderer or a width the core takes, otherwise the game's width,
// and the camera's widest view when [wideCamera] is on
WidescreenLaunch ResolveWidescreenLaunch(std::optional<int> wideViewWidth, bool wideCamera, std::optional<int> wideCameraMaxWidth,
    bool returnsToCenter, bool zoomsInOnText, bool isWideRenderer);

}
