#include "camera/WideCamera.h"

#include <algorithm>
#include <cmath>

namespace Frontend
{

namespace
{

// how close the shown zoom (in zoom factor) and pan (in DS pixels) get before they land on their targets
constexpr float ZOOM_SNAP = 0.001f;
constexpr float PAN_SNAP = 0.01f;
constexpr float NOT_A_TARGET = std::numeric_limits<float>::quiet_NaN();

// Kotlin's Float math, which goes through double: kotlin.math.exp and pow on floats are Math.exp and Math.pow of the widened values
float ExpF(float x)
{
    return (float)std::exp((double)x);
}

float PowF(float base, float exponent)
{
    return (float)std::pow((double)base, (double)exponent);
}

// Kotlin's coerceIn, coerceAtMost and coerceAtLeast (a NaN goes through), without coerceIn's throw for an empty range
float CoerceIn(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

float CoerceAtMost(float value, float maximum)
{
    return value > maximum ? maximum : value;
}

float CoerceAtLeast(float value, float minimum)
{
    return value < minimum ? minimum : value;
}

// the stick past the dead zone, -1 to 1
float GetStickStrength(float stick)
{
    if (std::fabs(stick) <= WideCamera::STICK_DEAD_ZONE)
        return 0.f;
    float strength = (std::fabs(stick) - WideCamera::STICK_DEAD_ZONE) / (1.f - WideCamera::STICK_DEAD_ZONE);
    float sign = stick > 0.f ? 1.f : (stick < 0.f ? -1.f : stick);
    return sign * CoerceAtMost(strength, 1.f);
}

// DS pixels the stick moves the pan by over dt, 0 inside the dead zone
float GetStickPan(float stick, float dt)
{
    return GetStickStrength(stick) * WideCamera::PAN_SPEED * dt;
}

// PokeDaisy's spring: [current] pulled toward [target] with time constant [seconds] over dt, landing on it once within 0.05 DS pixels
float Spring(float current, float target, float dt, float seconds)
{
    float next = current + (target - current) * (1.f - ExpF(-dt / seconds));
    return std::fabs(next - target) < 0.05f ? target : next;
}

float Smoothstep(float p)
{
    return p * p * (3.f - 2.f * p);
}

// the pan drifted back toward the middle by [factor] (1 for no drift), landing on the middle exactly, where the picture is the same as
// without a camera
float ReturnToCenter(float pan, float factor)
{
    if (factor == 1.f)
        return pan;
    float next = pan * factor;
    return std::fabs(next) < 0.01f ? 0.f : next;
}

// [current] eased toward [target] with time constant [seconds] over [dt], landing on it once within [snap]
float Approach(float current, float target, float seconds, float dt, float snap)
{
    float next = current + (target - current) * (1.f - ExpF(-dt / seconds));
    return std::fabs(target - next) < snap ? target : next;
}

}

std::optional<WideWindow> WideCamera::Update(const WideCameraFrame& frame, WideCameraInput& input, const WideCameraRequest& request,
    float deltaSeconds)
{
    return Update(frame.ShowWidth, frame.FrameWidth, frame.FrameMargin, frame.TopWingLines, frame.AreScreensSwapped, input, request,
        deltaSeconds, frame.GameScene, frame.MaxWidth);
}

std::optional<WideWindow> WideCamera::Update(int showWidth, int frameWidth, int frameMargin, int topWingLines, bool areScreensSwapped,
    WideCameraInput& input, const WideCameraRequest& request, float deltaSeconds, int gameScene, int maxWidth)
{
    float maxZoom = GetMaxZoom(showWidth, frameWidth, maxWidth);
    if (maxWidth > 0 && showWidth > 0)
    {
        // A known range clamps the player's zoom even on frames that go no further (a hold, a frame before the wide one), so the render
        // zoom asked for from it (PlayerZoom) is always one the range allows. Without one, the range is the frame's, which may not be here
        // yet
        TargetZoom = CoerceIn(TargetZoom, 1.f, std::max(1.f, maxZoom));
    }
    if (showWidth <= DS_SCREEN_WIDTH || frameWidth < showWidth || maxZoom <= 1.f)
    {
        // a frame from before the wide one (at launch, or a width change on its way), or no room to zoom out: back to the middle, keeping
        // the player's zoom
        Zoom = 1.f;
        ResetPan();
        FreeSeconds = 0.f;
        TweenTarget = NOT_A_TARGET;
        return std::nullopt;
    }

    WideWindow window = Move(showWidth, frameWidth, frameMargin, topWingLines, areScreensSwapped, input, request, deltaSeconds, gameScene,
        maxZoom);
    // at 1x the core renders the show width, which is drawn as without a camera
    if (frameWidth > showWidth)
        return window;
    return std::nullopt;
}

// Update once the frame is at least the show width and there's room to zoom out
WideWindow WideCamera::Move(int showWidth, int frameWidth, int frameMargin, int topWingLines, bool areScreensSwapped,
    WideCameraInput& input, const WideCameraRequest& request, float deltaSeconds, int gameScene, float maxZoom)
{
    int frameHeight = DS_SCREEN_HEIGHT + 2 * std::max(frameMargin, 0);
    float dt = CoerceIn(deltaSeconds, 0.f, MAX_DELTA_SECONDS);
    // the widest this frame can show, less than maxZoom while a wider frame is on its way
    float frameZoom = frameWidth / (float)showWidth;
    // The hotkey's presses count even while 1x is held (in a menu, say), so the picked step shows once the hold ends; while a script sets
    // the zoom they're dropped
    if (input.ZoomPresses > 0)
    {
        if (!request.Zoom)
        {
            for (int i = 0; i < input.ZoomPresses; i++)
                TargetZoom = CycleZoom(CoerceIn(TargetZoom, 1.f, maxZoom), maxZoom);
            PickedZoom = TargetZoom;
        }
        input.ZoomPresses = 0;
    }
    // Dual-screen 3D games aren't held: the core counts the wing hold's held lines, so their top screen has wings on every frame. Once a
    // game drops its 3D, the holds end within 2 frames. In a known game the scene takes the wing lines' place (UpdateForScene)
    if (gameScene != GameScene::UNKNOWN)
        LastKnownScene = gameScene;
    int scene = LastKnownScene;
    bool isKnownGame = scene != GameScene::UNKNOWN;
    bool isHeldNative = request.HoldNative || areScreensSwapped || (!isKnownGame && topWingLines < MIN_WING_LINES);
    if (isHeldNative)
    {
        // straight to 1x and the middle, since the wings are black or don't belong to this screen; the player's zoom comes back after.
        // In a known game it comes back like after any close-up, CLOSE_RETURN_SECONDS after the hold ends, eased
        Zoom = 1.f;
        ResetPan();
        FreeSeconds = 0.f;
        TweenTarget = NOT_A_TARGET;
        IsCloseWanted = true;
        CloseFreeSeconds = 0.f;
        return WideWindow::Centered(showWidth, frameWidth, frameHeight);
    }
    if (isKnownGame)
        return UpdateForScene(showWidth, frameWidth, frameHeight, scene, input, request, dt, maxZoom, frameZoom);
    TweenTarget = NOT_A_TARGET;
    FreeSeconds += dt;
    if (FreeSeconds < RESUME_SECONDS)
        return WideWindow::Centered(showWidth, frameWidth, frameHeight);

    // zoom: the script's, or the player's pinches (the hotkey's steps are in above)
    if (request.Zoom)
        TargetZoom = *request.Zoom;
    else if (input.PinchScale > 0.f && input.PinchScale != 1.f)
        TargetZoom /= input.PinchScale;
    TargetZoom = CoerceIn(TargetZoom, 1.f, maxZoom);
    // a pinch follows the fingers. Either way no wider than the frame, so a zoom-out ahead of the frame eases on from it once it's wider
    Zoom = input.IsTouching && !request.Zoom ? TargetZoom : Approach(Zoom, TargetZoom, ZOOM_SECONDS, dt, ZOOM_SNAP);
    Zoom = CoerceAtMost(Zoom, frameZoom);

    // pan, each axis on its own: the script's, or the stick, a drag and the drift back to the middle
    bool isStickIdle = std::fabs(input.StickX) <= STICK_DEAD_ZONE && std::fabs(input.StickY) <= STICK_DEAD_ZONE;
    bool isReturning = ReturnsToCenter && isStickIdle && !input.IsTouching && input.IsPlayerMoving;
    float returnFactor = isReturning ? ExpF(-dt / RETURN_SECONDS) : 1.f;
    if (request.Pan)
        TargetPan = *request.Pan;
    else
    {
        TargetPan += GetStickPan(input.StickX, dt);
        // the picture follows the finger, so the window moves the other way
        TargetPan -= input.DragFraction * showWidth * Zoom;
        TargetPan = ReturnToCenter(TargetPan, returnFactor);
    }
    if (request.PanY)
        TargetPanY = *request.PanY;
    else
    {
        TargetPanY += GetStickPan(input.StickY, dt);
        // a drag is a fraction of the picture's height, which shows the window's rows
        TargetPanY -= input.DragFractionY * GetWindowHeight(frameHeight, Zoom);
        TargetPanY = ReturnToCenter(TargetPanY, returnFactor);
    }
    TargetPan = CoerceIn(TargetPan, -GetMaxPan(showWidth, frameWidth, TargetZoom), GetMaxPan(showWidth, frameWidth, TargetZoom));
    TargetPanY = CoerceIn(TargetPanY, -GetMaxPanY(frameHeight, TargetZoom), GetMaxPanY(frameHeight, TargetZoom));
    Pan = input.IsTouching && !request.Pan ? TargetPan : Approach(Pan, TargetPan, PAN_SECONDS, dt, PAN_SNAP);
    PanY = input.IsTouching && !request.PanY ? TargetPanY : Approach(PanY, TargetPanY, PAN_SECONDS, dt, PAN_SNAP);
    float maxPan = GetMaxPan(showWidth, frameWidth, Zoom);
    Pan = CoerceIn(Pan, -maxPan, maxPan);
    float maxPanY = GetMaxPanY(frameHeight, Zoom);
    PanY = CoerceIn(PanY, -maxPanY, maxPanY);

    float width = CoerceAtMost(showWidth * Zoom, (float)frameWidth);
    float x = CoerceIn((frameWidth - width) / 2.f + Pan, 0.f, frameWidth - width);
    float height = GetWindowHeight(frameHeight, Zoom);
    float y = CoerceIn((frameHeight - height) / 2.f + PanY, 0.f, frameHeight - height);
    return {showWidth, frameWidth, x, width, frameHeight, y, height};
}

// Update for a known Pokemon game, after the holds every game has (a script's hold_native, swapped screens): PokeDaisy's
// WideCamera.frame, with our stick speed, dead zone, touch drag, pinch and script requests kept.
// - The scene says when to pull in to 1x: a battle, a menu screen and anything off the field always do, a text box if ZoomsInOnText, the
//   plain field never, and a fade decides nothing (the zoom holds through a door). The player's zoom comes back CLOSE_RETURN_SECONDS after
//   the close-up was last wanted. Ours only: a text box straight after a fade, with no field between, decides nothing for its first
//   AFTER_FADE_TEXT_SECONDS (HG/SS's building exits; see IsAfterFade).
// - The zoom eases with smoothstep in log space (UpdateSceneZoom).
// - Pulled in, the look-around springs back to the middle (LOOK_SECONDS). Walking with ReturnsToCenter on, it springs back to the middle
//   (WALK_RETURN_SECONDS), or with the right stick held follows the stick, keeping the middle PLAYER_KEEP DS pixels inside the window
//   (LOOK_SECONDS). Standing, the stick and a drag pan as in other games, and the view stays where it's left.
WideWindow WideCamera::UpdateForScene(int showWidth, int frameWidth, int frameHeight, int gameScene, const WideCameraInput& input,
    const WideCameraRequest& request, float dt, float maxZoom, float frameZoom)
{
    if (gameScene == GameScene::FADE)
    {
        IsAfterFade = true;
        AfterFadeTextSeconds = 0.f;
    }
    else if (gameScene == GameScene::TEXT)
    {
        if (IsAfterFade)
            AfterFadeTextSeconds += dt;
    }
    else
        IsAfterFade = false;
    bool isFadeText = gameScene == GameScene::TEXT && IsAfterFade && AfterFadeTextSeconds < AFTER_FADE_TEXT_SECONDS;
    bool wantsClose;
    if (gameScene == GameScene::FIELD)
        wantsClose = false;
    else if (gameScene == GameScene::FADE || isFadeText)
        // decides nothing: still close if it was, and the time since the close-up keeps running if it wasn't
        wantsClose = IsCloseWanted;
    else if (gameScene == GameScene::TEXT)
        wantsClose = ZoomsInOnText;
    else
        wantsClose = true;
    if (wantsClose)
    {
        IsCloseWanted = true;
        CloseFreeSeconds = 0.f;
    }
    else
    {
        IsCloseWanted = false;
        CloseFreeSeconds = CoerceAtMost(CloseFreeSeconds + dt, CLOSE_RETURN_SECONDS);
    }
    bool isClose = IsCloseWanted || CloseFreeSeconds < CLOSE_RETURN_SECONDS;
    // kept meaningful for the wing-line rules: held while pulled in, free to leave 1x otherwise
    FreeSeconds = isClose ? 0.f : RESUME_SECONDS;

    UpdateSceneZoom(isClose, input, request, dt, maxZoom, frameZoom);

    // pan, each axis on its own
    float width = CoerceAtMost(showWidth * Zoom, (float)frameWidth);
    float height = GetWindowHeight(frameHeight, Zoom);
    float stickX = GetStickStrength(input.StickX);
    float stickY = GetStickStrength(input.StickY);
    bool isWalking = ReturnsToCenter && input.IsPlayerMoving && !input.IsTouching;
    // where a spring pulls the view, and its time constant; 0 for none (standing)
    float springX = 0.f, springY = 0.f, springSeconds = 0.f;
    if (isClose)
        springSeconds = LOOK_SECONDS;
    else if (isWalking && (stickX != 0.f || stickY != 0.f))
    {
        // follow the stick, keeping the player (the frame's middle) inside the window
        springX = stickX * std::max(0.f, width / 2.f - PLAYER_KEEP);
        springY = stickY * std::max(0.f, height / 2.f - PLAYER_KEEP);
        springSeconds = LOOK_SECONDS;
    }
    else if (isWalking)
        springSeconds = WALK_RETURN_SECONDS;
    float maxPan = GetMaxPan(showWidth, frameWidth, Zoom);
    float maxPanY = GetMaxPanY(frameHeight, Zoom);
    // pulled in, the middle wins over a script's pan, as with hold_native; walking, a script's pan wins over the springs
    if (springSeconds > 0.f && (isClose || !request.Pan))
    {
        Pan = CoerceIn(Spring(Pan, springX, dt, springSeconds), -maxPan, maxPan);
        TargetPan = Pan;
    }
    else
    {
        TargetPan = request.Pan ? *request.Pan : TargetPan + GetStickPan(input.StickX, dt) - input.DragFraction * showWidth * Zoom;
        TargetPan = CoerceIn(TargetPan, -maxPan, maxPan);
        Pan = input.IsTouching && !request.Pan ? TargetPan : Approach(Pan, TargetPan, PAN_SECONDS, dt, PAN_SNAP);
    }
    if (springSeconds > 0.f && (isClose || !request.PanY))
    {
        PanY = CoerceIn(Spring(PanY, springY, dt, springSeconds), -maxPanY, maxPanY);
        TargetPanY = PanY;
    }
    else
    {
        TargetPanY = request.PanY ? *request.PanY : TargetPanY + GetStickPan(input.StickY, dt) - input.DragFractionY * height;
        TargetPanY = CoerceIn(TargetPanY, -maxPanY, maxPanY);
        PanY = input.IsTouching && !request.PanY ? TargetPanY : Approach(PanY, TargetPanY, PAN_SECONDS, dt, PAN_SNAP);
    }
    Pan = CoerceIn(Pan, -maxPan, maxPan);
    PanY = CoerceIn(PanY, -maxPanY, maxPanY);

    float x = CoerceIn((frameWidth - width) / 2.f + Pan, 0.f, frameWidth - width);
    float y = CoerceIn((frameHeight - height) / 2.f + PanY, 0.f, frameHeight - height);
    return {showWidth, frameWidth, x, width, frameHeight, y, height};
}

// UpdateForScene's zoom: the script's, or the player's pinches (the hotkey's steps are in Move), left alone while pulled in. It eases with
// smoothstep in log space: PULL_IN_SECONDS in (a battle too, as on PokeDaisy's DS path), ZOOM_OUT_SECONDS out. A pinch still follows the
// fingers, and a script's zoom eases as in other games
void WideCamera::UpdateSceneZoom(bool isClose, const WideCameraInput& input, const WideCameraRequest& request, float dt, float maxZoom,
    float frameZoom)
{
    if (!isClose)
    {
        if (request.Zoom)
            TargetZoom = *request.Zoom;
        else if (input.PinchScale > 0.f && input.PinchScale != 1.f)
            TargetZoom /= input.PinchScale;
    }
    TargetZoom = CoerceIn(TargetZoom, 1.f, maxZoom);
    float shownTarget = isClose ? 1.f : TargetZoom;
    if (!isClose && input.IsTouching && !request.Zoom)
    {
        // a pinch follows the fingers
        Zoom = TargetZoom;
        TweenFrom = Zoom;
        TweenTarget = Zoom;
        TweenSeconds = 0.f;
    }
    else if (!isClose && request.Zoom)
    {
        // a script's zoom eases as in other games, since a script may move it every frame, which would restart a timed move each time
        Zoom = Approach(Zoom, TargetZoom, ZOOM_SECONDS, dt, ZOOM_SNAP);
        TweenTarget = NOT_A_TARGET;
    }
    else
    {
        if (shownTarget != TweenTarget)
        {
            // a new target starts a new move from wherever the zoom is: in at the pace of what's coming, back out slower
            TweenFrom = Zoom;
            TweenTarget = shownTarget;
            TweenElapsed = 0.f;
            TweenSeconds = shownTarget < TweenFrom ? PULL_IN_SECONDS : ZOOM_OUT_SECONDS;
        }
        else
            TweenElapsed += dt;
        float progress = TweenSeconds <= 0.f ? 1.f : CoerceIn(TweenElapsed / TweenSeconds, 0.f, 1.f);
        Zoom = TweenFrom * PowF(TweenTarget / TweenFrom, Smoothstep(progress));
        if (std::fabs(Zoom - shownTarget) < ZOOM_SNAP)
            Zoom = shownTarget;
    }
    if (Zoom > frameZoom)
    {
        // a wider frame is on its way: all of this one, and the eased move starts over from it, so it doesn't jump once the frame comes
        Zoom = frameZoom;
        TweenFrom = frameZoom;
        TweenElapsed = 0.f;
    }
}

void WideCamera::SetPlayerZoom(float zoom)
{
    TargetZoom = CoerceAtLeast(zoom, 1.f);
}

std::optional<float> WideCamera::TakePickedZoom()
{
    std::optional<float> picked = PickedZoom;
    PickedZoom.reset();
    return picked;
}

void WideCamera::ResetPan()
{
    Pan = 0.f;
    TargetPan = 0.f;
    PanY = 0.f;
    TargetPanY = 0.f;
}

void WideCamera::Reset()
{
    PickedZoom.reset();
    TargetZoom = 1.f;
    Zoom = 1.f;
    ResetPan();
    FreeSeconds = 0.f;
    IsCloseWanted = true;
    CloseFreeSeconds = 0.f;
    IsAfterFade = false;
    AfterFadeTextSeconds = 0.f;
    TweenTarget = NOT_A_TARGET;
    LastKnownScene = GameScene::UNKNOWN;
}

float WideCamera::CycleZoom(float zoom, float maxZoom)
{
    if (zoom >= maxZoom - 1e-3f)
        return 1.f;
    // the next multiple of ZOOM_STEP above it, at most the widest view
    return CoerceIn((std::floor(zoom / ZOOM_STEP + 1e-3f) + 1.f) * ZOOM_STEP, 1.f, maxZoom);
}

float WideCamera::GetMaxZoom(int showWidth, int frameWidth, int maxWidth)
{
    if (showWidth <= 0)
        return 1.f;
    return (maxWidth > 0 ? maxWidth : frameWidth) / (float)showWidth;
}

float WideCamera::GetRenderZoom(float zoom, float maxZoom)
{
    // less a hundredth of a step, so a zoom a float error past a step isn't rounded up to the next one
    float step = std::ceil(zoom / ZOOM_STEP - 0.01f) * ZOOM_STEP;
    return CoerceIn(step, 1.f, std::max(1.f, maxZoom));
}

float WideCamera::GetMaxPan(int showWidth, int frameWidth, float zoom)
{
    return CoerceAtLeast((frameWidth - showWidth * zoom) / 2.f, 0.f);
}

float WideCamera::GetMaxPanY(int frameHeight, float zoom)
{
    return CoerceAtLeast((frameHeight - DS_SCREEN_HEIGHT * zoom) / 2.f, 0.f);
}

float WideCamera::GetWindowHeight(int frameHeight, float zoom)
{
    return CoerceAtMost(DS_SCREEN_HEIGHT * zoom, (float)frameHeight);
}

}
