// The WideMelon camera's controller, ported from the app's WideCameraController.kt: collects the player's camera input and the scripts'
// requests from the UI and Lua side and moves the WideCamera once per presented frame on the present thread, so every surface shows the
// same window. No SDL, GL or core here; the integration wires its callbacks to MelonInstance (setWideView, updateWideCamera)
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

#include "camera/WideCamera.h"
#include "renderer/SunGuard.h"

namespace Frontend
{

// The camera's settings for the shown layout: [ShowWidth] is the layout's width (Wd), and the core renders up to [MaxWidth] wide (Wr, the
// frame's width) as the player zooms out. [ZoomsInOnText] is whether a text box pulls in to 1x in a Pokemon game the native field read
// knows. A [MaxWidth] of 0 takes the frame's width as the widest view
struct WideCameraSettings
{
    int ShowWidth = 0;
    bool ReturnsToCenter = true;
    bool ZoomsInOnText = true;
    int MaxWidth = 0;

    bool operator==(const WideCameraSettings&) const = default;
};

// The camera's state: [Session] tells one game session from the next (a ROM switch), [Settings] are none while it's off, and [StartZoom]
// is the zoom the player last picked for the game, which the camera starts at when it comes on. Kept apart from [Settings], so a pick
// doesn't count as a settings change
struct WideCameraState
{
    int Session = 0;
    std::optional<WideCameraSettings> Settings;
    float StartZoom = 1;
};

// the DS inputs that count as the player moving (the app's Input.UP, DOWN, LEFT and RIGHT)
enum class WideCameraDirection { Up, Down, Left, Right };

// The callbacks, all called on the present thread from OnFrame and outside the controller's lock; any may be empty.
//   OnZoomPicked(session, zoom): the zoom a Camera Zoom press picked and the session it was picked in, to remember for the game.
//   OnCameraChanged(x, zoom, y): the window's offset from center, its zoom and its offset down from center for melon.wide.camera(), on the
//     first frame and whenever they change.
//   GameScene(): what a known Pokemon game is doing (GameScene), asked once per presented frame while the camera is on; empty is
//     GameScene::UNKNOWN.
//   OnRenderZoomNeeded(session, zoom): the zoom the core should render for (WideCamera::GetRenderZoom of the player's zoom, so a hotkey
//     step), on the camera's first frame and whenever it changes; WidescreenLaunch::GetRenderWidth and GetRenderMargin turn it into the
//     render width and margin. It follows the player's zoom, not what's shown, so a pull-in never resizes the core. A wider one is asked
//     for at once. A narrower one waits until no finger is on the screen, the shown zoom has eased all the way in to it and it has held for
//     SHRINK_FRAMES frames, so a pinch that goes out and back in resizes at most once per step out
struct WideCameraCallbacks
{
    std::function<void(int session, float zoom)> OnZoomPicked;
    std::function<void(float x, float zoom, float y)> OnCameraChanged;
    std::function<int()> GameScene;
    std::function<void(int session, float zoom)> OnRenderZoomNeeded;
};

class WideCameraController
{
public:
    // frames a narrower render zoom has to be ready for before it's asked for
    static constexpr int SHRINK_FRAMES = 6;

    explicit WideCameraController(WideCameraCallbacks callbacks = {});

    // whether the camera is on for the shown layout, so the input that drives it is worth sending
    bool IsEnabled();

    // The camera's state (WideCameraState). A new session starts over from nothing, even if the off state between the sessions never came
    // through; within one, settings of none switch it off (no widescreen, the camera switched off, no zoom-out past the shown width, or a
    // layout variant that isn't wide), and a settings change drops the held input, whose releases may never come
    void SetState(const WideCameraState& state);
    // SetState within the current session, keeping the start zoom or with a new one
    void SetSettings(const std::optional<WideCameraSettings>& settings);
    void SetSettings(const std::optional<WideCameraSettings>& settings, float startZoom);

    void SetScriptRequest(const WideCameraRequest& request);
    void SetScreensSwapped(bool swapped);
    // the right stick's X and Y (-1 to 1, right and down are positive) and whether the left stick is pushed
    void OnSticks(float rightX, float rightY, bool isLeftStickPushed);
    // a D-pad direction went down or up, which counts as the player moving
    void OnKey(WideCameraDirection direction, bool isPressed);
    // a Camera Zoom press: the next zoom step, or 1x after the widest
    void OnZoomPress();
    // a touch on the wide screen: [dragFraction] of its drawn picture's width moved right, [dragFractionY] of its height down, fingers
    // spread by [pinchScale]
    void OnTouchMove(float dragFraction, float pinchScale, float dragFractionY = 0);
    void SetTouching(bool touching);
    // forgets the sticks, the D-pad and a touch, for when their releases won't come (a menu opened over the game, the window lost focus)
    void ReleaseHeldInput();

    // Moves the camera on for a presented frame [frameWidth] wide with [frameMargin] rows above and below the top screen, and returns its
    // window, or none to draw the frame as without a camera. Present thread.
    // [frameGuard] is the sun shadows' guard band in both (FrameView::TopGuard), which is never shown: the camera works on the shown frame
    // inside it, [frameWidth] less the left and right guard with [frameMargin] less half the top and bottom one, so zoomed all the way out
    // it shows all of that and none of the guard, and the window it returns is of that shown frame (GetScreensDrawing moves it in by the
    // guard)
    std::optional<WideWindow> OnFrame(int64_t nowNanos, int frameWidth, int frameMargin, int topWingLines,
        const MelonDSAndroid::FrameGuard& frameGuard = {});

private:
    void ApplySettings(const std::optional<WideCameraSettings>& newSettings);
    void ClearHeldInput();
    std::optional<float> UpdateRenderZoom(const WideCameraSettings& currentSettings, int frameWidth);

    const WideCameraCallbacks Callbacks;

    // guards everything below down to Input; the UI, Lua and present threads all come through it
    std::mutex Lock;
    WideCamera Camera;
    int Session = 0;
    std::optional<WideCameraSettings> Settings;
    float StartZoom = 1;
    WideCameraRequest Request;
    bool AreScreensSwapped = false;
    float StickX = 0;
    float StickY = 0;
    bool LeftStickMoving = false;
    // one bit per WideCameraDirection held
    uint8_t HeldDirections = 0;
    int ZoomPresses = 0;
    float DragFraction = 0;
    float DragFractionY = 0;
    float PinchScale = 1;
    bool IsTouching = false;
    int64_t LastFrameNanos = 0;
    // the render zoom last asked for, NaN until the camera's first frame in this session and settings, so it's always asked for once
    float RenderZoom = std::numeric_limits<float>::quiet_NaN();
    // frames in a row a narrower render zoom has been ready to ask for
    int ShrinkFrames = 0;
    // reused for every frame
    WideCameraInput Input;

    // present thread only. NaN until the first frame, so a new controller always tells the scripts where the camera is instead of leaving
    // them the old one's
    float SentX = std::numeric_limits<float>::quiet_NaN();
    float SentZoom = std::numeric_limits<float>::quiet_NaN();
    float SentY = std::numeric_limits<float>::quiet_NaN();
};

}
