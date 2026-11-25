// The menus: Dear ImGui (SDL2 and OpenGL3 backends, the GLES 3 shader path) drawn into the main window after the game frame, with the screen
// stack, the input routing between the menus and the game, toasts, the on-screen keyboard and the slot thumbnails' textures. Nothing is
// drawn and no ImGui frame is made while no screen, toast or keyboard is up, so the window shows the game exactly as without the menus.
// UI thread only
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

#include "imgui.h"
#include "Controls.h"
#include "Host.h"
#include "Screen.h"
#include "Screens.h"
#include "Theme.h"
#include "PlatformOGL.h"

namespace Ui
{

class App
{
public:
    // ImGui on the main window and its context (current), with the theme from the settings and the fonts. [hidden]: a scripted run, whose
    // UI scale doesn't follow the monitor's DPI (so its window shots stay the same)
    bool Init(SDL_Window* window, SDL_GLContext context, Host& host, bool hidden);
    void Shutdown();

    // An SDL event, before the game sees it: true when it belongs to the menus and the game mustn't get it. While a screen is open that's
    // every key, button and mouse event of the main window (the F-key hotkeys still go through); while none is, Esc or a controller's
    // Start or Guide opens the pause menu over a running game, and keys and buttons that were held when the menus closed stay with the menus
    // until they're released, so the press that closed a menu doesn't reach the game
    bool ProcessEvent(const SDL_Event& event);
    // the menus' own input, for --ui-script: as the keyboard or controller would send it
    void Inject(Frontend::MenuAction action);
    void InjectText(const std::string& text);

    // a screen is open: the game is paused behind it and gets no input
    bool IsOpen() const { return !Stack.empty(); }
    // there's something to draw (a screen, a toast or the keyboard), so the window is drawn every frame
    bool Active() const;
    std::string TopId() const;
    // a screen called [id] is open, anywhere on the stack
    bool HasScreen(const char* id) const;

    // builds the frame's draw data (once per present, before it), and draws it into the main window (current, its drawable bound)
    void BuildFrame();
    void Render();

    // the screen stack
    void Push(std::unique_ptr<Screen> screen);
    void Pop();
    // every screen closed: back to the game
    void CloseAll();
    // the library alone (no game), the home screen; [open] as MakeLibraryScreen's
    void ShowStart(LibraryOpen open = LibraryOpen::None);
    // --open: "library" (or "start"), "library/details", "library/settings", "library/settings/mods", "pause", "pause/save", "pause/load",
    // "pause/mods", "pause/mods/options/<script id>", "settings", "settings/library", "settings/achievements",
    // "settings/achievements/login", "keyboard", "layouts", "layout-editor" (the layout in use), "layout-editor/new",
    // "layout-editor/<id>"; false for a name it doesn't know. In a RetroAchievements hardcore session "pause/load" is the pause menu
    bool OpenPath(const std::string& path);
    void OpenPause();

    void Toast(const std::string& text);

    // the theme from desktop_theme, again (after the setting changed)
    void ApplyTheme();
    const Palette& Colors() const { return *Colors_; }

    Host& GetHost() { return *HostPtr; }
    // [v] reference pixels (the layout is made for 1280 x 720) at the window's UI scale
    float Px(float v) const { return v * Scale; }
    float GetScale() const { return Scale; }
    ImFont* Regular = nullptr;
    ImFont* Medium = nullptr;
    ImFont* Icons = nullptr;
    // the last menu input came from a controller (the hints show its buttons instead of keys)
    bool UsingPad() const { return LastInputPad; }

    // a save or load is running: the screens take no input until it's done
    bool Busy = false;

    // widgets: after an item that can take the focus, gives it the focus when its screen opens (or comes back on top, then the item that
    // had it), and remembers it
    void TrackFocus(bool isDefault);
    // the screen being drawn gives the focus to its default item again (from that item on, in this frame or the next), as when it opened
    void RefocusDefault();

    // the on-screen keyboard over the screen, editing [target] (kept alive by the caller until it closes); the physical keyboard types too
    // [masked]: the text shown as dots (a key); Ctrl+V pastes into it as into any field
    void OpenKeyboard(std::string* target, const std::string& title, size_t maxLength = 32, bool masked = false);
    bool KeyboardOpen() const { return KeyboardTarget != nullptr; }

    // a slot thumbnail's texture (the file read and uploaded once per [stamp]); 0 when it can't be read
    GLuint Thumbnail(const std::filesystem::path& file, long long stamp, int& width, int& height);
    // a game's 32 x 32 RGBA banner icon as a texture (nearest filtering, for the pixels), made the first time it's drawn and kept for
    // [key] (the ROM's id) until its data changes (the cache holds the data, so a new icon can never look like the old one)
    GLuint IconTexture(long long key, const std::shared_ptr<const std::vector<unsigned char>>& rgba);
    // A downloaded image (the library's art, a UTF-8 path) as a texture, shrunk to at most 512 pixels a side ([topScreen]: a title screen of
    // both screens cut to its top one): read on a worker thread the first time it's asked for and made a texture here once it's ready, so
    // 0 until then (and for a file that can't be read). Kept while it's drawn; the ones not drawn for a while go once there are many
    // [stamp] changes when the file is saved again: a texture of another stamp is read again
    GLuint ArtTexture(const std::string& file, uint64_t stamp, bool topScreen, int& width, int& height);
    // images asked for and not ready yet (what --ui-script's wait-sync also waits for)
    bool ArtPending();

private:
    void ApplyScale(int w, int h);
    void DrawToasts();
    void DrawKeyboard();
    void KeyboardType(const char* utf8);
    void KeyboardBackspace();
    void Back();
    void Menu();
    // an action that goes to ImGui as a gamepad key (down now, up a frame later through the trickling input queue)
    void SendPadKey(ImGuiKey key);
    void FeedPadButton(const SDL_ControllerButtonEvent& event);
    void FeedAxis(const SDL_ControllerAxisEvent& event);
    void SwallowHeld();
    // the mouse's state in ImGui and its backend let go when the menus open, and its capture when they close
    void ResetMouse();
    void ReleaseMouse();
    Screen* Top() const { return Stack.empty() ? nullptr : Stack.back().get(); }
    // the screen whose Draw is running
    Screen* Drawing = nullptr;

    Host* HostPtr = nullptr;
    SDL_Window* Window = nullptr;
    bool Hidden = false;
    bool Initialized = false;
    const Palette* Colors_ = &DarkPalette();
    float Scale = 1;
    float DpiScale = 1;
    bool StyleDirty = true;
    std::vector<std::unique_ptr<Screen>> Stack;
    // screens closed during a frame are kept until it ends, since their Draw may still be running
    std::vector<std::unique_ptr<Screen>> Closed;
    bool LastInputPad = false;

    // what the menus see held, to keep it from the game after they close
    std::set<SDL_Keycode> HeldKeys;
    std::set<int> HeldButtons;
    std::set<SDL_Keycode> SwallowKeys;
    std::set<int> SwallowButtons;
    bool SwallowStick = false;
    int StickX = 0, StickY = 0;

    struct ToastItem { std::string Text; std::chrono::steady_clock::time_point Until; };
    std::vector<ToastItem> Toasts;

    std::string* KeyboardTarget = nullptr;
    std::string KeyboardTitle;
    size_t KeyboardMax = 32;
    bool KeyboardShift = false;
    bool KeyboardFocus = false;

    struct Texture { GLuint Id = 0; long long Stamp = 0; int W = 0, H = 0; };
    std::map<std::filesystem::path, Texture> Textures;
    struct IconTex { GLuint Id = 0; std::shared_ptr<const std::vector<unsigned char>> Data; };
    std::map<long long, IconTex> IconTextures;
    // the art: textures by file (with "#top" for a top screen), what the worker has to read and what it has read
    void UploadArt();
    void StopArtWorker();
    struct ArtTex { GLuint Id = 0; int W = 0, H = 0; bool Failed = false; bool Queued = false; uint64_t LastUsed = 0, FailedAt = 0, Stamp = 0; };
    std::map<std::string, ArtTex> ArtTextures;
    struct ArtDecoded { std::string Key; uint64_t Stamp = 0; std::vector<unsigned char> Rgba; int W = 0, H = 0; };
    struct ArtJob { std::string Key; uint64_t Stamp = 0; };
    std::mutex ArtLock;
    std::condition_variable ArtWake;
    std::deque<ArtJob> ArtQueue;
    std::vector<ArtDecoded> ArtDone;
    std::thread ArtWorker;
    bool ArtStopping = false;
    uint64_t FrameNumber = 0;
    bool KeyboardMasked = false;

    std::vector<unsigned char> FontData[3];
    // the system's CJK fonts merged into Regular and Medium (file mappings)
    std::vector<std::pair<void*, const void*>> MappedFonts;
};

// "\xee\x80\xb7" for an icon's codepoint, to draw with App::Icons
const char* IconText(ImWchar icon);

}
