#include "camera/WideCameraController.h"

#include <cmath>
#include <utility>

namespace Frontend
{

namespace
{

// in DS pixels (and that much of a 256 wide window for the zoom)
constexpr float CHANGE_THRESHOLD = 0.05f;

}

WideCameraController::WideCameraController(WideCameraCallbacks callbacks) : Callbacks(std::move(callbacks))
{
}

bool WideCameraController::IsEnabled()
{
    std::lock_guard<std::mutex> guard(Lock);
    return Settings.has_value();
}

void WideCameraController::SetState(const WideCameraState& state)
{
    std::lock_guard<std::mutex> guard(Lock);
    if (state.Session != Session)
    {
        Session = state.Session;
        ApplySettings(std::nullopt);
    }
    StartZoom = state.StartZoom;
    ApplySettings(state.Settings);
}

void WideCameraController::SetSettings(const std::optional<WideCameraSettings>& settings)
{
    std::lock_guard<std::mutex> guard(Lock);
    ApplySettings(settings);
}

void WideCameraController::SetSettings(const std::optional<WideCameraSettings>& settings, float startZoom)
{
    std::lock_guard<std::mutex> guard(Lock);
    StartZoom = startZoom;
    ApplySettings(settings);
}

// under Lock
void WideCameraController::ApplySettings(const std::optional<WideCameraSettings>& newSettings)
{
    if (!newSettings)
    {
        Camera.Reset();
        // presses wait for a frame that can apply them, but not past the camera going off
        ZoomPresses = 0;
    }
    // coming on starts at the game's remembered zoom; a change of the shown width (a fold, say) keeps the zoom that's there
    if (newSettings && !Settings)
        Camera.SetPlayerZoom(StartZoom);
    if (newSettings != Settings)
    {
        ClearHeldInput();
        // a new shown width changes what the zoom renders at, and the caller works it out again, so it's told where the zoom is
        RenderZoom = std::numeric_limits<float>::quiet_NaN();
        ShrinkFrames = 0;
    }
    Settings = newSettings;
    Camera.ReturnsToCenter = newSettings ? newSettings->ReturnsToCenter : true;
    Camera.ZoomsInOnText = newSettings ? newSettings->ZoomsInOnText : true;
}

void WideCameraController::SetScriptRequest(const WideCameraRequest& request)
{
    std::lock_guard<std::mutex> guard(Lock);
    Request = request;
}

void WideCameraController::SetScreensSwapped(bool swapped)
{
    std::lock_guard<std::mutex> guard(Lock);
    AreScreensSwapped = swapped;
}

void WideCameraController::OnSticks(float rightX, float rightY, bool isLeftStickPushed)
{
    std::lock_guard<std::mutex> guard(Lock);
    StickX = rightX;
    StickY = rightY;
    LeftStickMoving = isLeftStickPushed;
}

void WideCameraController::OnKey(WideCameraDirection direction, bool isPressed)
{
    std::lock_guard<std::mutex> guard(Lock);
    uint8_t bit = (uint8_t)(1u << (int)direction);
    if (isPressed)
        HeldDirections |= bit;
    else
        HeldDirections &= (uint8_t)~bit;
}

void WideCameraController::OnZoomPress()
{
    std::lock_guard<std::mutex> guard(Lock);
    ZoomPresses++;
}

void WideCameraController::OnTouchMove(float dragFraction, float pinchScale, float dragFractionY)
{
    std::lock_guard<std::mutex> guard(Lock);
    DragFraction += dragFraction;
    DragFractionY += dragFractionY;
    PinchScale *= pinchScale;
}

void WideCameraController::SetTouching(bool touching)
{
    std::lock_guard<std::mutex> guard(Lock);
    IsTouching = touching;
}

void WideCameraController::ReleaseHeldInput()
{
    std::lock_guard<std::mutex> guard(Lock);
    ClearHeldInput();
}

// under Lock
void WideCameraController::ClearHeldInput()
{
    StickX = 0.f;
    StickY = 0.f;
    LeftStickMoving = false;
    HeldDirections = 0;
    IsTouching = false;
    DragFraction = 0.f;
    DragFractionY = 0.f;
    PinchScale = 1.f;
}

std::optional<WideWindow> WideCameraController::OnFrame(int64_t nowNanos, int frameWidth, int frameMargin, int topWingLines,
    const MelonDSAndroid::FrameGuard& frameGuard)
{
    // MelonInstance keeps the guard within the frame already, as GetScreensDrawing does too
    MelonDSAndroid::FrameGuard guard = CoercedFrameGuard(frameGuard, frameWidth, frameMargin);
    int shownWidth = frameWidth - guard.left - guard.right;
    int shownMargin = frameMargin - (guard.top + guard.bottom) / 2;
    std::optional<float> pickedZoom, neededRenderZoom;
    int pickedSession = 0;
    // only asked while the camera is on, since asking keeps the native read running; outside the lock, as it goes into the core
    int scene = IsEnabled() && Callbacks.GameScene ? Callbacks.GameScene() : GameScene::UNKNOWN;
    std::optional<WideWindow> window;
    {
        std::lock_guard<std::mutex> lock(Lock);
        float deltaSeconds = LastFrameNanos == 0 ? 0.f : (float)(nowNanos - LastFrameNanos) / 1e9f;
        LastFrameNanos = nowNanos;
        Input.StickX = StickX;
        Input.StickY = StickY;
        Input.ZoomPresses = ZoomPresses;
        Input.DragFraction = DragFraction;
        Input.DragFractionY = DragFractionY;
        Input.PinchScale = PinchScale;
        Input.IsTouching = IsTouching;
        Input.IsPlayerMoving = HeldDirections != 0 || LeftStickMoving;
        DragFraction = 0.f;
        DragFractionY = 0.f;
        PinchScale = 1.f;
        if (!Settings)
            ZoomPresses = 0;
        else
        {
            WideCameraSettings currentSettings = *Settings;
            window = Camera.Update(currentSettings.ShowWidth, shownWidth, shownMargin, topWingLines, AreScreensSwapped, Input, Request,
                deltaSeconds, scene, currentSettings.MaxWidth);
            // whatever the camera couldn't apply yet (a frame from before the wide one) waits for the next frame
            ZoomPresses = Input.ZoomPresses;
            pickedZoom = Camera.TakePickedZoom();
            pickedSession = Session;
            neededRenderZoom = UpdateRenderZoom(currentSettings, shownWidth);
        }
    }
    if (pickedZoom && Callbacks.OnZoomPicked)
        Callbacks.OnZoomPicked(pickedSession, *pickedZoom);
    if (neededRenderZoom && Callbacks.OnRenderZoomNeeded)
        Callbacks.OnRenderZoomNeeded(pickedSession, *neededRenderZoom);

    float x = window ? window->Offset() : 0.f;
    float zoom = window ? window->Zoom() : 1.f;
    float y = window ? window->OffsetY() : 0.f;
    // small moves aren't worth telling the scripts about, but landing back in the middle at 1x always is (as is the first frame)
    bool isBackAtRest = x == 0.f && y == 0.f && zoom == 1.f && (SentX != 0.f || SentY != 0.f || SentZoom != 1.f);
    if (isBackAtRest || std::fabs(x - SentX) >= CHANGE_THRESHOLD || std::fabs(y - SentY) >= CHANGE_THRESHOLD ||
        std::fabs(zoom - SentZoom) >= CHANGE_THRESHOLD / 256.f)
    {
        SentX = x;
        SentZoom = zoom;
        SentY = y;
        if (Callbacks.OnCameraChanged)
            Callbacks.OnCameraChanged(x, zoom, y);
    }
    return window;
}

// under Lock, after the camera's update: the render zoom to ask for, or none to keep the last one
std::optional<float> WideCameraController::UpdateRenderZoom(const WideCameraSettings& currentSettings, int frameWidth)
{
    float maxZoom = WideCamera::GetMaxZoom(currentSettings.ShowWidth, frameWidth, currentSettings.MaxWidth);
    float needed = WideCamera::GetRenderZoom(Camera.PlayerZoom(), maxZoom);
    // wider at once, so a zoom-out waits only for the resize
    if (std::isnan(RenderZoom) || needed > RenderZoom)
    {
        ShrinkFrames = 0;
        RenderZoom = needed;
        return needed;
    }
    // narrower once the fingers are off and the zoom-in has eased all the way, for a few frames in a row
    bool isShrinkReady = needed < RenderZoom && !IsTouching && Camera.ShownZoom() <= needed + 1e-3f;
    ShrinkFrames = isShrinkReady ? ShrinkFrames + 1 : 0;
    if (ShrinkFrames < SHRINK_FRAMES)
        return std::nullopt;
    ShrinkFrames = 0;
    RenderZoom = needed;
    return needed;
}

}
