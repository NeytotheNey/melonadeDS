// The windows the UI thread presents into: the main window, and with detached screens a second one, so the top and bottom screens each
// get a window of their own. Each window keeps its own place (size, position, monitor), fullscreen and, when detached, fit, all remembered
// in the settings. Both windows draw the same frame with the one UI context, made current on each in turn; only the main window waits for
// vsync, so two vsync waits never halve the frame rate. UI thread only
#pragma once

#include <optional>
#include <string>

#include <SDL.h>

#include "Presenter.h"

namespace Frontend
{

class Settings;

enum class Fullscreen { Off, Borderless, Exclusive };

// A window's normal place (what it goes back to from fullscreen or maximized): size and position in pixels, the position relative to the
// top-left of its display, which is found again by name (and index, for two of the same name) so it lands on the same monitor
struct WindowPlace
{
    int W = 0, H = 0;
    int X = 0, Y = 0;
    bool HasPosition = false; // without one: centered (the main window) or next to the main window (the second one)
    bool Global = false;      // X and Y are desktop coordinates (--window), not relative to a display
    std::string Display;
    int DisplayIndex = 0;
    bool Maximized = false;
    // saved on exit only when it came from the settings or the user moved, resized, maximized or fullscreened the window: a default
    // size is shaped by the run's options (--layout, --wide, --window-scale), which mustn't end up in the settings
    bool Keep = false;
    // the window's rect as the frontend last set it, to tell the user's moves from its own
    bool HasApplied = false;
    int AppliedX = 0, AppliedY = 0, AppliedW = 0, AppliedH = 0;
};

// --window W,H[,X,Y]
bool ParseWindowSpec(const std::string& spec, WindowPlace& place);
const char* FitName(Fit fit);

class ScreenWindows
{
public:
    struct Options
    {
        bool Hidden = false;
        bool VSync = false;
        int WindowScale = 2;
        // the top screen's size in DS pixels (wider with WideMelon), for the default window sizes
        int TopW = 256, TopH = 192;
        // --window and --window2: places for this run, never saved
        std::optional<WindowPlace> MainOverride, SecondOverride;
        // a hidden run leaves the saved places alone unless this asks (--save-window-places, for tests); it never applies fullscreen
        bool SaveHiddenPlaces = false;
    };

    // reads the screens, fit, layout and window settings and makes the main window, in the place of the mode the run starts in (hidden
    // with Hidden); the GL context is made on it after this
    SDL_Window* CreateMain(Settings& settings, const Options& options);
    // with the UI context made (and current on the main window): the main window's fullscreen, and the second window when detached
    void Start(SDL_GLContext context);
    // the places saved and the second window closed; the main window is left to the caller, which destroys its context first
    void Close();

    SDL_Window* Main() const { return MainWindow; }
    bool IsDetached() const { return SecondWindow != nullptr; }
    bool IsSwapped() const { return Swap; }

    // together <-> detached, saved as desktop_screens
    void SetDetached(bool detached);
    void ToggleSwap();
    // the together layout (F2)
    void NextLayout();
    // the fit of the screen the window [windowId] shows (0: the main window); detached only
    void CycleFit(Uint32 windowId);
    // fullscreen on and off for the window [windowId] (0: the main window): the main one goes to desktop_fullscreen_mode (borderless or
    // exclusive), the second one is always borderless on its own monitor
    void ToggleFullscreen(Uint32 windowId);

    // a window event: CLOSE of the main window sets [quit], of the second one goes back to together; moves and resizes are remembered
    void OnWindowEvent(const SDL_WindowEvent& event, bool& quit);

    // draws [frame] into each window, calls [afterDraw] (the frame's present fence), then shows them: the second window without waiting,
    // then the main window with its vsync. Leaves the main window current
    template <typename AfterDraw> void Present(Presenter& presenter, const FrameView& frame, bool linear, AfterDraw afterDraw)
    {
        DrawAll(presenter, frame, linear);
        afterDraw();
        SwapAll();
    }
    // --window-shot: [path] for the main window when together, [path]_top.bmp and [path]_bottom.bmp when detached (named for the screen
    // each shows). Each window is drawn again and read from its back buffer before any swap
    void WindowShot(Presenter& presenter, const FrameView& frame, bool linear, const std::string& path);

    // the window that shows the bottom screen, and where the bottom screen is in that window's drawable pixels now (for the window's
    // current size, layout, fit and swap, and the shape of the last frame drawn)
    SDL_Window* BottomWindow() const { return (IsDetached() && !Swap) ? SecondWindow : MainWindow; }
    Rect BottomRect() const;
    // the frame shape (top screen width and margin) BottomRect assumes before the first frame is drawn
    void SetFrameShape(const FrameView& frame) { Shape = frame; }

    // the titles: [stats] after the main window's name
    void UpdateTitles(const std::string& stats);

private:
    void DrawAll(Presenter& presenter, const FrameView& frame, bool linear);
    void SwapAll();
    // what each window shows: the layout and fit to draw it with
    Layout MainLayout() const;
    Fit MainFit() const;
    Fit SecondFit() const;
    bool MainShowsTop() const { return !Swap; }

    // false when it can't (MELONADE_TEST_NO_SECOND_WINDOW=1 makes it fail, for tests)
    bool OpenSecond();
    void CloseSecond();
    void MarkApplied(SDL_Window* window, WindowPlace& place);
    void ApplySecondStyle();
    // the place to use for a window: the override, the stored one, or the default size
    WindowPlace& MainSlot() { return IsDetachedMode ? MainDetachedPlace : MainTogetherPlace; }
    void PlaceWindow(SDL_Window* window, const WindowPlace& place, bool clamp);
    void RecordNormal(SDL_Window* window, WindowPlace& place);
    void Track(SDL_Window* window);
    void SetFullscreen(SDL_Window* window, Fullscreen mode);
    void CheckSwapIntervals();
    void SetInterval(int interval);
    void SavePlaces();
    void SaveFits();
    std::string Title(bool main) const;
    static SDL_HitTestResult HitTest(SDL_Window* window, const SDL_Point* point, void* data);

    Settings* Store = nullptr;
    Options Opts;
    SDL_Window* MainWindow = nullptr;
    SDL_Window* SecondWindow = nullptr;
    SDL_GLContext Context = nullptr;
    bool IsDetachedMode = false;
    bool Swap = false;
    Layout TogetherLayout = Layout::Vertical;
    Fit FitTop = Fit::Aspect, FitBottom = Fit::Aspect;
    WindowPlace MainTogetherPlace, MainDetachedPlace, SecondPlace;
    // what each window's fullscreen is (a hidden run keeps track without applying it)
    Fullscreen MainFull = Fullscreen::Off;
    bool SecondFull = false;
    ScreenRects MainRects, SecondRects;
    FrameView Shape;
    std::string Stats;
    // whether one context's swap interval is per window (WGL_EXT_swap_control's wording) or per context (some drivers); in the latter case
    // it's set before each swap. CurrentInterval is the last one set, -1 for unknown
    bool IntervalPerContext = true;
    int CurrentInterval = -1;
};

}
