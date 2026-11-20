#include "Input.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "picojson/picojson.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace Frontend
{

namespace
{

struct ActionInfo { const char* Key; const char* Label; bool Held; };
const ActionInfo kActions[kActionCount] = {
    {"A", "A", true}, {"B", "B", true}, {"SELECT", "Select", true}, {"START", "Start", true},
    {"RIGHT", "Right", true}, {"LEFT", "Left", true}, {"UP", "Up", true}, {"DOWN", "Down", true},
    {"R", "R", true}, {"L", "L", true}, {"X", "X", true}, {"Y", "Y", true},
    {"TOUCH_TAP", "Tap the touch screen", true},
    {"PAUSE", "Pause menu", false},
    {"FAST_FORWARD_HOLD", "Fast forward (hold)", true},
    {"FAST_FORWARD", "Fast forward (on/off)", false},
    {"QUICK_SAVE", "Quick save", false},
    {"QUICK_LOAD", "Quick load", false},
    {"SWAP_SCREENS", "Swap screens", false},
    {"DETACH_SCREENS", "Detach screens", false},
    {"SCREENSHOT", "Screenshot", false},
    {"FULLSCREEN", "Fullscreen", false},
    {"REWIND", "Rewind", false},
};

// a stick counts as pushed one way past this (of 32767), a trigger as pulled past the other
constexpr int kStickThreshold = 16000;
constexpr int kTriggerThreshold = 8000;
// for "press a button": a firmer push, so a stick resting off center doesn't bind
constexpr int kCaptureAxisDown = 22000;
constexpr int kCaptureAxisUp = 10000;
// the touch cursor: the right stick's dead zone (of 1), its top speed in DS pixels a second, and how long it stays after it stops
constexpr float kCursorDeadZone = 0.22f;
constexpr float kCursorSpeed = 360.f;
constexpr int kCursorShowMs = 3000;

InputPart Key(SDL_Keycode key) { return {InputPart::Kind::Key, (int)key, 0}; }
InputPart Button(SDL_GameControllerButton button) { return {InputPart::Kind::Button, (int)button, 0}; }
InputPart Axis(SDL_GameControllerAxis axis, int dir) { return {InputPart::Kind::Axis, (int)axis, dir}; }
Binding One(InputPart part) { return Binding {{part}}; }
Binding Two(InputPart a, InputPart b) { return Binding {{a, b}}; }

// Ctrl, Alt and the Windows key are the same on both sides, so Ctrl + S works with either Ctrl (the shifts stay apart: Right Shift is the
// DS's Select)
SDL_Keycode NormalizeKey(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_RCTRL: return SDLK_LCTRL;
    case SDLK_RALT: return SDLK_LALT;
    case SDLK_RGUI: return SDLK_LGUI;
    default: return key;
    }
}

bool IsTrigger(int axis) { return axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT; }

bool IsNintendo(SDL_GameControllerType type)
{
    switch (type)
    {
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return true;
    default:
        return false;
    }
}

bool IsSony(SDL_GameControllerType type)
{
    return type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4 || type == SDL_CONTROLLER_TYPE_PS5;
}

// the face buttons trade places between the two layouts: south and east, west and north
int SwapFace(int button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A: return SDL_CONTROLLER_BUTTON_B;
    case SDL_CONTROLLER_BUTTON_B: return SDL_CONTROLLER_BUTTON_A;
    case SDL_CONTROLLER_BUTTON_X: return SDL_CONTROLLER_BUTTON_Y;
    case SDL_CONTROLLER_BUTTON_Y: return SDL_CONTROLLER_BUTTON_X;
    default: return button;
    }
}

std::string ButtonLabel(int button, SDL_GameControllerType type)
{
    bool nintendo = IsNintendo(type), sony = IsSony(type);
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A: return nintendo ? "B" : sony ? "Cross" : "A";
    case SDL_CONTROLLER_BUTTON_B: return nintendo ? "A" : sony ? "Circle" : "B";
    case SDL_CONTROLLER_BUTTON_X: return nintendo ? "Y" : sony ? "Square" : "X";
    case SDL_CONTROLLER_BUTTON_Y: return nintendo ? "X" : sony ? "Triangle" : "Y";
    case SDL_CONTROLLER_BUTTON_BACK: return nintendo ? "Minus" : sony ? (type == SDL_CONTROLLER_TYPE_PS5 ? "Create" : "Share") : "View";
    case SDL_CONTROLLER_BUTTON_GUIDE: return nintendo ? "Home" : sony ? "PS" : "Guide";
    case SDL_CONTROLLER_BUTTON_START: return nintendo ? "Plus" : sony ? "Options" : "Menu";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return sony ? "L3" : "Left stick press";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return sony ? "R3" : "Right stick press";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return nintendo ? "L" : sony ? "L1" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return nintendo ? "R" : sony ? "R1" : "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D-pad up";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D-pad down";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D-pad left";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-pad right";
    case SDL_CONTROLLER_BUTTON_MISC1: return nintendo ? "Capture" : sony ? "Mic" : "Share";
    case SDL_CONTROLLER_BUTTON_PADDLE1: return "Paddle 1";
    case SDL_CONTROLLER_BUTTON_PADDLE2: return "Paddle 2";
    case SDL_CONTROLLER_BUTTON_PADDLE3: return "Paddle 3";
    case SDL_CONTROLLER_BUTTON_PADDLE4: return "Paddle 4";
    case SDL_CONTROLLER_BUTTON_TOUCHPAD: return "Touchpad";
    default: return "Button " + std::to_string(button);
    }
}

std::string AxisLabel(int axis, int dir, SDL_GameControllerType type)
{
    bool nintendo = IsNintendo(type), sony = IsSony(type);
    switch (axis)
    {
    case SDL_CONTROLLER_AXIS_LEFTX: return dir > 0 ? "Left stick right" : "Left stick left";
    case SDL_CONTROLLER_AXIS_LEFTY: return dir > 0 ? "Left stick down" : "Left stick up";
    case SDL_CONTROLLER_AXIS_RIGHTX: return dir > 0 ? "Right stick right" : "Right stick left";
    case SDL_CONTROLLER_AXIS_RIGHTY: return dir > 0 ? "Right stick down" : "Right stick up";
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return nintendo ? "ZL" : sony ? "L2" : "LT";
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return nintendo ? "ZR" : sony ? "R2" : "RT";
    default: return "Axis " + std::to_string(axis);
    }
}

picojson::value BindingJson(const Binding& binding)
{
    picojson::array parts;
    for (const InputPart& part : binding.Parts)
        parts.emplace_back(PartName(part));
    return picojson::value(parts);
}

Binding BindingFromJson(const picojson::value& value)
{
    Binding binding;
    if (!value.is<picojson::array>())
        return binding;
    for (const picojson::value& item : value.get<picojson::array>())
    {
        InputPart part;
        if (item.is<std::string>() && ParsePart(item.get<std::string>(), part) && binding.Parts.size() < 3)
            binding.Parts.push_back(part);
    }
    return binding;
}

int ActionFromKey(const std::string& key)
{
    for (int i = 0; i < kActionCount; i++)
    {
        if (key == kActions[i].Key)
            return i;
    }
    return -1;
}

}

const char* ActionKey(Action action) { return kActions[(int)action].Key; }
const char* ActionLabel(Action action) { return kActions[(int)action].Label; }
bool IsHeldAction(Action action) { return kActions[(int)action].Held; }

FaceLayout DefaultLayout(SDL_GameControllerType type)
{
    return IsNintendo(type) ? FaceLayout::Nintendo : FaceLayout::Positional;
}

void SetKeyboardDefaults(DeviceProfile& p)
{
    p.Map = {};
    auto set = [&](Action a, Binding primary, Binding alt = {}) { p.Map[(int)a] = {primary, alt}; };
    // the test frontend's keys, as before
    set(Action::A, One(Key(SDLK_x)));
    set(Action::B, One(Key(SDLK_z)));
    set(Action::X, One(Key(SDLK_s)));
    set(Action::Y, One(Key(SDLK_a)));
    set(Action::L, One(Key(SDLK_q)));
    set(Action::R, One(Key(SDLK_w)));
    set(Action::Start, One(Key(SDLK_RETURN)));
    set(Action::Select, One(Key(SDLK_RSHIFT)), One(Key(SDLK_BACKSPACE)));
    set(Action::Up, One(Key(SDLK_UP)));
    set(Action::Down, One(Key(SDLK_DOWN)));
    set(Action::Left, One(Key(SDLK_LEFT)));
    set(Action::Right, One(Key(SDLK_RIGHT)));
    set(Action::Pause, One(Key(SDLK_ESCAPE)));
    set(Action::FastForwardHold, One(Key(SDLK_TAB)));
    set(Action::FastForward, One(Key(SDLK_BACKQUOTE)));
    set(Action::QuickSave, Two(Key(SDLK_LCTRL), Key(SDLK_s)));
    set(Action::QuickLoad, Two(Key(SDLK_LCTRL), Key(SDLK_l)));
    // the F-keys the test frontend had
    set(Action::SwapScreens, One(Key(SDLK_F1)));
    set(Action::DetachScreens, One(Key(SDLK_F9)));
    set(Action::Screenshot, One(Key(SDLK_F12)));
    set(Action::Fullscreen, One(Key(SDLK_F11)), Two(Key(SDLK_LALT), Key(SDLK_RETURN)));
}

void SetControllerDefaults(DeviceProfile& p)
{
    p.Map = {};
    auto set = [&](Action a, Binding primary, Binding alt = {}) { p.Map[(int)a] = {primary, alt}; };
    bool nintendo = p.Layout == FaceLayout::Nintendo;
    // Nintendo: the DS's own places (east A, south B, north X, west Y); positional: as an Xbox pad's labels say (south A, east B, west X,
    // north Y)
    set(Action::A, One(Button(nintendo ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A)));
    set(Action::B, One(Button(nintendo ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B)));
    set(Action::X, One(Button(nintendo ? SDL_CONTROLLER_BUTTON_Y : SDL_CONTROLLER_BUTTON_X)));
    set(Action::Y, One(Button(nintendo ? SDL_CONTROLLER_BUTTON_X : SDL_CONTROLLER_BUTTON_Y)));
    set(Action::L, One(Button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)));
    set(Action::R, One(Button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)));
    set(Action::Start, One(Button(SDL_CONTROLLER_BUTTON_START)));
    set(Action::Select, One(Button(SDL_CONTROLLER_BUTTON_BACK)));
    set(Action::Up, One(Button(SDL_CONTROLLER_BUTTON_DPAD_UP)), One(Axis(SDL_CONTROLLER_AXIS_LEFTY, -1)));
    set(Action::Down, One(Button(SDL_CONTROLLER_BUTTON_DPAD_DOWN)), One(Axis(SDL_CONTROLLER_AXIS_LEFTY, 1)));
    set(Action::Left, One(Button(SDL_CONTROLLER_BUTTON_DPAD_LEFT)), One(Axis(SDL_CONTROLLER_AXIS_LEFTX, -1)));
    set(Action::Right, One(Button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)), One(Axis(SDL_CONTROLLER_AXIS_LEFTX, 1)));
    set(Action::TouchTap, One(Axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1)), One(Button(SDL_CONTROLLER_BUTTON_RIGHTSTICK)));
    // Guide, and Select + Start held for pads whose Guide button doesn't reach SDL
    set(Action::Pause, One(Button(SDL_CONTROLLER_BUTTON_GUIDE)), Two(Button(SDL_CONTROLLER_BUTTON_BACK), Button(SDL_CONTROLLER_BUTTON_START)));
    set(Action::FastForwardHold, One(Axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1)));
    set(Action::QuickSave, Two(Button(SDL_CONTROLLER_BUTTON_BACK), Button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)));
    set(Action::QuickLoad, Two(Button(SDL_CONTROLLER_BUTTON_BACK), Button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)));
    set(Action::SwapScreens, One(Button(SDL_CONTROLLER_BUTTON_LEFTSTICK)));
}

std::string PartName(const InputPart& part)
{
    switch (part.Type)
    {
    case InputPart::Kind::Key: return std::string("key:") + SDL_GetKeyName((SDL_Keycode)part.Code);
    case InputPart::Kind::Button:
    {
        const char* name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)part.Code);
        return std::string("button:") + (name ? name : "?");
    }
    case InputPart::Kind::Axis:
    {
        const char* name = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)part.Code);
        return std::string("axis:") + (part.Dir < 0 ? "-" : "+") + (name ? name : "?");
    }
    default: return "";
    }
}

bool ParsePart(const std::string& text, InputPart& part)
{
    if (text.rfind("key:", 0) == 0)
    {
        SDL_Keycode key = SDL_GetKeyFromName(text.c_str() + 4);
        if (key == SDLK_UNKNOWN)
            return false;
        part = Key(NormalizeKey(key));
        return true;
    }
    if (text.rfind("button:", 0) == 0)
    {
        SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(text.c_str() + 7);
        if (button == SDL_CONTROLLER_BUTTON_INVALID)
            return false;
        part = Button(button);
        return true;
    }
    if (text.rfind("axis:", 0) == 0 && text.size() > 6 && (text[5] == '+' || text[5] == '-'))
    {
        SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(text.c_str() + 6);
        if (axis == SDL_CONTROLLER_AXIS_INVALID)
            return false;
        part = Axis(axis, text[5] == '-' ? -1 : 1);
        return true;
    }
    return false;
}

std::string PartLabel(const InputPart& part, const DeviceProfile& profile)
{
    switch (part.Type)
    {
    case InputPart::Kind::Key:
        switch (part.Code)
        {
        case SDLK_LCTRL: return "Ctrl";
        case SDLK_LALT: return "Alt";
        case SDLK_LGUI: return "Windows";
        case SDLK_RETURN: return "Enter";
        case SDLK_ESCAPE: return "Esc";
        case SDLK_BACKQUOTE: return "` (backquote)";
        default: return SDL_GetKeyName((SDL_Keycode)part.Code);
        }
    case InputPart::Kind::Button: return ButtonLabel(part.Code, profile.Type);
    case InputPart::Kind::Axis: return AxisLabel(part.Code, part.Dir, profile.Type);
    default: return "";
    }
}

std::string BindingLabel(const Binding& binding, const DeviceProfile& profile)
{
    std::string text;
    for (const InputPart& part : binding.Parts)
    {
        if (!text.empty())
            text += " + ";
        text += PartLabel(part, profile);
    }
    if (binding.Parts.size() > 1 && !profile.Keyboard)
        text += " (hold)";
    return text;
}

// ---- the profiles

DeviceProfile& Input::EnsureProfile(const std::string& id, const std::string& name, SDL_GameControllerType type)
{
    for (DeviceProfile& profile : Profiles)
    {
        if (profile.Id == id)
        {
            // a known model plugged in: its name and type as SDL says now
            if (!name.empty())
                profile.Name = name;
            if (type != SDL_CONTROLLER_TYPE_UNKNOWN)
                profile.Type = type;
            return profile;
        }
    }
    DeviceProfile profile;
    profile.Id = id;
    profile.Name = name;
    profile.Keyboard = id == "keyboard";
    profile.Type = type;
    if (profile.Keyboard)
        SetKeyboardDefaults(profile);
    else
    {
        profile.Layout = DefaultLayout(type);
        SetControllerDefaults(profile);
    }
    Profiles.push_back(profile);
    printf("input: new profile for %s (%s)\n", name.c_str(), profile.Keyboard ? "keyboard" : profile.Layout == FaceLayout::Nintendo
        ? "Nintendo layout, east = A" : "positional layout, south = A");
    return Profiles.back();
}

void Input::Load(const fs::path& dir, bool hidden)
{
    Hidden = hidden;
    File = dir / "input.json";
    Profiles.clear();
    std::ifstream in(File, std::ios::binary);
    if (in)
    {
        std::stringstream text;
        text << in.rdbuf();
        picojson::value root;
        std::string error;
        try
        {
            error = picojson::parse(root, text.str());
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        if (!error.empty() || !root.is<picojson::object>())
            fprintf(stderr, "input: %s doesn't parse (%s), using the default bindings\n", File.string().c_str(), error.c_str());
        else if (root.get("devices").is<picojson::object>())
        {
            for (const auto& [id, value] : root.get("devices").get<picojson::object>())
            {
                if (!value.is<picojson::object>())
                    continue;
                DeviceProfile profile;
                profile.Id = id;
                profile.Keyboard = id == "keyboard";
                profile.Name = value.get("name").is<std::string>() ? value.get("name").get<std::string>() : id;
                profile.Type = value.get("type").is<double>() ? (SDL_GameControllerType)(int)value.get("type").get<double>()
                                                              : SDL_CONTROLLER_TYPE_UNKNOWN;
                profile.Layout = value.get("layout").is<std::string>() && value.get("layout").get<std::string>() == "nintendo"
                    ? FaceLayout::Nintendo : FaceLayout::Positional;
                // the defaults first, so an action input.json doesn't name (a newer one) has its default
                if (profile.Keyboard)
                    SetKeyboardDefaults(profile);
                else
                    SetControllerDefaults(profile);
                const picojson::value& bindings = value.get("bindings");
                if (bindings.is<picojson::object>())
                {
                    for (const auto& [key, pair] : bindings.get<picojson::object>())
                    {
                        int action = ActionFromKey(key);
                        if (action < 0 || !pair.is<picojson::array>())
                            continue;
                        const picojson::array& list = pair.get<picojson::array>();
                        profile.Map[action].Primary = list.size() > 0 ? BindingFromJson(list[0]) : Binding();
                        profile.Map[action].Alt = list.size() > 1 ? BindingFromJson(list[1]) : Binding();
                    }
                }
                Profiles.push_back(profile);
            }
        }
    }
    EnsureProfile("keyboard", "Keyboard", SDL_CONTROLLER_TYPE_UNKNOWN);
    LastTick = Clock::now();
}

bool Input::Save()
{
    if (File.empty())
        return false;
    picojson::object devices;
    for (const DeviceProfile& profile : Profiles)
    {
        picojson::object device;
        device["name"] = picojson::value(profile.Name);
        if (!profile.Keyboard)
        {
            device["type"] = picojson::value((double)profile.Type);
            device["layout"] = picojson::value(std::string(profile.Layout == FaceLayout::Nintendo ? "nintendo" : "positional"));
        }
        picojson::object bindings;
        for (int i = 0; i < kActionCount; i++)
        {
            picojson::array pair;
            pair.push_back(BindingJson(profile.Map[i].Primary));
            pair.push_back(BindingJson(profile.Map[i].Alt));
            bindings[kActions[i].Key] = picojson::value(pair);
        }
        device["bindings"] = picojson::value(bindings);
        devices[profile.Id] = picojson::value(device);
    }
    picojson::object root;
    root["version"] = picojson::value(1.0);
    root["devices"] = picojson::value(devices);
    fs::path temp = File;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << picojson::value(root).serialize(true);
        if (!out.good())
            return false;
    }
    if (!MoveFileExW(temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        fprintf(stderr, "input: can't write %s\n", File.string().c_str());
        return false;
    }
    return true;
}

std::vector<std::string> Input::DeviceIds() const
{
    std::vector<std::string> ids {"keyboard"};
    for (const Pad& pad : OpenPads)
    {
        if (std::find(ids.begin(), ids.end(), pad.Guid) == ids.end())
            ids.push_back(pad.Guid);
    }
    for (const DeviceProfile& profile : Profiles)
    {
        if (std::find(ids.begin(), ids.end(), profile.Id) == ids.end())
            ids.push_back(profile.Id);
    }
    return ids;
}

DeviceProfile* Input::Profile(const std::string& id)
{
    for (DeviceProfile& profile : Profiles)
    {
        if (profile.Id == id)
            return &profile;
    }
    return nullptr;
}

bool Input::IsConnected(const std::string& id) const
{
    if (id == "keyboard")
        return true;
    for (const Pad& pad : OpenPads)
    {
        if (pad.Guid == id)
            return true;
    }
    return false;
}

void Input::SetBinding(const std::string& id, Action action, bool alt, const Binding& binding)
{
    DeviceProfile* profile = Profile(id);
    if (!profile)
        return;
    (alt ? profile->Map[(int)action].Alt : profile->Map[(int)action].Primary) = binding;
    printf("input: %s %s %s = %s\n", profile->Name.c_str(), ActionKey(action), alt ? "alt" : "primary",
        binding.Empty() ? "(none)" : BindingLabel(binding, *profile).c_str());
    fflush(stdout);
    Save();
    Update();
}

void Input::ResetDefaults(const std::string& id)
{
    DeviceProfile* profile = Profile(id);
    if (!profile)
        return;
    if (profile->Keyboard)
        SetKeyboardDefaults(*profile);
    else
    {
        profile->Layout = DefaultLayout(profile->Type);
        SetControllerDefaults(*profile);
    }
    printf("input: %s back to the defaults\n", profile->Name.c_str());
    Save();
    Update();
}

void Input::SetLayout(const std::string& id, FaceLayout layout)
{
    DeviceProfile* profile = Profile(id);
    if (!profile || profile->Keyboard || profile->Layout == layout)
        return;
    profile->Layout = layout;
    // the face buttons in every binding trade places, so A stays A by the new layout (and a binding made by hand moves with it)
    for (ActionBinding& pair : profile->Map)
    {
        for (Binding* binding : {&pair.Primary, &pair.Alt})
        {
            for (InputPart& part : binding->Parts)
            {
                if (part.Type == InputPart::Kind::Button)
                    part.Code = SwapFace(part.Code);
            }
        }
    }
    printf("input: %s face layout %s\n", profile->Name.c_str(), layout == FaceLayout::Nintendo ? "nintendo" : "positional");
    Save();
    Update();
}

// ---- the controllers

namespace
{

int VirtualRumble(void* userdata, Uint16 low, Uint16 high)
{
    printf("virtual pad: rumble %u %u\n", low, high);
    fflush(stdout);
    return 0;
}

}

bool Input::AttachVirtualPad(const std::string& type)
{
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.Rumble = VirtualRumble;
    // the ids of a real pad of each kind, which SDL_GameControllerGetType reads
    if (type == "switch")
    {
        desc.vendor_id = 0x057e;
        desc.product_id = 0x2009;
        desc.name = "Virtual Switch Pro Controller";
    }
    else if (type == "ps")
    {
        desc.vendor_id = 0x054c;
        desc.product_id = 0x09cc;
        desc.name = "Virtual DualShock 4";
    }
    else
    {
        desc.vendor_id = 0x045e;
        desc.product_id = 0x02ea;
        desc.name = "Virtual Xbox Controller";
    }
    VirtualIndex = SDL_JoystickAttachVirtualEx(&desc);
    if (VirtualIndex < 0)
    {
        fprintf(stderr, "can't attach the virtual pad: %s\n", SDL_GetError());
        return false;
    }
    printf("virtual pad: %s attached\n", desc.name);
    return true;
}

bool Input::SetVirtualButton(int button, bool down)
{
    for (const Pad& pad : OpenPads)
    {
        if (pad.Virtual)
            return SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(pad.Controller), button, down ? SDL_PRESSED : SDL_RELEASED) == 0;
    }
    return false;
}

bool Input::SetVirtualAxis(int axis, int value)
{
    for (const Pad& pad : OpenPads)
    {
        if (pad.Virtual)
        {
            // a virtual pad's triggers are full-range axes (-32768 at rest) in SDL's mapping, where the game controller's are 0 to 32767
            int raw = IsTrigger(axis) ? std::clamp(value, 0, 32767) * 2 - 32768 : std::clamp(value, -32768, 32767);
            return SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(pad.Controller), axis, (Sint16)std::clamp(raw, -32768, 32767)) == 0;
        }
    }
    return false;
}

void Input::OpenController(int deviceIndex)
{
    if (!SDL_IsGameController(deviceIndex))
        return;
    bool isVirtual = SDL_JoystickIsVirtual(deviceIndex);
    // a hidden run takes no input from real controllers (it would get a pad someone is playing with), only from its virtual one
    if (Hidden && !isVirtual)
        return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    if (FindPad(id))
        return;
    SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
    if (!controller)
    {
        fprintf(stderr, "input: can't open controller %d: %s\n", deviceIndex, SDL_GetError());
        return;
    }
    Pad pad;
    pad.Controller = controller;
    pad.Id = id;
    char guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(deviceIndex), guid, sizeof(guid));
    pad.Guid = guid;
    const char* name = SDL_GameControllerName(controller);
    pad.Name = name ? name : "Controller";
    pad.Type = SDL_GameControllerGetType(controller);
    pad.Virtual = isVirtual;
    OpenPads.push_back(pad);
    EnsureProfile(pad.Guid, pad.Name, pad.Type);
    printf("input: controller %s connected (%s, type %d)\n", pad.Name.c_str(), pad.Guid.c_str(), (int)pad.Type);
    fflush(stdout);
}

void Input::CloseController(SDL_JoystickID id)
{
    for (size_t i = 0; i < OpenPads.size(); i++)
    {
        if (OpenPads[i].Id != id)
            continue;
        printf("input: controller %s disconnected\n", OpenPads[i].Name.c_str());
        SDL_GameControllerClose(OpenPads[i].Controller);
        OpenPads.erase(OpenPads.begin() + i);
        break;
    }
    // what it held is let go
    for (size_t i = 0; i < PadStates.size(); i++)
    {
        if (PadStates[i].first == id)
        {
            PadStates.erase(PadStates.begin() + i);
            break;
        }
    }
    if (LastPad == id)
        LastPad = -1;
    Update();
}

const Input::Pad* Input::FindPad(SDL_JoystickID id) const
{
    for (const Pad& pad : OpenPads)
    {
        if (pad.Id == id)
            return &pad;
    }
    return nullptr;
}

Input::DeviceState& Input::State(SDL_JoystickID source)
{
    if (source == kKeyboard)
        return KeyboardState;
    for (auto& [id, state] : PadStates)
    {
        if (id == source)
            return state;
    }
    PadStates.emplace_back(source, DeviceState());
    return PadStates.back().second;
}

DeviceProfile& Input::ProfileFor(SDL_JoystickID source)
{
    if (source == kKeyboard)
        return *Profile("keyboard");
    if (const Pad* pad = FindPad(source))
        return EnsureProfile(pad->Guid, pad->Name, pad->Type);
    // a --ui-script pad step without a virtual pad (which -1): a pad with the default profile
    return EnsureProfile("script", "Script controller", SDL_CONTROLLER_TYPE_UNKNOWN);
}

void Input::Rumble(int durationMs)
{
    if (!RumbleEnabled)
        return;
    const Pad* pad = FindPad(LastPad);
    if (!pad && !OpenPads.empty())
        pad = &OpenPads.front();
    if (!pad)
        return;
    // the Rumble Pak is one motor that's on or off; both of the pad's at about three quarters
    SDL_GameControllerRumble(pad->Controller, 0xC000, 0xC000, durationMs > 0 ? (Uint32)durationMs : 1000);
}

void Input::StopRumble()
{
    for (const Pad& pad : OpenPads)
        SDL_GameControllerRumble(pad.Controller, 0, 0, 0);
}

MenuAction Input::MenuButton(SDL_JoystickID which, int button) const
{
    MenuAction action = MenuButtonAction(button);
    const Pad* pad = FindPad(which);
    if (!pad)
        return action;
    const DeviceProfile* profile = nullptr;
    for (const DeviceProfile& p : Profiles)
    {
        if (p.Id == pad->Guid)
            profile = &p;
    }
    // MenuButtonAction is Nintendo's (east chooses, south goes back); a positional pad has them the other way round
    if (profile && profile->Layout == FaceLayout::Positional)
    {
        if (button == SDL_CONTROLLER_BUTTON_A)
            return MenuAction::Confirm;
        if (button == SDL_CONTROLLER_BUTTON_B)
            return MenuAction::Back;
        // X clears: west on a positional pad, north on a Nintendo one
        if (button == SDL_CONTROLLER_BUTTON_X)
            return MenuAction::Clear;
        if (button == SDL_CONTROLLER_BUTTON_Y)
            return MenuAction::None;
    }
    return action;
}

// ---- the state and the actions

bool Input::PartHeld(const InputPart& part, const DeviceState& state)
{
    return state.Held.count(part) != 0;
}

void Input::SetHeld(DeviceState& state, const InputPart& part, bool held)
{
    if (held)
        state.Held.insert(part);
    else
    {
        state.Held.erase(part);
        state.Suppressed.erase(part);
    }
}

void Input::OnEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
        if (event.key.repeat)
            return;
        SetHeld(KeyboardState, Key(NormalizeKey(event.key.keysym.sym)), event.type == SDL_KEYDOWN);
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    {
        LastPad = event.cbutton.which;
        SetHeld(State(event.cbutton.which), Button((SDL_GameControllerButton)event.cbutton.button), event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
    }
    case SDL_CONTROLLERAXISMOTION:
    {
        DeviceState& state = State(event.caxis.which);
        int axis = event.caxis.axis, value = event.caxis.value;
        if (axis == SDL_CONTROLLER_AXIS_RIGHTX)
            state.RightX = value;
        else if (axis == SDL_CONTROLLER_AXIS_RIGHTY)
            state.RightY = value;
        int threshold = IsTrigger(axis) ? kTriggerThreshold : kStickThreshold;
        SetHeld(state, Axis((SDL_GameControllerAxis)axis, 1), value > threshold);
        SetHeld(state, Axis((SDL_GameControllerAxis)axis, -1), value < -threshold);
        if (std::abs(value) > threshold)
            LastPad = event.caxis.which;
        break;
    }
    default:
        return;
    }
    Update();
}

void Input::ReleaseAll()
{
    KeyboardState = DeviceState();
    PadStates.clear();
    Update();
}

void Input::Update()
{
    auto now = Clock::now();
    std::array<bool, kActionCount> next {};
    std::array<SDL_JoystickID, kActionCount> source;
    source.fill(kKeyboard);

    auto device = [&](SDL_JoystickID id, DeviceState& state)
    {
        const DeviceProfile& profile = ProfileFor(id);
        // the combos first, since one that's on keeps its parts from the single bindings below
        for (int a = 0; a < kActionCount; a++)
        {
            for (int alt = 0; alt < 2; alt++)
            {
                const Binding& b = alt ? profile.Map[a].Alt : profile.Map[a].Primary;
                if (b.Parts.size() < 2)
                    continue;
                bool all = std::all_of(b.Parts.begin(), b.Parts.end(), [&](const InputPart& p) { return PartHeld(p, state); });
                bool on = false;
                if (!all)
                    state.Since[a][alt] = {};
                else if (profile.Keyboard)
                    on = true;
                else
                {
                    if (state.Since[a][alt] == Clock::time_point())
                        state.Since[a][alt] = now;
                    on = now - state.Since[a][alt] >= std::chrono::milliseconds(kComboHoldMs);
                }
                if (on && !state.ComboOn[a][alt])
                {
                    for (const InputPart& p : b.Parts)
                        state.Suppressed.insert(p);
                }
                state.ComboOn[a][alt] = on;
                if (on && !next[a])
                {
                    next[a] = true;
                    source[a] = id;
                }
            }
        }
        for (int a = 0; a < kActionCount; a++)
        {
            for (int alt = 0; alt < 2; alt++)
            {
                const Binding& b = alt ? profile.Map[a].Alt : profile.Map[a].Primary;
                if (b.Parts.size() != 1)
                    continue;
                const InputPart& p = b.Parts[0];
                if (PartHeld(p, state) && !state.Suppressed.count(p) && !next[a])
                {
                    next[a] = true;
                    source[a] = id;
                }
            }
        }
    };
    device(kKeyboard, KeyboardState);
    for (auto& [id, state] : PadStates)
        device(id, state);

    // the pause menu can always be reached: Esc on a keyboard, Guide on a pad, when nothing is bound to it there
    {
        const DeviceProfile& keyboard = *Profile("keyboard");
        if (keyboard.Map[(int)Action::Pause].Primary.Empty() && keyboard.Map[(int)Action::Pause].Alt.Empty()
            && PartHeld(Key(SDLK_ESCAPE), KeyboardState))
            next[(int)Action::Pause] = true;
    }

    std::vector<std::pair<Action, bool>> edges;
    std::vector<SDL_JoystickID> sources;
    melonDS::u32 mask = 0;
    for (int a = 0; a < kActionCount; a++)
    {
        if (a < kDsButtonCount && next[a])
            mask |= 1u << a;
        if (next[a] != Active[a])
        {
            if (IsHeldAction((Action)a) || next[a])
            {
                edges.emplace_back((Action)a, next[a]);
                sources.push_back(source[a]);
            }
        }
    }
    Active = next;
    Mask = mask;
    // after the state is stored, since an action may change it (a menu opening releases everything)
    for (size_t i = 0; i < edges.size(); i++)
    {
        if (OnAction)
            OnAction(edges[i].first, edges[i].second, sources[i]);
    }
}

void Input::Tick()
{
    auto now = Clock::now();
    float dt = std::min(std::chrono::duration<float>(now - LastTick).count(), 0.1f);
    LastTick = now;
    if (CaptureOn && now >= CaptureUntil)
        FinishCapture(CaptureResult::TimedOut);
    // combos held long enough
    Update();

    // the touch cursor: the right stick of the pad pushed furthest, unless its profile binds the right stick to something
    float bestX = 0, bestY = 0, best = 0;
    for (auto& [id, state] : PadStates)
    {
        const DeviceProfile& profile = ProfileFor(id);
        bool bound = false;
        for (const ActionBinding& pair : profile.Map)
        {
            for (const Binding* b : {&pair.Primary, &pair.Alt})
            {
                for (const InputPart& p : b->Parts)
                    bound |= p.Type == InputPart::Kind::Axis && (p.Code == SDL_CONTROLLER_AXIS_RIGHTX || p.Code == SDL_CONTROLLER_AXIS_RIGHTY);
            }
        }
        if (bound)
            continue;
        float x = state.RightX / 32767.f, y = state.RightY / 32767.f;
        float m = std::hypot(x, y);
        if (m > best)
        {
            best = m;
            bestX = x;
            bestY = y;
        }
    }
    if (best > kCursorDeadZone)
    {
        // slow near the middle for aiming, fast at the edge
        float s = std::min((best - kCursorDeadZone) / (1 - kCursorDeadZone), 1.f);
        float speed = kCursorSpeed * s * s;
        CursorX = std::clamp(CursorX + bestX / best * speed * dt, 0.f, 255.f);
        CursorY = std::clamp(CursorY + bestY / best * speed * dt, 0.f, 191.f);
        CursorShownUntil = now + std::chrono::milliseconds(kCursorShowMs);
    }
    if (Active[(int)Action::TouchTap])
        CursorShownUntil = now + std::chrono::milliseconds(kCursorShowMs);
}

Input::Cursor Input::GetCursor() const
{
    Cursor cursor;
    cursor.X = CursorX;
    cursor.Y = CursorY;
    cursor.Pressed = Active[(int)Action::TouchTap];
    cursor.Visible = Clock::now() < CursorShownUntil || cursor.Pressed;
    return cursor;
}

void Input::TypeKeyHotkeys(SDL_Keycode key, Uint16 mod)
{
    std::set<InputPart> held {Key(NormalizeKey(key))};
    if (mod & KMOD_CTRL) held.insert(Key(SDLK_LCTRL));
    if (mod & KMOD_ALT) held.insert(Key(SDLK_LALT));
    if (mod & KMOD_LSHIFT) held.insert(Key(SDLK_LSHIFT));
    if (mod & KMOD_RSHIFT) held.insert(Key(SDLK_RSHIFT));
    const DeviceProfile& keyboard = *Profile("keyboard");
    for (int a = kDsButtonCount; a < kActionCount; a++)
    {
        for (const Binding* b : {&keyboard.Map[a].Primary, &keyboard.Map[a].Alt})
        {
            // exactly these keys: F9 doesn't also fire a Ctrl + F9
            if (b->Empty() || b->Parts.size() != held.size()
                || !std::all_of(b->Parts.begin(), b->Parts.end(), [&](const InputPart& p) { return held.count(p) != 0; }))
                continue;
            if (OnAction)
            {
                OnAction((Action)a, true, kKeyboard);
                if (IsHeldAction((Action)a))
                    OnAction((Action)a, false, kKeyboard);
            }
            break;
        }
    }
}

// ---- press a button to bind

void Input::BeginCapture(const std::string& id, Action action, bool alt)
{
    CaptureOn = true;
    CaptureDevice = id;
    CaptureAction = action;
    CaptureAlt = alt;
    CaptureUntil = Clock::now() + std::chrono::milliseconds(kCaptureTimeoutMs);
    CaptureParts.clear();
    CaptureHeld.clear();
    LastCapture = CaptureResult::None;
    printf("input: press something for %s (%s)\n", ActionKey(action), alt ? "alt" : "primary");
    fflush(stdout);
}

void Input::CancelCapture()
{
    if (CaptureOn)
        FinishCapture(CaptureResult::Cancelled);
}

void Input::FinishCapture(CaptureResult result)
{
    CaptureOn = false;
    LastCapture = result;
    if (result == CaptureResult::Bound)
        SetBinding(CaptureDevice, CaptureAction, CaptureAlt, Binding {CaptureParts});
    else
        printf("input: capture %s\n", result == CaptureResult::TimedOut ? "timed out" : "cancelled");
    fflush(stdout);
}

Input::CaptureResult Input::TakeCaptureResult()
{
    CaptureResult result = LastCapture;
    LastCapture = CaptureResult::None;
    return result;
}

float Input::CaptureSecondsLeft() const
{
    return CaptureOn ? std::max(0.f, std::chrono::duration<float>(CaptureUntil - Clock::now()).count()) : 0.f;
}

std::string Input::CaptureText()
{
    DeviceProfile* profile = Profile(CaptureDevice);
    return profile ? BindingLabel(Binding {CaptureParts}, *profile) : "";
}

bool Input::CaptureEvent(const SDL_Event& event)
{
    if (!CaptureOn)
        return false;
    std::string device;
    InputPart part;
    bool down = false, up = false;
    switch (event.type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (event.key.repeat)
            return true;
        device = "keyboard";
        part = Key(NormalizeKey(event.key.keysym.sym));
        down = event.type == SDL_KEYDOWN;
        up = !down;
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    {
        const Pad* pad = FindPad(event.cbutton.which);
        device = pad ? pad->Guid : "script";
        part = Button((SDL_GameControllerButton)event.cbutton.button);
        down = event.type == SDL_CONTROLLERBUTTONDOWN;
        up = !down;
        break;
    }
    case SDL_CONTROLLERAXISMOTION:
    {
        const Pad* pad = FindPad(event.caxis.which);
        device = pad ? pad->Guid : "script";
        int value = event.caxis.value;
        InputPart pos = Axis((SDL_GameControllerAxis)event.caxis.axis, 1), neg = Axis((SDL_GameControllerAxis)event.caxis.axis, -1);
        if (device != CaptureDevice)
            return true;
        if (value > kCaptureAxisDown) { part = pos; down = true; }
        else if (value < -kCaptureAxisDown) { part = neg; down = true; }
        else if (std::abs(value) < kCaptureAxisUp)
        {
            // back in the middle: whichever way it was pushed is let go
            part = CaptureHeld.count(pos) ? pos : neg;
            up = CaptureHeld.count(part) != 0;
        }
        if (!down && !up)
            return true;
        break;
    }
    default:
        return false;
    }

    if (device != CaptureDevice)
    {
        // the other kind of device can cancel: Esc on the keyboard, B (back) on a pad
        bool cancel = (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            || (event.type == SDL_CONTROLLERBUTTONDOWN && MenuButton(event.cbutton.which, event.cbutton.button) == MenuAction::Back);
        if (cancel)
            FinishCapture(CaptureResult::Cancelled);
        return true;
    }
    if (down)
    {
        if (!CaptureHeld.count(part))
        {
            CaptureHeld.insert(part);
            if (CaptureParts.size() < 3 && std::find(CaptureParts.begin(), CaptureParts.end(), part) == CaptureParts.end())
                CaptureParts.push_back(part);
        }
    }
    else if (up && CaptureHeld.count(part))
    {
        // the first one let go ends it, with everything that was held together (a release of what was held before the capture began, the
        // press that chose the row, is ignored)
        FinishCapture(CaptureResult::Bound);
    }
    return true;
}

}
