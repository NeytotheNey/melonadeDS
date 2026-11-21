#include "Ui.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include "ArtImage.h"
#include "Icons.h"
#include "ImageFile.h"
#include "Input.h"
#include "Resources.h"
#include "Screens.h"
#include "Settings.h"
#include "Widgets.h"

using Frontend::MenuAction;

namespace Ui
{

namespace
{

// the fonts are in the exe (resources.rc), so the shipped folder stays the exe and SDL2.dll
bool LoadResource(int id, std::vector<unsigned char>& out)
{
    HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    HGLOBAL handle = info ? ::LoadResource(nullptr, info) : nullptr;
    const void* data = handle ? LockResource(handle) : nullptr;
    DWORD size = info ? SizeofResource(nullptr, info) : 0;
    if (!data || size == 0)
        return false;
    out.assign((const unsigned char*)data, (const unsigned char*)data + size);
    return true;
}

ImFont* AddFont(std::vector<unsigned char>& data, const char* name)
{
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    snprintf(config.Name, sizeof(config.Name), "%s", name);
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data.data(), (int)data.size(), 18.0f, &config);
}

// Windows' own fonts for what Roboto lacks: Japanese (Yu Gothic, or MS Gothic on an older Windows), Korean (Malgun Gothic) and Chinese
// (Microsoft YaHei), for game titles from their banners. Merged into the font added last, and only drawn from when a glyph is needed
// (ImGui loads glyphs as they're used); the files are mapped, not read, so only the pages used are loaded. Not shipped, so not in
// third_party: a Windows without them shows those titles as boxes, as before
void AddFallbacks(const std::vector<const wchar_t*>& files, std::vector<std::pair<HANDLE, const void*>>& mapped)
{
    wchar_t windows[MAX_PATH];
    if (!GetWindowsDirectoryW(windows, MAX_PATH))
        return;
    for (const wchar_t* file : files)
    {
        std::wstring path = std::wstring(windows) + L"\\Fonts\\" + file;
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        LARGE_INTEGER size {};
        GetFileSizeEx(handle, &size);
        HANDLE mapping = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        CloseHandle(handle);
        const void* view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (!view)
        {
            if (mapping)
                CloseHandle(mapping);
            continue;
        }
        mapped.emplace_back(mapping, view);
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = true;
        snprintf(config.Name, sizeof(config.Name), "%ls", file);
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<void*>(view), (int)size.QuadPart, 18.0f, &config);
    }
}

size_t Utf8Length(const std::string& text)
{
    size_t count = 0;
    for (unsigned char c : text)
        count += (c & 0xC0) != 0x80;
    return count;
}

// the on-screen keyboard's keys, a row of ten each, and the shifted ones
const char* const kKeyRows[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm,.'"};
const char* const kShiftRows[4] = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL_", "ZXCVBNM;:\""};

}

const char* IconText(ImWchar icon)
{
    // a few at a time, so one expression can use more than one
    static char buffers[8][5];
    static int next = 0;
    char* out = buffers[next];
    next = (next + 1) % 8;
    unsigned c = icon;
    if (c < 0x80) { out[0] = (char)c; out[1] = 0; }
    else if (c < 0x800) { out[0] = (char)(0xC0 | (c >> 6)); out[1] = (char)(0x80 | (c & 0x3F)); out[2] = 0; }
    else { out[0] = (char)(0xE0 | (c >> 12)); out[1] = (char)(0x80 | ((c >> 6) & 0x3F)); out[2] = (char)(0x80 | (c & 0x3F)); out[3] = 0; }
    return out;
}

bool App::Init(SDL_Window* window, SDL_GLContext context, Host& host, bool hidden)
{
    HostPtr = &host;
    Window = window;
    Hidden = hidden;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // a TV: the focus is always shown, and Esc (which the frontend takes for Back anyway) never clears it
    io.ConfigNavCursorVisibleAlways = true;
    io.ConfigNavEscapeClearFocusItem = false;
    if (!ImGui_ImplSDL2_InitForOpenGL(window, context))
        return false;
    // the controllers' buttons come through Feed*() as the fixed layout (Controls.cpp) says, not the backend's Xbox layout
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
    // the context is GLES 3.2 (pc_gl_loader), so the GLES 3 shaders
    if (!ImGui_ImplOpenGL3_Init("#version 300 es"))
        return false;

    if (!LoadResource(IDR_FONT_REGULAR, FontData[0]) || !LoadResource(IDR_FONT_MEDIUM, FontData[1]) || !LoadResource(IDR_FONT_ICONS, FontData[2]))
    {
        fprintf(stderr, "ui: the fonts aren't in the exe\n");
        return false;
    }
    Regular = AddFont(FontData[0], "Roboto Regular");
    AddFallbacks({L"YuGothR.ttc", L"msgothic.ttc", L"malgun.ttf", L"msyh.ttc"}, MappedFonts);
    Medium = AddFont(FontData[1], "Roboto Medium");
    AddFallbacks({L"YuGothM.ttc", L"msgothic.ttc", L"malgun.ttf", L"msyh.ttc"}, MappedFonts);
    Icons = AddFont(FontData[2], "Material Symbols Outlined");
    if (!Regular || !Medium || !Icons)
        return false;
    io.FontDefault = Regular;
    ApplyTheme();
    Initialized = true;
    return true;
}

void App::Shutdown()
{
    if (!Initialized)
        return;
    for (auto& [path, texture] : Textures)
        glDeleteTextures(1, &texture.Id);
    Textures.clear();
    for (auto& [key, texture] : IconTextures)
        glDeleteTextures(1, &texture.Id);
    IconTextures.clear();
    StopArtWorker();
    for (auto& [key, texture] : ArtTextures)
    {
        if (texture.Id)
            glDeleteTextures(1, &texture.Id);
    }
    ArtTextures.clear();
    Stack.clear();
    Closed.clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    for (auto& [mapping, view] : MappedFonts)
    {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
    }
    MappedFonts.clear();
    Initialized = false;
}

void App::ApplyTheme()
{
    std::string theme = HostPtr->Settings ? HostPtr->Settings->GetString("desktop_theme", "system") : "system";
    Colors_ = IsDarkTheme(theme) ? &DarkPalette() : &LightPalette();
    StyleDirty = true;
}

bool App::Active() const
{
    return !Stack.empty() || !Toasts.empty() || KeyboardTarget != nullptr;
}

std::string App::TopId() const
{
    return Stack.empty() ? "" : Stack.back()->Id();
}

void App::ApplyScale(int w, int h)
{
    if (!Hidden)
    {
        float dpi = 0;
        int display = SDL_GetWindowDisplayIndex(Window);
        DpiScale = (display >= 0 && SDL_GetDisplayDPI(display, &dpi, nullptr, nullptr) == 0 && dpi > 0) ? dpi / 96.f : 1.f;
    }
    // made for 1280 x 720 and scaled with the window (a 1080p TV is 1.5x, a 4K one 3x), never below the monitor's own scale, or 3/4 of it
    // in a short window (the detached top screen's 2x is 384 high), and small enough that the pause menu's panel (400 wide) fits a
    // narrow window
    float s = std::min(w / 1280.f, h / 720.f);
    s = std::max(s, DpiScale * std::clamp(h / (720.f * DpiScale), 0.75f, 1.f));
    s = std::min(s, w / 440.f);
    s = std::max(s, 0.5f);
    s = std::round(s * 20.f) / 20.f;
    if (s != Scale || StyleDirty)
    {
        Scale = s;
        ApplyStyle(*Colors_, s);
        StyleDirty = false;
    }
}

void App::BuildFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    // the controller's input is fed in by Feed*(), whether or not the backend sees a pad
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    int w = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x), h = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    ApplyScale(std::max(w, 1), std::max(h, 1));
    FrameNumber++;
    UploadArt();
    ImGui::NewFrame();

    auto now = std::chrono::steady_clock::now();
    Toasts.erase(std::remove_if(Toasts.begin(), Toasts.end(), [&](const ToastItem& t) { return t.Until <= now; }), Toasts.end());

    if (Screen* top = Top())
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* background = ImGui::GetBackgroundDrawList();
        // the game's last frame dimmed behind an overlay, the surface behind the others
        ImVec2 end(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
        background->AddRectFilled(viewport->Pos, end, top->Overlay() ? WithAlpha(Colors_->Scrim, 0.6f) : Colors_->Surface);
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        if (top->WantFocus && !KeyboardOpen())
            ImGui::SetNextWindowFocus();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoScrollWithMouse;
        // the keyboard over the screen takes its input
        if (KeyboardOpen())
            flags |= ImGuiWindowFlags_NoInputs;
        bool wantedFocus = top->WantFocus;
        ImGui::Begin(top->Id(), nullptr, flags);
        Drawing = top;
        top->Draw(*this);
        Drawing = nullptr;
        ImGui::End();
        // the item to focus is gone (or there's no default): the default next frame, then nothing
        if (wantedFocus && top->WantFocus && Top() == top)
        {
            if (top->FocusId)
                top->FocusId = 0;
            else
                top->WantFocus = false;
        }
    }
    if (KeyboardOpen())
        DrawKeyboard();
    DrawToasts();
    ImGui::Render();
    Closed.clear();
}

void App::Render()
{
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void App::TrackFocus(bool isDefault)
{
    // the screen being drawn, which isn't the top one any more once it opened another during its Draw
    Screen* s = Drawing;
    if (!s || KeyboardOpen())
        return;
    ImGuiID id = ImGui::GetItemID();
    bool disabled = (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    if (s->WantFocus && !disabled && (s->FocusId ? id == s->FocusId : isDefault))
    {
        ImGuiContext& g = *GImGui;
        ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
        // and as the window's first item, since its appearing would otherwise focus its first one at the end of the frame
        ImGui::SetItemDefaultFocus();
        g.NavCursorVisible = true;
        g.NavHighlightItemUnderNav = true;
        ImGui::ScrollToItem();
        s->WantFocus = false;
        s->RefocusId = id;
        s->RefocusFrames = 1;
    }
    else if (s->RefocusFrames > 0 && id == s->RefocusId)
    {
        // held a few frames more, against ImGui's own focus of a new window's first item
        s->RefocusFrames--;
        if (!ImGui::IsItemFocused() && !ImGui::IsAnyItemActive())
        {
            ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
            GImGui->NavCursorVisible = true;
        }
    }
    // (not a disabled item: one under a question keeps the focus it had, and would take it back from the question; nor while a focus is
    // wanted, whose target an item drawn before it would take the place of)
    if (!disabled && ImGui::IsItemFocused() && !s->WantFocus)
        s->FocusId = id;
}

void App::Push(std::unique_ptr<Screen> screen)
{
    if (Stack.empty())
    {
        // what the menus didn't see while they were closed
        ImGuiIO& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        ResetMouse();
        SwallowKeys.clear();
        SwallowButtons.clear();
        SwallowStick = false;
    }
    screen->WantFocus = true;
    Stack.push_back(std::move(screen));
    printf("ui: %s\n", Stack.back()->Id());
    fflush(stdout);
}

void App::Pop()
{
    if (Stack.empty())
        return;
    Closed.push_back(std::move(Stack.back()));
    Stack.pop_back();
    if (Stack.empty())
    {
        KeyboardTarget = nullptr;
        SwallowHeld();
        ReleaseMouse();
        printf("ui: closed\n");
    }
    else
    {
        Stack.back()->WantFocus = true;
        printf("ui: %s\n", Stack.back()->Id());
    }
    fflush(stdout);
}

void App::CloseAll()
{
    if (Stack.empty())
        return;
    while (!Stack.empty())
    {
        Closed.push_back(std::move(Stack.back()));
        Stack.pop_back();
    }
    KeyboardTarget = nullptr;
    SwallowHeld();
    ReleaseMouse();
    printf("ui: closed\n");
    fflush(stdout);
}

void App::ReleaseMouse()
{
    // the backend captures the mouse while a button is down in the menus; closed, they get no more events, so a capture left over
    // would hold the mouse
    SDL_CaptureMouse(SDL_FALSE);
}

void App::ResetMouse()
{
    // a button the menus saw go down but not up (they closed meanwhile) is let go in the backend, whose own count would otherwise
    // keep capturing the mouse, and in ImGui, which would otherwise see a held button or a drag when they open again
    if (ImGui::IsAnyMouseDown())
        printf("ui: a mouse button held since the menus closed let go\n");
    for (Uint8 button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; button++)
    {
        SDL_Event up {};
        up.type = SDL_MOUSEBUTTONUP;
        up.button.windowID = SDL_GetWindowID(Window);
        up.button.button = button;
        up.button.state = SDL_RELEASED;
        ImGui_ImplSDL2_ProcessEvent(&up);
    }
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputMouse();
}

void App::ShowStart(LibraryOpen open)
{
    while (!Stack.empty())
    {
        Closed.push_back(std::move(Stack.back()));
        Stack.pop_back();
    }
    KeyboardTarget = nullptr;
    Push(MakeLibraryScreen(open));
}

void App::OpenPause()
{
    Push(MakePauseScreen());
}

bool App::OpenPath(const std::string& path)
{
    bool game = HostPtr->GameRunning && HostPtr->GameRunning();
    if (path == "start" || path == "library" || path == "library/details" || path == "library/settings")
    {
        // the home screen, which a running game would have to stop for
        if (game)
            return false;
        ShowStart(path == "library/details" ? LibraryOpen::Details : path == "library/settings" ? LibraryOpen::Settings : LibraryOpen::None);
    }
    else if (path == "pause" || path == "pause/save" || path == "pause/load")
    {
        if (!game)
            return false;
        if (Stack.empty())
            OpenPause();
        if (path != "pause")
            Push(MakeStatesScreen(path == "pause/save"));
    }
    else if (path == "settings" || path.rfind("settings/", 0) == 0 || path == "input")
    {
        std::string tab = path.size() > 9 && path != "input" ? path.substr(9) : "";
        if (tab != "" && tab != "video" && tab != "display" && tab != "emulation" && tab != "audio" && tab != "controls" && tab != "library")
            return false;
        if (game && Stack.empty())
            OpenPause();
        Push(MakeSettingsScreen(path == "input" ? "controls" : tab));
        if (path == "input")
            Push(MakeInputScreen());
    }
    else if (path == "keyboard")
        Push(MakeKeyboardTestScreen());
    else
        return false;
    return true;
}

void App::Toast(const std::string& text)
{
    Toasts.push_back({text, std::chrono::steady_clock::now() + std::chrono::milliseconds(2500)});
    if (Toasts.size() > 3)
        Toasts.erase(Toasts.begin());
    printf("ui: toast \"%s\"\n", text.c_str());
    fflush(stdout);
}

void App::DrawToasts()
{
    if (Toasts.empty())
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float y = viewport->Pos.y + viewport->Size.y - Px(88);
    for (auto it = Toasts.rbegin(); it != Toasts.rend(); ++it)
    {
        float size = Px(16);
        ImVec2 t = Regular->CalcTextSizeA(size, FLT_MAX, 0, it->Text.c_str());
        float padX = Px(24), h = Px(48);
        ImVec2 min(viewport->Pos.x + (viewport->Size.x - t.x) / 2 - padX, y - h), max(min.x + t.x + 2 * padX, y);
        draw->AddRectFilled(min, max, Colors_->InverseSurface, Px(8));
        draw->AddText(Regular, size, ImVec2(IM_ROUND(min.x + padX), IM_ROUND(min.y + (h - t.y) / 2)), Colors_->InverseOnSurface, it->Text.c_str());
        y -= h + Px(8);
    }
}

void App::SwallowHeld()
{
    SwallowKeys = HeldKeys;
    SwallowButtons = HeldButtons;
    SwallowStick = std::abs(StickX) > Frontend::kStickDeadZone || std::abs(StickY) > Frontend::kStickDeadZone;
}

void App::Back()
{
    if (Busy)
        return;
    if (KeyboardOpen())
    {
        KeyboardTarget = nullptr;
        return;
    }
    Screen* top = Top();
    if (!top || top->Back(*this))
        return;
    Pop();
}

void App::Menu()
{
    // Start or Guide in the menus: back to the game, or with none the screen's own (the library's menu)
    if (Busy || KeyboardOpen())
        return;
    if (HostPtr->GameRunning && HostPtr->GameRunning())
        CloseAll();
    else if (Screen* top = Top())
        top->MenuButton(*this);
}

void App::SendPadKey(ImGuiKey key)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
}

void App::Inject(MenuAction action)
{
    LastInputPad = true;
    if (!IsOpen() && !KeyboardOpen())
    {
        if (action == MenuAction::Menu && HostPtr->GameRunning && HostPtr->GameRunning())
            OpenPause();
        return;
    }
    if (Busy && action != MenuAction::Menu)
        return;
    switch (action)
    {
    case MenuAction::Up: SendPadKey(ImGuiKey_GamepadDpadUp); break;
    case MenuAction::Down: SendPadKey(ImGuiKey_GamepadDpadDown); break;
    case MenuAction::Left: SendPadKey(ImGuiKey_GamepadDpadLeft); break;
    case MenuAction::Right: SendPadKey(ImGuiKey_GamepadDpadRight); break;
    case MenuAction::Confirm: SendPadKey(ImGuiKey_GamepadFaceDown); break;
    case MenuAction::Back: Back(); break;
    case MenuAction::TabPrev: if (Top() && !KeyboardOpen()) Top()->Tab(*this, -1); break;
    case MenuAction::TabNext: if (Top() && !KeyboardOpen()) Top()->Tab(*this, 1); break;
    case MenuAction::Clear: if (Top() && !KeyboardOpen()) Top()->Clear(*this); break;
    case MenuAction::Menu: Menu(); break;
    default: break;
    }
}

void App::InjectText(const std::string& text)
{
    if (KeyboardOpen())
        KeyboardType(text.c_str());
    else
        fprintf(stderr, "ui: text \"%s\": no text field is being edited\n", text.c_str());
}

void App::FeedPadButton(const SDL_ControllerButtonEvent& event)
{
    bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
    ImGuiIO& io = ImGui::GetIO();
    // A and B by the pad's face layout
    MenuAction action = HostPtr->Input ? HostPtr->Input->MenuButton(event.which, event.button) : Frontend::MenuButtonAction(event.button);
    // a press while busy is dropped; its release still goes through so nothing stays held
    if (Busy && down && action != MenuAction::None)
        return;
    switch (action)
    {
    case MenuAction::Up: io.AddKeyEvent(ImGuiKey_GamepadDpadUp, down); break;
    case MenuAction::Down: io.AddKeyEvent(ImGuiKey_GamepadDpadDown, down); break;
    case MenuAction::Left: io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, down); break;
    case MenuAction::Right: io.AddKeyEvent(ImGuiKey_GamepadDpadRight, down); break;
    // ImGui's "activate" is its south button; the fixed layout's A is the east one
    case MenuAction::Confirm: io.AddKeyEvent(ImGuiKey_GamepadFaceDown, down); break;
    case MenuAction::Back: if (down) Back(); break;
    case MenuAction::TabPrev: if (down && Top() && !KeyboardOpen()) Top()->Tab(*this, -1); break;
    case MenuAction::TabNext: if (down && Top() && !KeyboardOpen()) Top()->Tab(*this, 1); break;
    case MenuAction::Clear: if (down && Top() && !KeyboardOpen()) Top()->Clear(*this); break;
    case MenuAction::Menu: if (down) Menu(); break;
    default: break;
    }
}

void App::FeedAxis(const SDL_ControllerAxisEvent& event)
{
    ImGuiIO& io = ImGui::GetIO();
    const int dead = Frontend::kStickDeadZone;
    float v = event.value / 32767.f;
    if (event.axis == SDL_CONTROLLER_AXIS_LEFTX)
    {
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, event.value < -dead, std::max(0.f, -v));
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, event.value > dead, std::max(0.f, v));
    }
    else if (event.axis == SDL_CONTROLLER_AXIS_LEFTY)
    {
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, event.value < -dead, std::max(0.f, -v));
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, event.value > dead, std::max(0.f, v));
    }
}

bool App::ProcessEvent(const SDL_Event& event)
{
    if (!Initialized)
        return false;
    Uint32 mainId = SDL_GetWindowID(Window);
    bool open = IsOpen() || KeyboardOpen();
    switch (event.type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
        SDL_Keycode key = event.key.keysym.sym;
        bool down = event.type == SDL_KEYDOWN;
        if (down)
            HeldKeys.insert(key);
        else
            HeldKeys.erase(key);
        if (!open)
        {
            if (SwallowKeys.count(key))
            {
                if (!down)
                    SwallowKeys.erase(key);
                return true;
            }
            // the game's (the pause menu is the input mapping's Pause, Esc by default)
            return false;
        }
        // the hotkeys keep working in the menus
        if (key >= SDLK_F1 && key <= SDLK_F12)
            return false;
        LastInputPad = false;
        SDL_Event copy = event;
        // as typed in the main window, from whichever window has the focus
        copy.key.windowID = mainId;
        if (KeyboardOpen())
        {
            if (down && (key == SDLK_BACKSPACE))
                KeyboardBackspace();
            else if (down && key == SDLK_v && (event.key.keysym.mod & KMOD_CTRL))
            {
                // pasted (a key copied from the RetroAchievements site), one line of it
                if (char* clip = SDL_GetClipboardText())
                {
                    std::string text = clip;
                    SDL_free(clip);
                    text = text.substr(0, text.find_first_of("\r\n"));
                    KeyboardType(text.c_str());
                }
            }
            else if (down && !event.key.repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE))
                KeyboardTarget = nullptr;
            else if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT)
                ImGui_ImplSDL2_ProcessEvent(&copy);
            return true;
        }
        MenuAction action = Frontend::MenuKeyAction(key);
        if (action != MenuAction::None)
        {
            // a held key repeats: Back, Clear and the tabs only take its first press, so holding Esc doesn't close one menu after another
            if (down && !event.key.repeat && !Busy)
            {
                if (action == MenuAction::Back)
                    Back();
                else if (action == MenuAction::Clear)
                {
                    if (Top())
                        Top()->Clear(*this);
                }
                else if (Top() && (action == MenuAction::TabPrev || action == MenuAction::TabNext))
                    Top()->Tab(*this, action == MenuAction::TabNext ? 1 : -1);
            }
            return true;
        }
        if (Busy && down)
            return true;
        // the rest (arrows, Enter, Space, Tab) is ImGui's keyboard navigation
        ImGui_ImplSDL2_ProcessEvent(&copy);
        return true;
    }
    case SDL_TEXTINPUT:
        if (KeyboardOpen())
        {
            KeyboardType(event.text.text);
            return true;
        }
        return open;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    {
        int button = event.cbutton.button;
        bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
        if (down)
            HeldButtons.insert(button);
        else
            HeldButtons.erase(button);
        if (!open)
        {
            if (SwallowButtons.count(button))
            {
                if (!down)
                    SwallowButtons.erase(button);
                return true;
            }
            // the game's (the pause menu is the input mapping's Pause, Guide or Select + Start by default)
            return false;
        }
        LastInputPad = true;
        FeedPadButton(event.cbutton);
        return true;
    }
    case SDL_CONTROLLERAXISMOTION:
    {
        const int dead = Frontend::kStickDeadZone;
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            StickX = event.caxis.value;
        else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            StickY = event.caxis.value;
        if (!open)
        {
            if (SwallowStick)
            {
                if (std::abs(StickX) > dead || std::abs(StickY) > dead)
                    return true;
                // back in the middle: the game gets this one, which releases its D-pad
                SwallowStick = false;
            }
            return false;
        }
        if (std::abs(event.caxis.value) > dead)
            LastInputPad = true;
        FeedAxis(event.caxis);
        return true;
    }
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    {
        if (!open)
            return false;
        Uint32 windowId = event.type == SDL_MOUSEMOTION ? event.motion.windowID
                        : event.type == SDL_MOUSEWHEEL ? event.wheel.windowID : event.button.windowID;
        // the menus are only in the main window; the other one takes no touch while they're open
        if (windowId != mainId)
            return true;
        if (event.type == SDL_MOUSEBUTTONDOWN)
            LastInputPad = false;
        ImGui_ImplSDL2_ProcessEvent(&event);
        return true;
    }
    case SDL_WINDOWEVENT:
        ImGui_ImplSDL2_ProcessEvent(&event);
        return false;
    default:
        return false;
    }
}

void App::OpenKeyboard(std::string* target, const std::string& title, size_t maxLength, bool masked)
{
    KeyboardTarget = target;
    KeyboardMasked = masked;
    KeyboardTitle = title;
    KeyboardMax = maxLength;
    KeyboardShift = false;
    KeyboardFocus = true;
    printf("ui: keyboard\n");
    fflush(stdout);
}

void App::KeyboardType(const char* utf8)
{
    if (!KeyboardTarget)
        return;
    for (const char* p = utf8; *p;)
    {
        // one UTF-8 character at a time, up to the length
        int len = ((unsigned char)*p < 0x80) ? 1 : (((unsigned char)*p >> 5) == 6) ? 2 : (((unsigned char)*p >> 4) == 14) ? 3 : 4;
        std::string c(p, strnlen(p, len));
        p += c.size();
        if ((unsigned char)c[0] < 0x20)
            continue;
        if (Utf8Length(*KeyboardTarget) >= KeyboardMax)
            break;
        *KeyboardTarget += c;
    }
}

void App::KeyboardBackspace()
{
    if (!KeyboardTarget || KeyboardTarget->empty())
        return;
    std::string& text = *KeyboardTarget;
    // a whole UTF-8 character: its continuation bytes, then its first one
    while (!text.empty() && ((unsigned char)text.back() & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

void App::DrawKeyboard()
{
    const Palette& p = *Colors_;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 vmin = viewport->Pos, vsize = viewport->Size;
    // dims what's behind, then a sheet along the bottom
    ImGui::GetBackgroundDrawList(); // (the screen behind is already drawn)
    ImGui::SetNextWindowPos(vmin);
    ImGui::SetNextWindowSize(vsize);
    if (KeyboardFocus)
        ImGui::SetNextWindowFocus();
    ImGui::Begin("##keyboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(vmin, ImVec2(vmin.x + vsize.x, vmin.y + vsize.y), WithAlpha(p.Scrim, 0.45f));

    float gap = Px(8);
    float sheetW = std::min(vsize.x, Px(880));
    float keyW = std::floor((sheetW - Px(48) - gap * 9) / 10), keyH = std::min(keyW, Px(60));
    float sheetH = Px(24) + Px(28) + Px(16) + Px(56) + Px(20) + 5 * keyH + 4 * gap + Px(24) + FooterHeight(*this);
    ImVec2 s0(vmin.x + (vsize.x - sheetW) / 2, vmin.y + vsize.y - sheetH), s1(s0.x + sheetW, vmin.y + vsize.y);
    draw->AddRectFilled(s0, s1, p.SurfaceContainer, Px(28), ImDrawFlags_RoundTop);

    float x0 = s0.x + Px(24);
    float y = s0.y + Px(24);
    TextAt(*this, Medium, 20, ImVec2(x0, y), p.OnSurface, KeyboardTitle.c_str());
    y += Px(28) + Px(16);
    // the text so far, with a caret
    float fieldW = sheetW - Px(48);
    ImVec2 f0(x0, y), f1(x0 + fieldW, y + Px(56));
    draw->AddRectFilled(f0, f1, p.SurfaceContainerHighest, Px(12));
    draw->AddRect(f0, f1, p.Primary, Px(12), std::max(1.f, Px(2)));
    std::string shown = KeyboardTarget ? *KeyboardTarget : "";
    if (KeyboardMasked)
    {
        // a dot for each character (keys are ASCII)
        std::string dots;
        for (size_t i = 0; i < shown.size(); i++)
            dots += "\xe2\x80\xa2";
        shown = dots;
    }
    ImVec2 ts = TextSize(*this, Regular, 22, shown.empty() ? "Ag" : shown.c_str());
    float tx = f0.x + Px(16), ty = f0.y + (Px(56) - ts.y) / 2;
    ImVec2 drawn = TextAt(*this, Regular, 22, ImVec2(tx, ty), p.OnSurface, shown.c_str());
    if (std::fmod(ImGui::GetTime(), 1.0) < 0.6 || Hidden)
        draw->AddRectFilled(ImVec2(tx + (shown.empty() ? 0 : drawn.x) + Px(1), ty), ImVec2(tx + (shown.empty() ? 0 : drawn.x) + Px(3), ty + ts.y), p.Primary);
    y += Px(56) + Px(20);

    bool focusSet = !KeyboardFocus;
    auto key = [&](const char* id, const char* label, ImWchar icon, float width, bool accent, bool isDefault) -> bool
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, ImVec2(width, keyH), ImGuiButtonFlags_EnableNav);
        if (!focusSet && isDefault)
        {
            ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
            ImGui::SetItemDefaultFocus();
            GImGui->NavCursorVisible = true;
            focusSet = true;
        }
        bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImU32 base = accent ? p.PrimaryContainer : p.SurfaceContainerHighest;
        ImU32 on = accent ? p.OnPrimaryContainer : p.OnSurface;
        if (focused)
        {
            base = p.SecondaryContainer;
            on = p.OnSecondaryContainer;
        }
        ImU32 bg = held ? Blend(base, on, 0.12f) : hovered ? Blend(base, on, 0.08f) : base;
        ImVec2 max(pos.x + width, pos.y + keyH);
        draw->AddRectFilled(pos, max, bg, Px(12));
        if (focused)
            FocusRing(*this, draw, pos, max, Px(12));
        ImVec2 c(pos.x + width / 2, pos.y + keyH / 2);
        if (icon)
            IconAt(*this, icon, 26, label ? ImVec2(pos.x + Px(28), c.y) : c, on);
        if (label)
        {
            ImVec2 ls = TextSize(*this, Medium, 22, label);
            float lx = icon ? pos.x + Px(48) : c.x - ls.x / 2;
            TextAt(*this, Medium, 22, ImVec2(lx, c.y - ls.y / 2), on, label);
        }
        return pressed;
    };

    for (int row = 0; row < 4; row++)
    {
        ImGui::SetCursorScreenPos(ImVec2(x0, y + row * (keyH + gap)));
        const char* keys = KeyboardShift ? kShiftRows[row] : kKeyRows[row];
        for (int i = 0; i < 10; i++)
        {
            if (i)
                ImGui::SameLine(0, gap);
            char label[2] = {keys[i], 0};
            char id[16];
            snprintf(id, sizeof(id), "##k%d_%d", row, i);
            if (key(id, label, 0, keyW, false, row == 1 && i == 0))
            {
                KeyboardType(label);
                // one shifted key, as on a phone
                if (KeyboardShift && row > 0)
                    KeyboardShift = false;
            }
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(x0, y + 4 * (keyH + gap)));
    float unit = keyW + gap;
    if (key("##shift", "Shift", Icon::Capslock, 2 * unit - gap, KeyboardShift, false))
        KeyboardShift = !KeyboardShift;
    ImGui::SameLine(0, gap);
    if (key("##space", "Space", Icon::SpaceBar, 4 * unit - gap, false, false))
        KeyboardType(" ");
    ImGui::SameLine(0, gap);
    if (key("##backspace", nullptr, Icon::Backspace, 2 * unit - gap, false, false))
        KeyboardBackspace();
    ImGui::SameLine(0, gap);
    bool done = key("##done", "Done", Icon::Check, 2 * unit - gap, true, false);
    KeyboardFocus = false;

    FooterHints(*this, {{"A|Enter", "Type"}, {"B|Esc", "Done"}}, p.SurfaceContainerLow);
    ImGui::End();
    if (done)
        KeyboardTarget = nullptr;
}

GLuint App::IconTexture(long long key, const std::shared_ptr<const std::vector<unsigned char>>& icon)
{
    if (!icon || icon->size() != 32 * 32 * 4)
        return 0;
    const std::vector<unsigned char>& rgba = *icon;
    IconTex& texture = IconTextures[key];
    if (texture.Id && texture.Data == icon)
        return texture.Id;
    if (!texture.Id)
        glGenTextures(1, &texture.Id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.Id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    texture.Data = icon;
    return texture.Id;
}

GLuint App::Thumbnail(const std::filesystem::path& file, long long stamp, int& width, int& height)
{
    Texture& texture = Textures[file];
    if (!texture.Id || texture.Stamp != stamp)
    {
        std::vector<unsigned char> rgba;
        int w = 0, h = 0;
        if (!Frontend::ReadImage(file, rgba, w, h))
        {
            if (texture.Id)
                glDeleteTextures(1, &texture.Id);
            texture = {};
            Textures.erase(file);
            return 0;
        }
        if (!texture.Id)
            glGenTextures(1, &texture.Id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture.Id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        texture.Stamp = stamp;
        texture.W = w;
        texture.H = h;
    }
    width = texture.W;
    height = texture.H;
    return texture.Id;
}

GLuint App::ArtTexture(const std::string& file, uint64_t stamp, bool topScreen, int& width, int& height)
{
    width = height = 0;
    if (file.empty())
        return 0;
    std::string key = topScreen ? file + "#top" : file;
    ArtTex& texture = ArtTextures[key];
    texture.LastUsed = FrameNumber;
    // the file saved again since (art replaced, or cleared and downloaded again): read again
    if (texture.Stamp != stamp)
    {
        if (texture.Id)
            glDeleteTextures(1, &texture.Id);
        texture.Id = 0;
        texture.Failed = false;
        texture.Queued = false;
        texture.Stamp = stamp;
    }
    if (texture.Id)
    {
        width = texture.W;
        height = texture.H;
        return texture.Id;
    }
    // (one that failed is tried again after a while: the sync may have saved it since)
    if ((texture.Failed && FrameNumber - texture.FailedAt < 600) || texture.Queued)
        return 0;
    texture.Queued = true;
    {
        std::lock_guard guard(ArtLock);
        ArtQueue.push_back({key, stamp});
        if (!ArtWorker.joinable())
        {
            ArtStopping = false;
            ArtWorker = std::thread([this]
            {
                std::unique_lock lock(ArtLock);
                for (;;)
                {
                    ArtWake.wait(lock, [&] { return ArtStopping || !ArtQueue.empty(); });
                    if (ArtStopping)
                        return;
                    // the newest first: what's on screen now, after a fast scroll
                    ArtJob job = ArtQueue.back();
                    ArtQueue.pop_back();
                    const std::string& next = job.Key;
                    lock.unlock();
                    bool top = next.size() > 4 && next.compare(next.size() - 4, 4, "#top") == 0;
                    Frontend::ArtPixels pixels;
                    bool ok = Frontend::LoadArt(Frontend::PathFromUtf8(top ? next.substr(0, next.size() - 4) : next), 512, top, pixels);
                    lock.lock();
                    ArtDecoded decoded;
                    decoded.Key = next;
                    decoded.Stamp = job.Stamp;
                    if (ok)
                    {
                        decoded.Rgba = std::move(pixels.Rgba);
                        decoded.W = pixels.Width;
                        decoded.H = pixels.Height;
                    }
                    ArtDone.push_back(std::move(decoded));
                }
            });
        }
    }
    ArtWake.notify_all();
    return 0;
}

bool App::ArtPending()
{
    {
        std::lock_guard guard(ArtLock);
        if (!ArtQueue.empty() || !ArtDone.empty())
            return true;
    }
    for (const auto& [key, texture] : ArtTextures)
    {
        if (texture.Queued)
            return true;
    }
    return false;
}

void App::UploadArt()
{
    std::vector<ArtDecoded> done;
    {
        std::lock_guard guard(ArtLock);
        // a few a frame, so a screen full of new art doesn't stall one
        size_t n = std::min<size_t>(ArtDone.size(), 8);
        for (size_t i = 0; i < n; i++)
            done.push_back(std::move(ArtDone[i]));
        ArtDone.erase(ArtDone.begin(), ArtDone.begin() + n);
    }
    for (ArtDecoded& d : done)
    {
        auto it = ArtTextures.find(d.Key);
        // (an older stamp's, or one evicted meanwhile)
        if (it == ArtTextures.end() || it->second.Stamp != d.Stamp)
            continue;
        ArtTex& texture = it->second;
        texture.Queued = false;
        if (d.Rgba.empty())
        {
            texture.Failed = true;
            texture.FailedAt = FrameNumber;
            printf("ui: can't read the art %s\n", d.Key.c_str());
            fflush(stdout);
            continue;
        }
        glGenTextures(1, &texture.Id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture.Id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, d.W, d.H, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.Rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        texture.W = d.W;
        texture.H = d.H;
    }
    // over 160 (about 160 MB at most), the ones not drawn for the last few seconds go, the longest unused first
    constexpr size_t kMaxArt = 160;
    if (ArtTextures.size() > kMaxArt)
    {
        std::vector<std::pair<uint64_t, std::string>> old;
        for (const auto& [key, texture] : ArtTextures)
        {
            if (!texture.Queued && texture.LastUsed + 180 < FrameNumber)
                old.emplace_back(texture.LastUsed, key);
        }
        std::sort(old.begin(), old.end());
        for (size_t i = 0; i < old.size() && ArtTextures.size() > kMaxArt; i++)
        {
            auto it = ArtTextures.find(old[i].second);
            if (it->second.Id)
                glDeleteTextures(1, &it->second.Id);
            ArtTextures.erase(it);
        }
    }
}

void App::StopArtWorker()
{
    {
        std::lock_guard guard(ArtLock);
        ArtStopping = true;
        ArtQueue.clear();
    }
    ArtWake.notify_all();
    if (ArtWorker.joinable())
        ArtWorker.join();
    ArtDone.clear();
}

}
