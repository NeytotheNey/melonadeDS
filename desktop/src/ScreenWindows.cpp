#include "ScreenWindows.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Settings.h"

namespace Frontend
{

namespace
{

const char* const kLayoutNames[] = {"vertical", "horizontal", "top", "bottom"};
const char* const kFitNames[] = {"aspect", "integer", "stretch"};

Layout ParseLayout(const std::string& name)
{
    for (int i = 0; i < (int)Layout::Count; i++)
    {
        if (name == kLayoutNames[i])
            return (Layout)i;
    }
    return Layout::Vertical;
}

Fit ParseFit(const std::string& name)
{
    for (int i = 0; i < (int)Fit::Count; i++)
    {
        if (name == kFitNames[i])
            return (Fit)i;
    }
    return Fit::Aspect;
}

const char* FullscreenName(Fullscreen mode)
{
    switch (mode)
    {
    case Fullscreen::Borderless: return "borderless";
    case Fullscreen::Exclusive: return "exclusive";
    default: return "off";
    }
}

Fullscreen ParseFullscreen(const std::string& name)
{
    if (name == "borderless") return Fullscreen::Borderless;
    if (name == "exclusive") return Fullscreen::Exclusive;
    return Fullscreen::Off;
}

int JsonInt(const picojson::object& object, const char* key, int fallback)
{
    auto it = object.find(key);
    // in int range (a cast of one that isn't is undefined)
    if (it == object.end() || !it->second.is<double>())
        return fallback;
    double value = it->second.get<double>();
    return value > -1e9 && value < 1e9 ? (int)value : fallback;
}

// a stored place (desktop_window, desktop_window_detached, desktop_window2); false when there's none or it's broken
bool LoadPlace(const Settings& settings, const char* key, WindowPlace& place, std::string* fullscreen)
{
    const picojson::value* value = settings.FindStored(key);
    if (!value || !value->is<picojson::object>())
        return false;
    const picojson::object& object = value->get<picojson::object>();
    int w = JsonInt(object, "w", 0), h = JsonInt(object, "h", 0);
    if (w < 16 || h < 16 || w > 16384 || h > 16384)
        return false;
    place = {};
    place.W = w;
    place.H = h;
    if (object.count("x") && object.count("y"))
    {
        const int none = -2000000000;
        int x = JsonInt(object, "x", none), y = JsonInt(object, "y", none);
        if (x != none && y != none)
        {
            place.X = x;
            place.Y = y;
            place.HasPosition = true;
        }
    }
    auto display = object.find("display");
    if (display != object.end() && display->second.is<std::string>())
        place.Display = display->second.get<std::string>();
    place.DisplayIndex = JsonInt(object, "display_index", 0);
    auto maximized = object.find("maximized");
    place.Maximized = maximized != object.end() && maximized->second.is<bool>() && maximized->second.get<bool>();
    auto full = object.find("fullscreen");
    if (fullscreen && full != object.end() && full->second.is<std::string>())
        *fullscreen = full->second.get<std::string>();
    place.Keep = true;
    return true;
}

picojson::value PlaceJson(const WindowPlace& place, const char* fullscreen)
{
    picojson::object object;
    object["w"] = picojson::value((double)place.W);
    object["h"] = picojson::value((double)place.H);
    if (place.HasPosition)
    {
        object["x"] = picojson::value((double)place.X);
        object["y"] = picojson::value((double)place.Y);
        object["display"] = picojson::value(place.Display);
        object["display_index"] = picojson::value((double)place.DisplayIndex);
    }
    object["maximized"] = picojson::value(place.Maximized);
    if (fullscreen)
        object["fullscreen"] = picojson::value(std::string(fullscreen));
    return picojson::value(object);
}

// the display a stored place was on: the one with its name (its index first, for two of the same name), else its index, else the first
int FindDisplay(const WindowPlace& place)
{
    int count = SDL_GetNumVideoDisplays();
    if (count <= 0)
        return 0;
    auto nameIs = [&](int index)
    {
        const char* name = SDL_GetDisplayName(index);
        return name && place.Display == name;
    };
    if (!place.Display.empty())
    {
        if (place.DisplayIndex >= 0 && place.DisplayIndex < count && nameIs(place.DisplayIndex))
            return place.DisplayIndex;
        for (int i = 0; i < count; i++)
        {
            if (nameIs(i))
                return i;
        }
    }
    return place.DisplayIndex >= 0 && place.DisplayIndex < count ? place.DisplayIndex : 0;
}

bool PointOnADisplay(int x, int y)
{
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++)
    {
        SDL_Rect bounds;
        SDL_Point point {x, y};
        if (SDL_GetDisplayBounds(i, &bounds) == 0 && SDL_PointInRect(&point, &bounds))
            return true;
    }
    return false;
}

int DisplayAt(int x, int y)
{
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++)
    {
        SDL_Rect bounds;
        SDL_Point point {x, y};
        if (SDL_GetDisplayBounds(i, &bounds) == 0 && SDL_PointInRect(&point, &bounds))
            return i;
    }
    return -1;
}

// the frame Windows puts around a bordered window's client area on [display] (title bar on top, resize borders), for SDL's window style
// at the display's DPI; all 0 for a borderless window. SDL's sizes and positions are the client area's, so this is what has to fit too
void FrameInsets(int display, bool bordered, int& left, int& top, int& right, int& bottom)
{
    left = top = right = bottom = 0;
    if (!bordered)
        return;
    float dpi = 0;
    if (SDL_GetDisplayDPI(display, &dpi, nullptr, nullptr) != 0 || dpi <= 0)
        dpi = 96;
    RECT rect {0, 0, 100, 100};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (!AdjustWindowRectExForDpi(&rect, style, FALSE, 0, (UINT)(dpi + 0.5f)))
        AdjustWindowRectEx(&rect, style, FALSE, 0);
    left = -rect.left;
    top = -rect.top;
    right = rect.right - 100;
    bottom = rect.bottom - 100;
}

// fits a window rect (client area) into [display]'s usable area with its frame: shrunk to fit, and moved in so the title bar and the
// edges are on it; [center] puts it in the middle instead
void FitToDisplay(int display, bool bordered, bool center, int& x, int& y, int& w, int& h)
{
    SDL_Rect usable {0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(display, &usable) != 0 && SDL_GetDisplayBounds(display, &usable) != 0)
        return;
    if (usable.w <= 0 || usable.h <= 0)
        return;
    int left, top, right, bottom;
    FrameInsets(display, bordered, left, top, right, bottom);
    int maxW = std::max(usable.w - left - right, 16), maxH = std::max(usable.h - top - bottom, 16);
    w = std::min(w, maxW);
    h = std::min(h, maxH);
    if (center)
    {
        x = usable.x + left + (maxW - w) / 2;
        y = usable.y + top + (maxH - h) / 2;
    }
    else
    {
        x = std::clamp(x, usable.x + left, usable.x + left + maxW - w);
        y = std::clamp(y, usable.y + top, usable.y + top + maxH - h);
    }
}

// a place's window rect in desktop coordinates. [clamp] (a stored place) fits it into its display's usable area with its frame
// ([bordered]), centered there when its monitor is gone or its title bar isn't on any display any more. false when the place has no
// position
bool ResolvePlace(const WindowPlace& place, bool clamp, bool bordered, int& x, int& y, int& w, int& h)
{
    w = place.W;
    h = place.H;
    if (!place.HasPosition)
        return false;
    if (place.Global)
    {
        x = place.X;
        y = place.Y;
        return true;
    }
    int display = FindDisplay(place);
    SDL_Rect bounds {0, 0, 0, 0};
    SDL_GetDisplayBounds(display, &bounds);
    x = bounds.x + place.X;
    y = bounds.y + place.Y;
    if (clamp)
    {
        int left, top, right, bottom;
        FrameInsets(display, bordered, left, top, right, bottom);
        bool foundByName = place.Display.empty() || (SDL_GetDisplayName(display) && place.Display == SDL_GetDisplayName(display));
        bool titleShown = PointOnADisplay(x + w / 2, y - top / 2 - 1) && PointOnADisplay(x + std::min(w, 64) / 2, y - top / 2 - 1);
        FitToDisplay(display, bordered, !foundByName || !titleShown, x, y, w, h);
    }
    return true;
}

}

bool ParseWindowSpec(const std::string& spec, WindowPlace& place)
{
    int values[4];
    int count = sscanf(spec.c_str(), "%d,%d,%d,%d", &values[0], &values[1], &values[2], &values[3]);
    if ((count != 2 && count != 4) || values[0] < 16 || values[1] < 16)
        return false;
    place = {};
    place.W = values[0];
    place.H = values[1];
    if (count == 4)
    {
        place.X = values[2];
        place.Y = values[3];
        place.HasPosition = place.Global = true;
    }
    return true;
}

const char* FitName(Fit fit)
{
    return kFitNames[(int)fit];
}

SDL_Window* ScreenWindows::CreateMain(Settings& settings, const Options& options)
{
    Store = &settings;
    Opts = options;
    IsDetachedMode = settings.GetString("desktop_screens", "together") == "detached";
    Swap = settings.GetBool("desktop_swap_screens", false);
    TogetherLayout = ParseLayout(settings.GetString("desktop_layout", "vertical"));
    FitTop = ParseFit(settings.GetString("desktop_fit_top", "aspect"));
    FitBottom = ParseFit(settings.GetString("desktop_fit_bottom", "aspect"));

    // the default sizes: the screens at the window scale, times the first monitor's scale (150% makes a 2x window 3x in pixels, the size
    // it had before the process was DPI aware) so a first run isn't tiny on a high-DPI monitor. Hidden runs keep the plain window scale,
    // so their window shots stay what they always were
    float scale = (float)Opts.WindowScale;
    float dpi = 0;
    if (!Opts.Hidden && SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) == 0 && dpi > 0)
        scale *= std::max(dpi / 96.f, 1.f);
    auto scaled = [&](int size) { return (int)(size * scale + 0.5f); };
    int togetherW = TogetherLayout == Layout::Horizontal ? Opts.TopW + 256 : Opts.TopW;
    int togetherH = TogetherLayout == Layout::Vertical ? 192 + Opts.TopH : Opts.TopH;
    MainTogetherPlace = {};
    MainTogetherPlace.W = scaled(togetherW);
    MainTogetherPlace.H = scaled(togetherH);
    std::string full;
    LoadPlace(settings, "desktop_window", MainTogetherPlace, &full);
    MainFull = ParseFullscreen(full);
    MainDetachedPlace = {};
    MainDetachedPlace.W = scaled(Swap ? 256 : Opts.TopW);
    MainDetachedPlace.H = scaled(Swap ? 192 : Opts.TopH);
    LoadPlace(settings, "desktop_window_detached", MainDetachedPlace, nullptr);
    SecondPlace = {};
    SecondPlace.W = scaled(Swap ? Opts.TopW : 256);
    SecondPlace.H = scaled(Swap ? Opts.TopH : 192);
    full.clear();
    LoadPlace(settings, "desktop_window2", SecondPlace, &full);
    SecondFull = ParseFullscreen(full) != Fullscreen::Off;
    // the command line's places aren't fitted to the displays, so a test gets exactly the size it asked for
    bool clampMain = !Opts.MainOverride;
    if (Opts.MainOverride)
        MainTogetherPlace = MainDetachedPlace = *Opts.MainOverride;
    if (Opts.SecondOverride)
        SecondPlace = *Opts.SecondOverride;

    WindowPlace& place = MainSlot();
    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED, w, h;
    // a default size is fitted to the first monitor, title bar included, and centered there (a hidden run keeps SDL's centering and
    // the exact size, which no one sees)
    if (!ResolvePlace(place, clampMain, true, x, y, w, h) && !Opts.Hidden && !Opts.MainOverride)
        FitToDisplay(0, true, true, x, y, w, h);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (Opts.Hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    if (place.Maximized && !Opts.Hidden)
        flags |= SDL_WINDOW_MAXIMIZED;
    MainWindow = SDL_CreateWindow(Title(true).c_str(), x, y, w, h, flags);
    if (MainWindow)
        MarkApplied(MainWindow, place);
    return MainWindow;
}

void ScreenWindows::Start(SDL_GLContext context)
{
    Context = context;
    SDL_GL_SetSwapInterval(Opts.VSync ? 1 : 0);
    CurrentInterval = Opts.VSync ? 1 : 0;
    IntervalPerContext = false;
    // the second window before the main one's fullscreen, so opening it can't take the focus from an exclusive fullscreen window
    if (IsDetachedMode && !OpenSecond())
    {
        // together after all, in the together place
        IsDetachedMode = false;
        printf("screens: together (the second window didn't open)\n");
        if (!(SDL_GetWindowFlags(MainWindow) & SDL_WINDOW_MAXIMIZED))
        {
            PlaceWindow(MainWindow, MainTogetherPlace, !Opts.MainOverride);
            MarkApplied(MainWindow, MainTogetherPlace);
        }
    }
    if (MainFull != Fullscreen::Off)
        SetFullscreen(MainWindow, MainFull);
    UpdateTitles(Stats);
}

void ScreenWindows::MarkApplied(SDL_Window* window, WindowPlace& place)
{
    SDL_GetWindowPosition(window, &place.AppliedX, &place.AppliedY);
    SDL_GetWindowSize(window, &place.AppliedW, &place.AppliedH);
    place.HasApplied = true;
}

void ScreenWindows::Close()
{
    SavePlaces();
    if (SecondWindow)
    {
        SDL_GL_MakeCurrent(MainWindow, Context);
        SDL_DestroyWindow(SecondWindow);
        SecondWindow = nullptr;
    }
}

Layout ScreenWindows::MainLayout() const
{
    if (!IsDetached())
        return TogetherLayout;
    return MainShowsTop() ? Layout::TopOnly : Layout::BottomOnly;
}

Fit ScreenWindows::MainFit() const
{
    if (!IsDetached())
        return Fit::Aspect;
    return MainShowsTop() ? FitTop : FitBottom;
}

Fit ScreenWindows::SecondFit() const
{
    return MainShowsTop() ? FitBottom : FitTop;
}

std::string ScreenWindows::Title(bool main) const
{
    if (!IsDetachedMode)
        return "melonadeDS";
    bool top = main ? MainShowsTop() : !MainShowsTop();
    return top ? "melonadeDS: top" : "melonadeDS: bottom";
}

void ScreenWindows::UpdateTitles(const std::string& stats)
{
    Stats = stats;
    if (MainWindow)
        SDL_SetWindowTitle(MainWindow, (Title(true) + Stats).c_str());
    if (SecondWindow)
        SDL_SetWindowTitle(SecondWindow, Title(false).c_str());
}

void ScreenWindows::RecordNormal(SDL_Window* window, WindowPlace& place)
{
    int x, y, w, h;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    int display = SDL_GetWindowDisplayIndex(window);
    SDL_Rect bounds {0, 0, 0, 0};
    if (display < 0 || SDL_GetDisplayBounds(display, &bounds) != 0)
        display = 0;
    const char* name = SDL_GetDisplayName(display);
    place.W = w;
    place.H = h;
    place.X = x - bounds.x;
    place.Y = y - bounds.y;
    place.HasPosition = true;
    place.Global = false;
    place.Display = name ? name : "";
    place.DisplayIndex = display;
}

void ScreenWindows::Track(SDL_Window* window)
{
    if (!window || (window != MainWindow && window != SecondWindow))
        return;
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED))
        return;
    WindowPlace& place = window == MainWindow ? MainSlot() : SecondPlace;
    // a hidden window is never maximized, which says nothing about the saved place
    if (!Opts.Hidden)
    {
        bool maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (maximized != place.Maximized)
            place.Keep = true;
        place.Maximized = maximized;
    }
    if (flags & SDL_WINDOW_MAXIMIZED)
        return;
    // the user moved or resized it since the frontend last placed it: the place is theirs now
    int x, y, w, h;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    if (place.HasApplied && (x != place.AppliedX || y != place.AppliedY || w != place.AppliedW || h != place.AppliedH))
        place.Keep = true;
    RecordNormal(window, place);
}

void ScreenWindows::PlaceWindow(SDL_Window* window, const WindowPlace& place, bool clamp)
{
    int x, y, w, h;
    bool bordered = !(SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS);
    bool positioned = ResolvePlace(place, clamp, bordered, x, y, w, h);
    if (!positioned && clamp && !Opts.Hidden)
    {
        // a default size: kept on the monitor the window is on, where it is
        SDL_GetWindowPosition(window, &x, &y);
        int display = std::max(SDL_GetWindowDisplayIndex(window), 0);
        FitToDisplay(display, bordered, false, x, y, w, h);
        positioned = true;
    }
    SDL_SetWindowSize(window, w, h);
    if (positioned)
        SDL_SetWindowPosition(window, x, y);
}

void ScreenWindows::SetFullscreen(SDL_Window* window, Fullscreen mode)
{
    // a hidden run keeps track of it, but a hidden window never takes a monitor (an exclusive mode would change the display's)
    if (Opts.Hidden)
        return;
    Uint32 flags = SDL_GetWindowFlags(window);
    if (!(flags & SDL_WINDOW_FULLSCREEN))
        Track(window);
    int result = 0;
    if (mode == Fullscreen::Exclusive)
    {
        // the monitor's own mode: without one SDL picks the mode closest to the window's size
        SDL_DisplayMode displayMode;
        int display = SDL_GetWindowDisplayIndex(window);
        if (display >= 0 && SDL_GetDesktopDisplayMode(display, &displayMode) == 0)
            SDL_SetWindowDisplayMode(window, &displayMode);
        result = SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
    else if (mode == Fullscreen::Borderless)
        result = SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    else
    {
        result = SDL_SetWindowFullscreen(window, 0);
        // back to the window's own place, which can be another mode's than the one it went fullscreen in
        WindowPlace& place = window == MainWindow ? MainSlot() : SecondPlace;
        if (place.Maximized)
            SDL_MaximizeWindow(window);
        else
        {
            PlaceWindow(window, place, false);
            MarkApplied(window, place);
        }
    }
    if (result != 0)
        fprintf(stderr, "fullscreen %s: %s\n", FullscreenName(mode), SDL_GetError());
}

void ScreenWindows::ToggleFullscreen(Uint32 windowId)
{
    if (SecondWindow && windowId && windowId == SDL_GetWindowID(SecondWindow))
    {
        SecondFull = !SecondFull;
        SecondPlace.Keep = true;
        SetFullscreen(SecondWindow, SecondFull ? Fullscreen::Borderless : Fullscreen::Off);
        printf("fullscreen %s window: %s\n", Title(false).c_str() + 12, SecondFull ? "borderless" : "off");
        return;
    }
    Fullscreen next = Fullscreen::Off;
    if (MainFull == Fullscreen::Off)
        next = Store->GetString("desktop_fullscreen_mode", "borderless") == "exclusive" ? Fullscreen::Exclusive : Fullscreen::Borderless;
    MainFull = next;
    // the main window's fullscreen is saved with the together place
    MainTogetherPlace.Keep = true;
    SetFullscreen(MainWindow, next);
    printf("fullscreen main window: %s\n", FullscreenName(next));
}

bool ScreenWindows::OpenSecond()
{
    if (SecondWindow)
        return true;
    // for tests: the second window doesn't open, as when SDL or the driver refuses it
    const char* fail = SDL_getenv("MELONADE_TEST_NO_SECOND_WINDOW");
    if (fail && fail[0] == '1')
    {
        fprintf(stderr, "can't open the second window: MELONADE_TEST_NO_SECOND_WINDOW is set\n");
        return false;
    }
    bool noTitle = Store->GetBool("desktop_bottom_no_title", false);
    bool onTop = Store->GetBool("desktop_bottom_on_top", false);
    int x, y, w, h;
    if (!ResolvePlace(SecondPlace, !Opts.SecondOverride, !noTitle, x, y, w, h))
    {
        // next to the main window: to its right, on the display that lands on (or the main window's), fitted to it with its title bar
        int mx, my, mw, mh;
        SDL_GetWindowPosition(MainWindow, &mx, &my);
        SDL_GetWindowSize(MainWindow, &mw, &mh);
        x = mx + mw + 16;
        y = my;
        if (!Opts.Hidden && !Opts.SecondOverride)
        {
            int display = DisplayAt(x + w / 2, y);
            if (display < 0)
            {
                // off the right edge: just inside it, on the main window's display
                display = std::max(SDL_GetWindowDisplayIndex(MainWindow), 0);
                x = INT_MAX / 2;
            }
            FitToDisplay(display, !noTitle, false, x, y, w, h);
        }
    }
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (Opts.Hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    if (noTitle)
        flags |= SDL_WINDOW_BORDERLESS;
    if (onTop)
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    if (SecondPlace.Maximized && !Opts.Hidden)
        flags |= SDL_WINDOW_MAXIMIZED;
    // before the window exists: an exclusive fullscreen main window minimizes when it loses the focus, which a new window shown takes.
    // Shown without taking the focus too, so the main window keeps the keyboard
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
    // the same GL attributes as the main window's, so the same pixel format and the UI context can be made current on it
    SecondWindow = SDL_CreateWindow(Title(false).c_str(), x, y, w, h, flags);
    SDL_ResetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN);
    if (!SecondWindow)
    {
        fprintf(stderr, "can't open the second window: %s\n", SDL_GetError());
        SDL_ResetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS);
        return false;
    }
    if (noTitle)
        SDL_SetWindowHitTest(SecondWindow, HitTest, this);
    if (SDL_GL_MakeCurrent(SecondWindow, Context) != 0)
    {
        fprintf(stderr, "the UI context doesn't go on the second window: %s\n", SDL_GetError());
        SDL_GL_MakeCurrent(MainWindow, Context);
        SDL_DestroyWindow(SecondWindow);
        SecondWindow = nullptr;
        SDL_ResetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS);
        return false;
    }
    SDL_GL_MakeCurrent(MainWindow, Context);
    MarkApplied(SecondWindow, SecondPlace);
    if (SecondFull)
        SetFullscreen(SecondWindow, Fullscreen::Borderless);
    CheckSwapIntervals();
    return true;
}

void ScreenWindows::CloseSecond()
{
    if (!SecondWindow)
        return;
    Track(SecondWindow);
    SDL_GL_MakeCurrent(MainWindow, Context);
    SDL_DestroyWindow(SecondWindow);
    SecondWindow = nullptr;
    SecondRects = {};
    SDL_ResetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS);
    SDL_GL_SetSwapInterval(Opts.VSync ? 1 : 0);
    CurrentInterval = Opts.VSync ? 1 : 0;
    IntervalPerContext = false;
}

void ScreenWindows::CheckSwapIntervals()
{
    // WGL_EXT_swap_control sets the interval of the window the context is current on, but drivers have kept it per context. Set 1 on the
    // main window and 0 on the second, then read the main one's back: still 1 means per window and nothing more to do, 0 means per context,
    // so it's set before each swap
    using GetIntervalFn = int(WINAPI*)();
    auto getInterval = (GetIntervalFn)SDL_GL_GetProcAddress("wglGetSwapIntervalEXT");
    SDL_GL_MakeCurrent(MainWindow, Context);
    SDL_GL_SetSwapInterval(1);
    SDL_GL_MakeCurrent(SecondWindow, Context);
    SDL_GL_SetSwapInterval(0);
    int second = getInterval ? getInterval() : SDL_GL_GetSwapInterval();
    SDL_GL_MakeCurrent(MainWindow, Context);
    int main = getInterval ? getInterval() : SDL_GL_GetSwapInterval();
    IntervalPerContext = main != 1;
    int wanted = Opts.VSync ? 1 : 0;
    SDL_GL_SetSwapInterval(wanted);
    CurrentInterval = wanted;
    printf("detached: swap interval is per %s (test: main read back %d, second %d); main %d, second 0\n",
        IntervalPerContext ? "context, set before each swap" : "window", main, second, wanted);
}

void ScreenWindows::SetInterval(int interval)
{
    if (IntervalPerContext && CurrentInterval != interval)
    {
        SDL_GL_SetSwapInterval(interval);
        CurrentInterval = interval;
    }
}

void ScreenWindows::DrawAll(Presenter& presenter, const FrameView& frame, bool linear)
{
    Shape = frame;
    int w, h;
    SDL_GL_GetDrawableSize(MainWindow, &w, &h);
    MainRects = presenter.Draw(frame, w, h, MainLayout(), linear, MainFit(), IsDetached() ? false : Swap);
    if (SecondWindow)
    {
        SDL_GL_MakeCurrent(SecondWindow, Context);
        SDL_GL_GetDrawableSize(SecondWindow, &w, &h);
        SecondRects = presenter.Draw(frame, w, h, MainShowsTop() ? Layout::BottomOnly : Layout::TopOnly, linear, SecondFit());
    }
}

void ScreenWindows::SwapAll()
{
    if (SecondWindow)
    {
        // the second window first and without waiting, the UI context still current on it from DrawAll
        SetInterval(0);
        SDL_GL_SwapWindow(SecondWindow);
        SDL_GL_MakeCurrent(MainWindow, Context);
        SetInterval(Opts.VSync ? 1 : 0);
    }
    SDL_GL_SwapWindow(MainWindow);
}

void ScreenWindows::WindowShot(Presenter& presenter, const FrameView& frame, bool linear, const std::string& path)
{
    auto shot = [&](SDL_Window* window, Layout layout, Fit fit, bool swap, const std::string& file)
    {
        SDL_GL_MakeCurrent(window, Context);
        int dw, dh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        presenter.Draw(frame, dw, dh, layout, linear, fit, swap);
        std::vector<melonDS::u8> pixels((size_t)dw * dh * 4), flipped(pixels.size());
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 0; y < dh; y++)
            memcpy(&flipped[(size_t)y * dw * 4], &pixels[(size_t)(dh - 1 - y) * dw * 4], (size_t)dw * 4);
        if (WriteBMP(file.c_str(), flipped, dw, dh))
            printf("window shot %s (%dx%d)\n", file.c_str(), dw, dh);
        else
            fprintf(stderr, "window shot %s failed\n", file.c_str());
    };
    if (!SecondWindow)
        shot(MainWindow, MainLayout(), MainFit(), Swap, path);
    else
    {
        std::string base = path;
        if (base.size() > 4 && _stricmp(base.c_str() + base.size() - 4, ".bmp") == 0)
            base.resize(base.size() - 4);
        bool top = MainShowsTop();
        shot(MainWindow, MainLayout(), MainFit(), false, base + (top ? "_top.bmp" : "_bottom.bmp"));
        shot(SecondWindow, top ? Layout::BottomOnly : Layout::TopOnly, SecondFit(), false, base + (top ? "_bottom.bmp" : "_top.bmp"));
    }
    SDL_GL_MakeCurrent(MainWindow, Context);
}

Rect ScreenWindows::BottomRect() const
{
    // where DrawAll puts it now
    int w, h;
    SDL_GL_GetDrawableSize(BottomWindow(), &w, &h);
    if (!IsDetached())
        return Presenter::Place(Shape, w, h, TogetherLayout, Fit::Aspect, Swap).Bottom;
    return Presenter::Place(Shape, w, h, Layout::BottomOnly, FitBottom).Bottom;
}

void ScreenWindows::SetDetached(bool detached)
{
    if (detached == IsDetachedMode)
        return;
    // the main window's place in the mode it leaves
    Track(MainWindow);
    if (detached)
    {
        // the second window first: when it doesn't open, nothing changes (no setting stored, the main window left as it is)
        IsDetachedMode = true;
        if (!OpenSecond())
        {
            IsDetachedMode = false;
            printf("screens: still together (the second window didn't open)\n");
            return;
        }
    }
    else
    {
        CloseSecond();
        IsDetachedMode = false;
    }
    Store->Set("desktop_screens", picojson::value(std::string(detached ? "detached" : "together")));
    // the main window takes the new mode's place, unless it's fullscreen (it gets it when it leaves) or maximized
    Uint32 flags = SDL_GetWindowFlags(MainWindow);
    if (!(flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED)))
    {
        PlaceWindow(MainWindow, MainSlot(), !Opts.MainOverride);
        MarkApplied(MainWindow, MainSlot());
    }
    MainRects = SecondRects = {};
    UpdateTitles(Stats);
    printf("screens: %s\n", IsDetachedMode ? "detached" : "together");
    SavePlaces();
}

void ScreenWindows::ToggleSwap()
{
    Swap = !Swap;
    Store->Set("desktop_swap_screens", picojson::value(Swap));
    MainRects = SecondRects = {};
    UpdateTitles(Stats);
    printf("swap screens: %s\n", Swap ? "on" : "off");
}

void ScreenWindows::NextLayout()
{
    TogetherLayout = (Layout)(((int)TogetherLayout + 1) % (int)Layout::Count);
    Store->Set("desktop_layout", picojson::value(std::string(kLayoutNames[(int)TogetherLayout])));
    printf("layout: %s\n", kLayoutNames[(int)TogetherLayout]);
}

void ScreenWindows::CycleFit(Uint32 windowId)
{
    if (!IsDetached())
    {
        printf("fit: only detached screens have one (F9)\n");
        return;
    }
    bool second = windowId && windowId == SDL_GetWindowID(SecondWindow);
    bool top = second ? !MainShowsTop() : MainShowsTop();
    Fit& fit = top ? FitTop : FitBottom;
    fit = (Fit)(((int)fit + 1) % (int)Fit::Count);
    Store->Set(top ? "desktop_fit_top" : "desktop_fit_bottom", picojson::value(std::string(FitName(fit))));
    printf("fit %s: %s\n", top ? "top" : "bottom", FitName(fit));
}

void ScreenWindows::OnWindowEvent(const SDL_WindowEvent& event, bool& quit)
{
    SDL_Window* window = SDL_GetWindowFromID(event.windowID);
    if (!window || (window != MainWindow && window != SecondWindow))
        return;
    switch (event.event)
    {
    case SDL_WINDOWEVENT_CLOSE:
        if (window == MainWindow)
            quit = true;
        else
            SetDetached(false);
        break;
    case SDL_WINDOWEVENT_MOVED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_MAXIMIZED:
    case SDL_WINDOWEVENT_RESTORED:
        Track(window);
        break;
    default: break;
    }
}

void ScreenWindows::SavePlaces()
{
    if (!Store || (Opts.Hidden && !Opts.SaveHiddenPlaces))
        return;
    Track(MainWindow);
    Track(SecondWindow);
    // only places from the settings or that the user changed (WindowPlace::Keep), never the command line's
    if (!Opts.MainOverride)
    {
        if (MainTogetherPlace.Keep)
            Store->SetStored("desktop_window", PlaceJson(MainTogetherPlace, FullscreenName(MainFull)));
        if (MainDetachedPlace.Keep)
            Store->SetStored("desktop_window_detached", PlaceJson(MainDetachedPlace, nullptr));
    }
    if (!Opts.SecondOverride && SecondPlace.Keep)
        Store->SetStored("desktop_window2", PlaceJson(SecondPlace, SecondFull ? "borderless" : "off"));
    Store->Save();
}

SDL_HitTestResult ScreenWindows::HitTest(SDL_Window* window, const SDL_Point* point, void* data)
{
    // the second window without a title bar: its edges resize it, and the black bars beside the screen (or anywhere with Alt held) move it
    auto* self = static_cast<ScreenWindows*>(data);
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
        return SDL_HITTEST_NORMAL;
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    const int edge = 6;
    bool left = point->x < edge, right = point->x >= w - edge, top = point->y < edge, bottom = point->y >= h - edge;
    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (SDL_GetModState() & KMOD_ALT)
        return SDL_HITTEST_DRAGGABLE;
    // the rects are in drawable pixels, the point in window ones
    int dw, dh;
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    float px = point->x * (float)dw / std::max(w, 1), py = point->y * (float)dh / std::max(h, 1);
    const ScreenRects& rects = self->SecondRects;
    const Rect& r = rects.Top.W > 0 ? rects.Top : rects.Bottom;
    if (r.W > 0 && (px < r.X || px >= r.X + r.W || py < r.Y || py >= r.Y + r.H))
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

}
