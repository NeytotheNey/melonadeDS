// The input mapping: what each key and each controller button does in game, per device, with a primary and an alternate binding for every
// DS button and hotkey like the app's InputConfig. A binding is one input or a combo of up to three held together (a key, a controller
// button, or a stick or trigger pushed one way). Controller combos fire once all their parts were held for half a second, so a game's own
// combos (Select + Start, a soft reset's L + R + Start + Select) still reach it; keyboard combos (Ctrl + S) fire at once. While a combo is
// on, its parts don't reach the game. The keyboard has one profile, each controller model (its SDL GUID) another, made from the defaults
// the first time it's seen; all of them live in input.json in the profile folder.
//
// The controllers are SDL GameControllers (gamecontrollerdb.txt next to the exe adds mappings), opened as they're plugged in, with their
// face buttons reported by position (SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS off), so a profile's face layout decides where the DS's A
// is: Nintendo (east = A, the default for Nintendo pads) or positional (south = A, for Xbox and PlayStation pads, as their labels say).
//
// The right stick moves a touch cursor over the bottom screen, and the Touch binding (the right trigger by default) taps it. UI thread only
#pragma once

#include <SDL.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "Controls.h"
#include "types.h"

namespace Frontend
{

// What a binding does. The DS buttons come first, in MelonInstance::pressKey's bit order
enum class Action
{
    A, B, Select, Start, Right, Left, Up, Down, R, L, X, Y,
    // a tap on the bottom screen at the touch cursor
    TouchTap,
    // hotkeys
    Pause, FastForwardHold, FastForward, QuickSave, QuickLoad, SwapScreens, DetachScreens, Screenshot, Fullscreen, Rewind,
    Count
};
constexpr int kActionCount = (int)Action::Count;
constexpr int kDsButtonCount = 12;

// the name in input.json: the app's Input names (A, START, FAST_FORWARD_HOLD, QUICK_SAVE, ...), and DETACH_SCREENS, SCREENSHOT,
// FULLSCREEN and TOUCH_TAP for the desktop's own
const char* ActionKey(Action action);
// "Fast forward (hold)"
const char* ActionLabel(Action action);
// held for as long as the binding is (DS buttons, the touch tap, fast forward hold); the others fire once per press
bool IsHeldAction(Action action);

struct InputPart
{
    enum class Kind : uint8_t { None, Key, Button, Axis };
    Kind Type = Kind::None;
    int Code = 0; // SDL_Keycode, SDL_GameControllerButton or SDL_GameControllerAxis
    int Dir = 0;  // an axis: +1 or -1
    bool operator==(const InputPart& other) const { return Type == other.Type && Code == other.Code && Dir == other.Dir; }
    bool operator<(const InputPart& other) const
    {
        return Type != other.Type ? Type < other.Type : Code != other.Code ? Code < other.Code : Dir < other.Dir;
    }
};

// one input, or up to three held together
struct Binding
{
    std::vector<InputPart> Parts;
    bool Empty() const { return Parts.empty(); }
    bool operator==(const Binding& other) const { return Parts == other.Parts; }
};

struct ActionBinding
{
    Binding Primary, Alt;
};

enum class FaceLayout { Nintendo, Positional };

struct DeviceProfile
{
    std::string Id;   // "keyboard", or the controller's GUID
    std::string Name;
    bool Keyboard = false;
    SDL_GameControllerType Type = SDL_CONTROLLER_TYPE_UNKNOWN;
    FaceLayout Layout = FaceLayout::Positional;
    std::array<ActionBinding, kActionCount> Map;
};

FaceLayout DefaultLayout(SDL_GameControllerType type);
// the bindings a new profile gets
void SetKeyboardDefaults(DeviceProfile& profile);
void SetControllerDefaults(DeviceProfile& profile);

// input.json's form: "key:Right Shift", "button:a", "axis:+righttrigger"
std::string PartName(const InputPart& part);
bool ParsePart(const std::string& text, InputPart& part);
// as the device labels it: "Right Shift", "A", "ZR", "Left stick up"; a combo joined with " + ", a controller one with "(hold)"
std::string PartLabel(const InputPart& part, const DeviceProfile& profile);
std::string BindingLabel(const Binding& binding, const DeviceProfile& profile);

class Input
{
public:
    static constexpr int kComboHoldMs = 500;
    static constexpr int kCaptureTimeoutMs = 5000;
    // a source that isn't a controller (keyboard actions, --at key=)
    static constexpr SDL_JoystickID kKeyboard = -2;

    // reads [dir]\input.json (missing: the defaults); [hidden]: a scripted run, which opens only virtual controllers (--virtual-pad)
    void Load(const std::filesystem::path& dir, bool hidden);
    bool Save();

    // --virtual-pad TYPE (xbox, switch, ps, or generic: a joystick with no mapping): an SDL virtual controller of that model, for tests in hidden runs; its buttons and axes are
    // set with SetVirtual*, which go through SDL's events like a real pad's. Its rumble is printed
    bool AttachVirtualPad(const std::string& type);
    bool HasVirtualPad() const { return VirtualIndex >= 0; }
    bool SetVirtualButton(int button, bool down);
    bool SetVirtualAxis(int axis, int value);

    // SDL_CONTROLLERDEVICEADDED / REMOVED (and the ones there at the start)
    void OpenController(int deviceIndex);
    // Every joystick SDL sees (at the start, and SDL_JOYDEVICEADDED), logged once with its name, GUID, ids and whether SDL takes it as a
    // game controller. False for one that isn't (no mapping in SDL or gamecontrollerdb.txt): it can't be bound, and [name] says which
    bool DescribeJoystick(int deviceIndex, std::string& name);
    // the SDL hints that decide which drivers see a pad (HIDAPI for Switch, PlayStation and Xbox pads, RawInput, background events)
    static void LogDriverHints();
    void CloseController(SDL_JoystickID id);
    struct Pad
    {
        SDL_GameController* Controller = nullptr;
        SDL_JoystickID Id = -1;
        std::string Guid, Name;
        SDL_GameControllerType Type = SDL_CONTROLLER_TYPE_UNKNOWN;
        bool Virtual = false;
    };
    const std::vector<Pad>& Pads() const { return OpenPads; }

    // A key, controller button or axis event the menus didn't take: the state changes, and the actions it starts or ends go to OnAction
    // (and the DS buttons to GameMask())
    void OnEvent(const SDL_Event& event);
    // nothing held any more (a menu opened over the game, which takes the input from here on)
    void ReleaseAll();
    // once per pass of the frame loop: combos held long enough fire, the touch cursor moves
    void Tick();
    // the hotkeys a key typed alone fires (--at key=), pressed and released
    void TypeKeyHotkeys(SDL_Keycode key, Uint16 mod);

    // an action started (down) or ended; a once-per-press one only comes down. [source] is the controller it came from, or kKeyboard
    std::function<void(Action action, bool down, SDL_JoystickID source)> OnAction;
    // the DS buttons held, MelonInstance::pressKey's bits
    melonDS::u32 GameMask() const { return Mask; }
    bool TouchTapHeld() const { return Active[(int)Action::TouchTap]; }

    // The right stick's touch cursor, in DS bottom-screen pixels (0-255, 0-191). Visible while it moves, a tap is held, or for a few seconds
    // after
    struct Cursor { bool Visible = false; float X = 128, Y = 96; bool Pressed = false; };
    Cursor GetCursor() const;

    // the menus: what a controller's button does there (A and B by the pad's face layout; a pad that isn't open gets Nintendo's)
    MenuAction MenuButton(SDL_JoystickID which, int button) const;

    // the profiles, for the setup screen: the keyboard first, then the controllers plugged in, then the others input.json knows
    std::vector<std::string> DeviceIds() const;
    DeviceProfile* Profile(const std::string& id);
    bool IsConnected(const std::string& id) const;
    void SetBinding(const std::string& id, Action action, bool alt, const Binding& binding);
    void ResetDefaults(const std::string& id);
    void SetLayout(const std::string& id, FaceLayout layout);

    // "Press a button": the next input of the device [id] (any controller of that model) becomes [action]'s binding. Held together they make
    // a combo, taken when the first one is let go. Ends after kCaptureTimeoutMs, or when another device's Esc or B cancels it
    void BeginCapture(const std::string& id, Action action, bool alt);
    bool Capturing() const { return CaptureOn; }
    void CancelCapture();
    // what's held so far, and the seconds left
    std::string CaptureText();
    float CaptureSecondsLeft() const;
    // true when it took the event (every key, button and axis while capturing)
    bool CaptureEvent(const SDL_Event& event);
    enum class CaptureResult { None, Bound, Cancelled, TimedOut };
    // the last capture's end, once
    CaptureResult TakeCaptureResult();
    // the device the capture waits for (it moves to another one when that one's L or R, or Q / W, is pressed during it)
    const std::string& CaptureDeviceId() const { return CaptureDevice; }
    // a press on another device during the capture, said for a few seconds: which device it waits for, and how to switch to the other
    std::string CaptureHint() const;

    // Every key and controller button pressed (and stick pushed), menus or game: the device it came from, which the setup screen opens
    // on. "keyboard", or the profile id (GUID) of the pad pressed last
    void NoteDevice(SDL_JoystickID source);
    std::string LastDeviceId() const;
    // a device's name for the menus ("Keyboard", the pad's)
    std::string DeviceName(const std::string& id);

    // the controller the game was last played with: rumble goes to it
    void Rumble(int durationMs);
    void StopRumble();
    bool RumbleEnabled = true;

private:
    struct DeviceState
    {
        std::set<InputPart> Held;
        std::set<InputPart> Suppressed;
        // when each combo binding had all its parts held (0: not all held), [action][alt]
        std::array<std::array<std::chrono::steady_clock::time_point, 2>, kActionCount> Since {};
        std::array<std::array<bool, 2>, kActionCount> ComboOn {};
        int RightX = 0, RightY = 0;
    };
    DeviceState& State(SDL_JoystickID source);
    DeviceProfile& ProfileFor(SDL_JoystickID source);
    const Pad* FindPad(SDL_JoystickID id) const;
    static bool PartHeld(const InputPart& part, const DeviceState& state);
    void Update();
    void SetHeld(DeviceState& state, const InputPart& part, bool held);
    void FinishCapture(CaptureResult result);
    DeviceProfile& EnsureProfile(const std::string& id, const std::string& name, SDL_GameControllerType type);

    std::filesystem::path File;
    bool Hidden = false;
    std::vector<DeviceProfile> Profiles;
    std::vector<Pad> OpenPads;
    std::set<SDL_JoystickID> Described;
    // the keyboard's state, then each controller's by instance id
    DeviceState KeyboardState;
    std::vector<std::pair<SDL_JoystickID, DeviceState>> PadStates;
    std::array<bool, kActionCount> Active {};
    melonDS::u32 Mask = 0;
    SDL_JoystickID LastPad = -1;
    int VirtualIndex = -1;
    SDL_Joystick* VirtualJoystick = nullptr;

    // the touch cursor
    float CursorX = 128, CursorY = 96;
    std::chrono::steady_clock::time_point CursorShownUntil {};
    std::chrono::steady_clock::time_point LastTick {};

    bool CaptureOn = false;
    std::string CaptureDevice;
    Action CaptureAction = Action::A;
    bool CaptureAlt = false;
    std::chrono::steady_clock::time_point CaptureUntil {};
    std::vector<InputPart> CaptureParts;
    std::set<InputPart> CaptureHeld;
    CaptureResult LastCapture = CaptureResult::None;
    std::string HintText;
    std::chrono::steady_clock::time_point HintUntil {};
    SDL_JoystickID LastUsed = kKeyboard;
};

}
