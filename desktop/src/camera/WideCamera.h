// The WideMelon camera's pure logic, ported from the app's WideCamera.kt: which window of the wide top screen frame (the core renders it at a
// render width Wr plus margin rows) the wide screen shows. The game's own camera never moves; zooming out widens the window up to the render
// width (and heightens it into the margin rows) and panning slides it, clamped to the frame. Pure, not thread-safe (WideCameraController
// locks around it)
#pragma once

#include <limits>
#include <optional>

#include "layout/LayoutGeometry.h"

namespace Frontend
{

// The part of a [FrameWidth] x [FrameHeight] frame (the render width Wr, and the render height Hr = 192 + 2 margin rows) the wide screen
// shows: columns [X, X + Width) and rows [Y, Y + Height) of the top screen's half, in DS pixels. [ShowWidth] is the layout's width (Wd),
// the one the screen's rect is shaped for; a window wider than its height covers (because the margin can't cover it) is shown letterboxed
// top and bottom
struct WideWindow
{
    int ShowWidth = 0;
    int FrameWidth = 0;
    float X = 0;
    float Width = 0;
    int FrameHeight = DS_SCREEN_HEIGHT;
    float Y = 0;
    float Height = (float)DS_SCREEN_HEIGHT;

    // the window's offset from the frame's center in DS pixels, what melon.wide.camera() calls x
    float Offset() const { return X + Width / 2.f - FrameWidth / 2.f; }
    // the same down the frame (down is positive), melon.wide.camera()'s y
    float OffsetY() const { return Y + Height / 2.f - FrameHeight / 2.f; }
    // 1 at the show width, more when zoomed out
    float Zoom() const { return Width / ShowWidth; }
    // the frame's rows above (and below) the top screen's 192, so its DS line 0 is row Margin()
    int Margin() const { return (FrameHeight - DS_SCREEN_HEIGHT) / 2; }

    bool operator==(const WideWindow&) const = default;

    // the middle [showWidth] columns and 192 rows, which looks exactly like a frame rendered at [showWidth] with no margin
    static WideWindow Centered(int showWidth, int frameWidth, int frameHeight = DS_SCREEN_HEIGHT)
    {
        return {showWidth, frameWidth, (frameWidth - showWidth) / 2.f, (float)showWidth, frameHeight,
            (frameHeight - DS_SCREEN_HEIGHT) / 2.f, (float)DS_SCREEN_HEIGHT};
    }
};

// What the running scripts ask of the camera with melon.wide (LuaWideCameraRequests picks it): a zoom and a pan on each axis that replace
// the player's, none to leave them to the player, and whether to stay at 1x in the middle
struct WideCameraRequest
{
    std::optional<float> Zoom;
    std::optional<float> Pan;
    bool HoldNative = false;
    std::optional<float> PanY;

    bool IsOff() const { return !Zoom && !Pan && !PanY && !HoldNative; }
    bool operator==(const WideCameraRequest&) const = default;
};

// What a Pokemon game the native field read knows is doing (cpp/gamestate/GameState.h), as MelonInstance's game scene gives it. The numbers
// are PokeDaisy's WideCamera.SCENE_*; BATTLE is its SCENE_BATTLE_START's, which ours sends for the whole battle
namespace GameScene
{
// not a game the field read knows (or not read yet): the camera keeps its generic rules
constexpr int UNKNOWN = -1;
// the overworld with nothing over it
constexpr int FIELD = 0;
// the overworld with a text box up: a conversation, a sign, a yes/no prompt
constexpr int TEXT = 1;
constexpr int BATTLE = 3;
// off the field: the title, the intro, any screen that isn't the overworld
constexpr int OFF = 4;
// the top screen faded all the way to black or white, between two maps or screens; it decides nothing, so the zoom holds through a door
constexpr int FADE = 5;
// a menu screen over the field (the party, the bag), and Gen 5's X menu
constexpr int MENU = 6;
}

// What the camera sees of one presented frame.
//   ShowWidth: the layout's width (Wd); FrameWidth: the frame's width (Wr, what the core renders at)
//   TopWingLines: the DS top screen's lines with 3D wings this frame, the dual-screen wing hold's held lines included
//   AreScreensSwapped: whether the DS bottom screen is the wide one, which keeps the camera at 1x
//   FrameMargin: the frame's rows of 3D above and below the top screen, so it's 192 + 2 * FrameMargin tall
//   GameScene: what a known Pokemon game is doing (GameScene), or GameScene::UNKNOWN for other games
//   MaxWidth: the widest the camera zooms out to, or 0 for the frame's width. The core renders only as wide as the player's zoom needs
//     (WidescreenLaunch::GetRenderWidth), so the frame can be narrower than this
struct WideCameraFrame
{
    int ShowWidth = 0;
    int FrameWidth = 0;
    int TopWingLines = 0;
    bool AreScreensSwapped = false;
    int FrameMargin = 0;
    int GameScene = GameScene::UNKNOWN;
    int MaxWidth = 0;
};

// The player's camera input since the last frame; mutable so WideCameraController can reuse one.
//   StickX, StickY: the right stick, -1 to 1 (right and down are positive), before the dead zone
//   ZoomPresses: Camera Zoom hotkey presses, each one the next zoom step (WideCamera::CycleZoom). Update zeroes it once it has applied them
//     (or dropped them for a script's zoom), so presses that arrive before a wide frame can wait for one
//   DragFraction: how far a touch dragged the picture, as a fraction of the screen's width (right is positive)
//   PinchScale: how much a pinch spread the fingers (above 1) or closed them, multiplied over the frame
//   IsTouching: whether a finger is on the wide screen; IsPlayerMoving: whether the D-pad or the left stick is held
//   DragFractionY: how far a touch dragged the picture down, as a fraction of its drawn height
struct WideCameraInput
{
    float StickX = 0;
    int ZoomPresses = 0;
    float DragFraction = 0;
    float PinchScale = 1;
    bool IsTouching = false;
    bool IsPlayerMoving = false;
    float StickY = 0;
    float DragFractionY = 0;
};

// The camera itself. The zoom's range comes from the widest view (MaxWidth), but the core renders only as wide as the player's zoom needs
// (PlayerZoom), so a zoom-out can be ahead of the frame for a frame or two while the wider one is on its way; the window never leaves the
// frame it's given. In a Pokemon game the native field read knows (a GameScene other than UNKNOWN), the scene decides when to pull in to 1x
// instead of the wing lines, and the zoom and the look-around move the way PokeDaisy's WideCamera.kt does (UpdateForScene)
class WideCamera
{
public:
    // zoom steps of the Camera Zoom hotkey, in zoom factor; the widest zoom is a step of its own when the range doesn't end on one
    static constexpr float ZOOM_STEP = 0.25f;
    // right stick: dead zone, and DS pixels per second at full tilt
    static constexpr float STICK_DEAD_ZONE = 0.2f;
    static constexpr float PAN_SPEED = 300.f;
    // easing time constants: how fast the shown zoom and pan catch up, and how fast the pan drifts back to the middle while the player moves
    static constexpr float ZOOM_SECONDS = 0.12f;
    static constexpr float PAN_SECONDS = 0.08f;
    static constexpr float RETURN_SECONDS = 0.5f;
    // how long a frame has to stay free of the things that hold 1x before the player's zoom comes back
    static constexpr float RESUME_SECONDS = 0.25f;
    // fewer top-screen lines with 3D wings than this holds 1x
    static constexpr int MIN_WING_LINES = 1;
    // known games, from PokeDaisy's WideCamera.kt: the zoom's eased moves (in, back out), the look-around's springs (following the stick or
    // pulled in, and back to the player who walks), how far inside the window the player stays while the view follows the stick (DS
    // pixels), and how long after the close-up the player's zoom comes back
    static constexpr float PULL_IN_SECONDS = 0.42f;
    static constexpr float ZOOM_OUT_SECONDS = 0.65f;
    static constexpr float LOOK_SECONDS = 0.12f;
    static constexpr float WALK_RETURN_SECONDS = 0.18f;
    static constexpr float PLAYER_KEEP = 28.f;
    static constexpr float CLOSE_RETURN_SECONDS = 0.5f;
    // ours: how long TEXT straight after a fade decides nothing (an HG/SS building exit reads as TEXT for about 0.8 s)
    static constexpr float AFTER_FADE_TEXT_SECONDS = 1.5f;
    // a longer gap (a pause, a stall) counts as this, so the camera doesn't jump
    static constexpr float MAX_DELTA_SECONDS = 0.1f;

    bool ReturnsToCenter = true;
    // known games: whether a text box (GameScene::TEXT) pulls in to 1x
    bool ZoomsInOnText = true;

    std::optional<WideWindow> Update(const WideCameraFrame& frame, WideCameraInput& input, const WideCameraRequest& request,
        float deltaSeconds);

    // Moves the camera on by [deltaSeconds] for a frame (see WideCameraFrame) and returns the window to show, or nothing when the frame isn't
    // wider than the show width (the screen is then drawn the way it is without a camera). A frame as wide as the show width (the core at
    // 1x) still moves the camera when [maxWidth] leaves room to zoom out, so the player can zoom out from it
    std::optional<WideWindow> Update(int showWidth, int frameWidth, int frameMargin, int topWingLines, bool areScreensSwapped,
        WideCameraInput& input, const WideCameraRequest& request, float deltaSeconds, int gameScene = GameScene::UNKNOWN, int maxWidth = 0);

    // starts the player's zoom at [zoom] (the step remembered for the game), clamped to the frame once it's known
    void SetPlayerZoom(float zoom);

    // The zoom the player's camera is at once nothing pulls it in: the hotkey's step, a pinch, the remembered start zoom or a script's
    // zoom, but not the pull-ins (a scene, swapped screens, hold_native, no wing lines), which only change what's shown. The core's render
    // width follows this (WidescreenLaunch::GetRenderWidth), so a pull-in never resizes it. Within the range the last update saw, or
    // unclamped (at least 1) before it
    float PlayerZoom() const { return TargetZoom; }
    // the zoom shown on the last frame (1 while pulled in or on a frame that gives no window), which eases toward the player's zoom
    float ShownZoom() const { return Zoom; }
    // the zoom the last hotkey press picked, once, or nothing if there was none since the last call
    std::optional<float> TakePickedZoom();

    // back to 1x in the middle, for a camera that's off or a new session
    void Reset();

    // what a Camera Zoom press goes to from [zoom]: the next step above it (a pinched zoom between steps goes to the one above), or 1x from
    // the widest view, like PokeDaisy's Expand. With a widest view of 1.5 that's 1, 1.25, 1.5, 1, ...
    static float CycleZoom(float zoom, float maxZoom);
    // the widest zoom for a layout [showWidth] wide: the widest view [maxWidth], or the frame's [frameWidth] when that's 0
    static float GetMaxZoom(int showWidth, int frameWidth, int maxWidth);
    // The zoom the core renders for when the player's zoom is [zoom] (PlayerZoom): the next hotkey step at or above it, at least 1 and at
    // most [maxZoom], so a pinch changes the render width only when it crosses a step. 1.1 renders for 1.25, 1.25 for itself
    static float GetRenderZoom(float zoom, float maxZoom);
    // how far a window at [zoom] can sit off center in a [frameWidth]-wide frame
    static float GetMaxPan(int showWidth, int frameWidth, float zoom);
    // how far a window at [zoom] can sit off center vertically in a [frameHeight]-tall frame (none without a margin)
    static float GetMaxPanY(int frameHeight, float zoom);
    // the rows a window at [zoom] shows of a [frameHeight]-tall frame: as many as keep the DS's pixel shape, at most the frame's (the rest
    // is letterboxed)
    static float GetWindowHeight(int frameHeight, float zoom);

private:
    WideWindow Move(int showWidth, int frameWidth, int frameMargin, int topWingLines, bool areScreensSwapped, WideCameraInput& input,
        const WideCameraRequest& request, float deltaSeconds, int gameScene, float maxZoom);
    WideWindow UpdateForScene(int showWidth, int frameWidth, int frameHeight, int gameScene, const WideCameraInput& input,
        const WideCameraRequest& request, float dt, float maxZoom, float frameZoom);
    void UpdateSceneZoom(bool isClose, const WideCameraInput& input, const WideCameraRequest& request, float dt, float maxZoom,
        float frameZoom);
    void ResetPan();

    // the player's zoom and pan, which the shown ones ease toward; a script's request replaces them while it lasts and leaves them where it
    // was
    float TargetZoom = 1;
    float TargetPan = 0;
    float TargetPanY = 0;
    // what's shown
    float Zoom = 1;
    float Pan = 0;
    float PanY = 0;
    // how long the frame has been free to leave 1x, so a frame or two of wings after a menu doesn't zoom back out
    float FreeSeconds = 0;
    // known games: whether the scene wants the close-up at 1x, and for how long it hasn't (PokeDaisy's closeOverSince). Close from the
    // start, so the player's zoom comes back once the field shows
    bool IsCloseWanted = true;
    float CloseFreeSeconds = 0;
    // known games: a fade was the last scene that wasn't TEXT, and the seconds of TEXT since it. Leaving a building in HG/SS reads as TEXT
    // for about 0.8 s after the fade, so TEXT that follows a fade with no FIELD frame between decides nothing for AFTER_FADE_TEXT_SECONDS
    bool IsAfterFade = false;
    float AfterFadeTextSeconds = 0;
    // known games: the zoom's eased move from TweenFrom to TweenTarget over TweenSeconds, TweenElapsed into it (PokeDaisy's tween); a NaN
    // target makes the next one start from wherever the zoom is
    float TweenFrom = 1;
    float TweenTarget = std::numeric_limits<float>::quiet_NaN();
    float TweenElapsed = 0;
    float TweenSeconds = 0;
    // known games: the last scene that wasn't UNKNOWN, used while the read gives UNKNOWN for a frame (a reset, a state load, the first poll
    // after the camera comes on), so a known game never drops to the wing-line rules; Reset forgets it, which a new session does
    int LastKnownScene = GameScene::UNKNOWN;
    // the zoom the player last picked with the Camera Zoom hotkey, set by the update that applied the press, for the caller to remember
    // for the game (TakePickedZoom)
    std::optional<float> PickedZoom;

};

}
