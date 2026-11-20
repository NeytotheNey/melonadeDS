// melonadeDS for PC: runs a ROM with the same core and OpenGL/Compute renderers as the Android app,
// in an SDL2 window with a GLES 3.2 context, for testing on a desktop. See desktop/README.md
#include <SDL.h>
#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "Args.h"
#include "SPI_Firmware.h"
#include "GPU.h"
#include "GPU_Shadow2D.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"
#include "GPU3D_Compute.h"
#include "OpenGLSupport.h"
#include "Platform.h"
#include "Savestate.h"
#include "net/Net.h"

// the app's shared native code (app/src/main/cpp/SharedNative.cmake)
#include "FrontendContext.h"
#include "MelonInstance.h"
#include "gamestate/GameState.h"
#include "renderer/FrameQueue.h"
#include "renderer/GpuFence.h"
#include "renderer/ScreenshotRenderer.h"
#include "retroachievements/RetroAchievementsManager.h"

#include "DesktopEventMessenger.h"
#include "EmulatorThread.h"
#include "Frontend.h"
#include "Presenter.h"
#include "RunOptions.h"
#include "ScreenWindows.h"
#include "Settings.h"

using namespace melonDS;
namespace fs = std::filesystem;

// ask hybrid-GPU laptops for the discrete GPU
extern "C"
{
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

void* PcGl_GetProcAddress(const char* name)
{
    return SDL_GL_GetProcAddress(name);
}

namespace
{

// PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION, which older MinGW headers don't have
constexpr ULONG kIgnoreTimerResolution = 0x4;

enum class RendererType { Software, OpenGL, Compute };

const char* RendererName(RendererType type)
{
    switch (type)
    {
    case RendererType::OpenGL: return "OpenGL";
    case RendererType::Compute: return "Compute";
    default: return "Software";
    }
}

struct Options
{
    std::string RomPath;
    std::string GbaRomPath;
    std::string BiosDir;       // bios9.bin, bios7.bin and firmware.bin; FreeBIOS and a generated firmware without
    std::string SavePath;      // the ROM's path with .sav by default
    std::string ScreenshotPath;
    std::string WindowShotPath;
    bool WindowShotLinear = false; // the window shot drawn with linear filtering, whatever --linear says
    std::string LogPath;
    std::string CacheDir;      // the Compute program cache, next to the exe by default
    RendererType Renderer = RendererType::Compute;
    int Scale = 4;
    int WideWidth = 256;
    int WideMargin = 0;        // WideMelon camera stage 2: 3D rows above and below the top screen
    // both off: in this core the x86-64 JIT runs slower than the interpreter, with or without
    // fast memory (see README), where the app runs the ARM64 one with fast memory
    bool Jit = false;
    bool FastMem = false;
    int LdmDirect = -1;        // debug.melonade.ldmdirect (ARMJIT::SetDirectBlockTransfers), -1 keeps the core's default
    bool BetterPolygons = false;
    bool PreciseDepth = false; // GPU::PreciseDepth, the app's "Precise depth" setting
    bool HighResCoordinates = true;
    bool ColorDither = false;
    bool HiresCapture = false;
    int Filter2D = 0;          // GPU::Filter2D: 0 off, 1 on, 2 the 2x map only, 3 identity
    int Gpu2D = 0;             // the GPU 2D shadow renderer (GPU::SetShadow2D): 0 off, 1, 2 all, 3 all + log, 4 check
    int Gpu2DWide = 0;         // debug.melonade.gpu2dwide: 1 lets every layer draw past the edge
    int Gpu2DAffine = 0;       // debug.melonade.gpu2daffine: 1-2 draw rotated BGs with the plain composite, -1 lifts the 6x cap
    bool Threaded2D = true;
    bool ThreadedSoft3D = true;
    bool HalfCamera = false;
    int HalfCameraMode = 0;    // debug.melonade.halfcam (GPU3D::SetHalfCamera), replaces --smooth30
    bool BootScreen = false;
    bool FixedTime = false;    // the RTC starts at 2026-01-01 12:00 instead of now
    bool VSync = false;
    bool Unthrottled = false;
    bool Audio = true;
    bool Linear = false;
    bool Hidden = false;
    bool GpuSync = false;      // glFinish after each frame, so the work time includes the GPU's
    bool GameState = false;    // print the app's game state scene (app/src/main/cpp/gamestate) when it changes
    bool WideBattles = true;   // the app's WideMelon Gen 4 battles (GameState::applyWideBattle), with --wide on gl/compute
    int WindowScale = 2;
    u32 Frames = 0;            // 0 runs until the window is closed
    std::vector<std::string> Sets;
    bool WideDrops = false;    // the WIDEDROPS log line (GPU3D::SetWideDropLogging)
    std::string StatePath;     // a savestate (.mln/.ml1...) loaded after the reset
    using Press = Frontend::KeyPress;
    std::vector<Press> Presses; // --press: keys held over frame ranges, for scripted runs
    struct At { u32 Frame; std::string Action; };
    std::vector<At> Ats;       // --at: a reset, a state load or save, a switch change or a hotkey before a frame
    // --touch: the mouse held at a point of the window that shows the bottom screen over frame ranges, for scripted runs
    struct Touch { u32 From, To; float X, Y; };
    std::vector<Touch> Touches;

    std::string ProfileDir;    // --profile; see Frontend::ResolveProfileDir
    // the settings the command line gives (--renderer, --scale, --setting, ...), for this run only and never written. The fields above
    // that have a setting (Renderer, Scale, Linear, Audio, VSync) are filled from the settings once they're read (ApplySettings)
    std::vector<std::pair<std::string, picojson::value>> SettingOverrides;
    std::optional<Frontend::WindowPlace> Window, Window2; // --window, --window2
    bool SaveHiddenPlaces = false;
};

void Usage()
{
    printf(
        "usage: melonade-desktop [options] game.nds\n"
        "\n"
        "  --renderer soft|gl|compute  3D renderer (default compute)\n"
        "  --scale N                   internal resolution for gl/compute, 1-16 (default 4)\n"
        "  --wide W                    WideMelon top screen width, even, 256-768 (default 256, off)\n"
        "  --wide-margin M             with --wide, M more 3D rows above and below the top screen (0-128)\n"
        "  --better-polygons           OpenGL: better polygon splitting\n"
        "  --precise-depth             3D depth at full precision, not the DS's 0x200 steps (GPU::PreciseDepth)\n"
        "  --no-high-res-coords        Compute: native-resolution vertex coordinates\n"
        "  --dither                    Compute: smooth gradients (color dithering)\n"
        "  --hires-capture             upscaled display capture (gl/compute)\n"
        "  --filter2d                  MMPX-filtered 2D (gl/compute, scale 2+)\n"
        "  --filter2d-mode N           filtered 2D as debug.melonade.filter2d: 2 the 2x map only, 3 identity\n"
        "  --gpu2d N                   GPU 2D shadow renderer: 0 off, 1 lines that gain, 2 all, 3 all with the\n"
        "                              GPU2D log line, 4 check (gl/compute)\n"
        "  --gpu2d-wide                with --gpu2d, every 2D layer draws past the edge (debug.melonade.gpu2dwide 1)\n"
        "  --gpu2d-affine N            with --gpu2d, rotated BGs drawn with the plain composite: 1 with the affine\n"
        "                              stack still written, 2 without it, -1 lifts the 6x cap on sharp rotated\n"
        "                              BGs (debug.melonade.gpu2daffine)\n"
        "  --smooth30                  gl/compute: in-between frames for 30 fps games (half camera)\n"
        "  --halfcam N                 debug.melonade.halfcam N, 1-5 (3 and up log HALFCAM lines), replaces --smooth30\n"
        "  --no-threaded2d             2D on the emulator thread\n"
        "  --no-threaded-soft          software 3D on the emulator thread\n"
        "  --jit                       x86-64 JIT (off by default: slower than the interpreter here)\n"
        "  --fastmem                   JIT with fast memory, as the app runs it\n"
        "  --ldmdirect 0|1             JIT: slow block transfers in one plain region straight in host memory (default 1)\n"
        "  --bios DIR                  official bios9.bin, bios7.bin (and firmware.bin) in DIR\n"
        "  --gba ROM                   GBA ROM for slot 2\n"
        "  --sav FILE                  save file (default: the ROM's path with .sav)\n"
        "  --state FILE                load a savestate after the reset; it replaces the save in\n"
        "                              memory, which gets flushed to --sav, so point that at a copy\n"
        "  --press F[-L]:KEYS          hold keys (a b x y l r start select up down left right, joined\n"
        "                              by +) over frames F..L (6 frames without L), for scripted runs\n"
        "  --boot-screen               boot through the firmware instead of directly\n"
        "  --fixed-time                the RTC starts at 2026-01-01 12:00, for runs to compare\n"
        "  --set NAME=VALUE            a core debug switch, e.g. --set LateBlit=0 (see --list-sets)\n"
        "  --at F:reset|state|save=FILE|scale=N|renderer=R|shot=FILE|key=KEY|NAME=VALUE\n"
        "                              before frame F: a reset, the --state loaded again, a savestate\n"
        "                              written to FILE, the scale or renderer (soft, gl, compute) changed, the last\n"
        "                              frame saved as FILE.bmp (as --screenshot), a hotkey pressed (key=F9,\n"
        "                              key=ctrl+r: what it changes is saved as when typed), or a --set\n"
        "  --touch F[-L]:X,Y           touch at X,Y in the drawable pixels of the window that shows the bottom\n"
        "                              screen over frames F..L (6 frames without L); off the screen does nothing\n"
        "  --list-sets                 list the switches --set knows\n"
        "  --widedrops                 log a WIDEDROPS line once a second (WideMelon's dropped polygons)\n"
        "\n"
        "  --profile DIR               settings.json in DIR instead of %%APPDATA%%\\melonadeDS (or the exe's folder with a\n"
        "                              portable.txt there); a scripted run (--hidden or --frames) without it\n"
        "                              gets a fresh temporary one, so it never reads or writes your settings\n"
        "  --setting KEY=VALUE         a setting for this run only, e.g. desktop_fit_bottom=integer (README lists them)\n"
        "  (the options that change a setting, --renderer, --scale, --linear, --vsync, --no-audio, --layout and\n"
        "  --detach, are for the run only too: none of them is written to settings.json)\n"
        "\n"
        "  --layout vertical|horizontal|top|bottom   the screens together in one window (with --detach: the\n"
        "                              layout for when they're together again)\n"
        "  --detach                    the screens detached: the top and bottom screens in two windows\n"
        "  --window W,H[,X,Y]          the main window's size in pixels (and desktop position) for this run\n"
        "  --window2 W,H[,X,Y]         the same for the second window when detached\n"
        "  --window-scale N            default window size in DS screens (default 2), times the monitor's\n"
        "                              scale (150%%: 3 screens) unless --hidden\n"
        "  --linear                    linear filtering when drawing to the window\n"
        "  --vsync                     wait for vsync on swap (pacing stays at the DS's 59.83 Hz)\n"
        "  --unthrottled               run as fast as possible (benchmarks)\n"
        "  --no-audio\n"
        "  --frames N                  quit after N frames (prints timing stats)\n"
        "  --game-state                print the app's Pokemon game state scene whenever it changes, and the\n"
        "                              GPU 2D shadow's mode, which a Gen 4 game's battles turn on and off\n"
        "  --no-wide-battles           with --wide, Gen 4 Pokemon battles stop at the edge as in other games\n"
        "  --screenshot FILE.bmp       save the last frame at the internal resolution when quitting\n"
        "  --window-shot FILE.bmp      save the window as drawn when quitting (with --frames); detached: the\n"
        "                              windows as FILE_top.bmp and FILE_bottom.bmp, for the screen each shows\n"
        "  --window-shot-linear        draw the window shot with linear filtering (the run keeps --linear's)\n"
        "  --hidden                    no visible window (with --frames); never fullscreen, saves no window places\n"
        "  --save-window-places        save the window places with --hidden too (tests)\n"
        "  --log FILE                  core log file (default melonade.log next to the exe)\n"
        "  --cache-dir DIR             Compute program cache directory\n"
        "  --verbose                   debug log lines on the console too\n"
        "\n"
        "keys: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Right Shift = Select\n"
        "      mouse on the bottom screen = touch, Tab (hold) = fast forward, P = pause, Ctrl+R = reset\n"
        "      F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up, F2 = layout, F8 = linear filter\n"
        "      F9 = screens together/detached, F1 = swap screens, F10 = fit of the focused detached window\n"
        "      (aspect, integer, stretch), F11 = fullscreen of the focused window, F12 = screenshot, Esc = quit\n"
        "      game controllers work too (Nintendo layout: east = A, south = B)\n");
}

using Frontend::ApplySet;
using Frontend::ReadFile;

bool ParseArgs(int argc, char** argv, Options& opts)
{
    bool layoutGiven = false, detachGiven = false;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto next = [&]() -> std::string
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "%s needs a value\n", arg.c_str());
                exit(1);
            }
            return argv[++i];
        };

        // a setting for this run (see Options::SettingOverrides)
        auto setting = [&](const char* key, const picojson::value& value) { opts.SettingOverrides.emplace_back(key, value); };
        auto text = [](const char* value) { return picojson::value(std::string(value)); };

        if (arg == "--renderer")
        {
            std::string r = next();
            if (r == "soft" || r == "software") setting("video_renderer", text("software"));
            else if (r == "gl" || r == "opengl") setting("video_renderer", text("opengl"));
            else if (r == "compute") setting("video_renderer", text("compute"));
            else { fprintf(stderr, "unknown renderer %s\n", r.c_str()); return false; }
        }
        else if (arg == "--scale") setting("video_internal_resolution", text(std::to_string(std::clamp(atoi(next().c_str()), 1, 16)).c_str()));
        else if (arg == "--setting")
        {
            std::string spec = next();
            size_t eq = spec.find('=');
            if (eq == std::string::npos || eq == 0) { fprintf(stderr, "bad --setting %s (KEY=VALUE)\n", spec.c_str()); return false; }
            opts.SettingOverrides.emplace_back(spec.substr(0, eq), Frontend::Settings::ParseValue(spec.substr(eq + 1)));
        }
        else if (arg == "--profile") opts.ProfileDir = next();
        else if (arg == "--detach") { setting("desktop_screens", text("detached")); detachGiven = true; }
        else if (arg == "--window" || arg == "--window2")
        {
            std::string spec = next();
            Frontend::WindowPlace place;
            if (!Frontend::ParseWindowSpec(spec, place)) { fprintf(stderr, "bad %s %s (W,H or W,H,X,Y)\n", arg.c_str(), spec.c_str()); return false; }
            (arg == "--window" ? opts.Window : opts.Window2) = place;
        }
        else if (arg == "--save-window-places") opts.SaveHiddenPlaces = true;
        else if (arg == "--touch")
        {
            std::string spec = next();
            Options::Touch touch {};
            unsigned from = 0, to = 0;
            float x = 0, y = 0;
            if (sscanf(spec.c_str(), "%u-%u:%f,%f", &from, &to, &x, &y) == 4) {}
            else if (sscanf(spec.c_str(), "%u:%f,%f", &from, &x, &y) == 3) to = from + 5;
            else { fprintf(stderr, "bad --touch %s (F[-L]:X,Y)\n", spec.c_str()); return false; }
            touch = {from, to, x, y};
            if (to < from) { fprintf(stderr, "bad --touch %s\n", spec.c_str()); return false; }
            opts.Touches.push_back(touch);
        }
        else if (arg == "--wide")
        {
            opts.WideWidth = atoi(next().c_str());
            if (opts.WideWidth < 256 || opts.WideWidth > 768 || (opts.WideWidth & 1))
            {
                fprintf(stderr, "--wide needs an even width 256-768\n");
                return false;
            }
        }
        else if (arg == "--wide-margin") opts.WideMargin = std::clamp(atoi(next().c_str()), 0, 128);
        else if (arg == "--better-polygons") opts.BetterPolygons = true;
        else if (arg == "--precise-depth") opts.PreciseDepth = true;
        else if (arg == "--no-high-res-coords") opts.HighResCoordinates = false;
        else if (arg == "--dither") opts.ColorDither = true;
        else if (arg == "--hires-capture") opts.HiresCapture = true;
        else if (arg == "--filter2d") opts.Filter2D = 1;
        else if (arg == "--filter2d-mode") opts.Filter2D = std::clamp(atoi(next().c_str()), 0, 3);
        else if (arg == "--gpu2d") opts.Gpu2D = std::clamp(atoi(next().c_str()), 0, 4);
        else if (arg == "--gpu2d-wide") opts.Gpu2DWide = 1;
        else if (arg == "--gpu2d-affine") opts.Gpu2DAffine = std::clamp(atoi(next().c_str()), -1, 2);
        else if (arg == "--smooth30") opts.HalfCamera = true;
        else if (arg == "--halfcam") opts.HalfCameraMode = std::clamp(atoi(next().c_str()), 0, 5);
        else if (arg == "--no-threaded2d") opts.Threaded2D = false;
        else if (arg == "--no-threaded-soft") opts.ThreadedSoft3D = false;
        else if (arg == "--jit") opts.Jit = true;
        else if (arg == "--no-jit") opts.Jit = false;
        else if (arg == "--fastmem") opts.Jit = opts.FastMem = true;
        else if (arg == "--ldmdirect") opts.LdmDirect = atoi(next().c_str()) ? 1 : 0;
        else if (arg == "--bios") opts.BiosDir = next();
        else if (arg == "--gba") opts.GbaRomPath = next();
        else if (arg == "--sav") opts.SavePath = next();
        else if (arg == "--state") opts.StatePath = next();
        else if (arg == "--press")
        {
            Options::Press press;
            std::string spec = next();
            if (!Frontend::ParsePress(spec, press)) { fprintf(stderr, "bad --press %s\n", spec.c_str()); return false; }
            opts.Presses.push_back(press);
        }
        else if (arg == "--boot-screen") opts.BootScreen = true;
        else if (arg == "--fixed-time") opts.FixedTime = true;
        else if (arg == "--set") opts.Sets.push_back(next());
        else if (arg == "--widedrops") opts.WideDrops = true;
        else if (arg == "--at")
        {
            std::string spec = next();
            size_t colon = spec.find(':');
            if (colon == std::string::npos || colon == 0) { fprintf(stderr, "bad --at %s\n", spec.c_str()); return false; }
            opts.Ats.push_back({(u32)strtoul(spec.c_str(), nullptr, 10), spec.substr(colon + 1)});
        }
        else if (arg == "--list-sets")
        {
            Frontend::ListSets();
            exit(0);
        }
        else if (arg == "--layout")
        {
            std::string l = next();
            if (l != "vertical" && l != "horizontal" && l != "top" && l != "bottom") { fprintf(stderr, "unknown layout %s\n", l.c_str()); return false; }
            setting("desktop_layout", text(l.c_str()));
            layoutGiven = true;
        }
        else if (arg == "--window-scale") opts.WindowScale = std::clamp(atoi(next().c_str()), 1, 8);
        else if (arg == "--linear") setting("video_filtering", text("linear"));
        else if (arg == "--vsync") setting("desktop_vsync", picojson::value(true));
        else if (arg == "--unthrottled") opts.Unthrottled = true;
        else if (arg == "--no-audio") setting("sound_enabled", picojson::value(false));
        else if (arg == "--frames") opts.Frames = strtoul(next().c_str(), nullptr, 0);
        else if (arg == "--game-state") opts.GameState = true;
        else if (arg == "--no-wide-battles") opts.WideBattles = false;
        else if (arg == "--screenshot") opts.ScreenshotPath = next();
        else if (arg == "--window-shot") opts.WindowShotPath = next();
        else if (arg == "--window-shot-linear") opts.WindowShotLinear = true;
        else if (arg == "--hidden") opts.Hidden = true;
        else if (arg == "--gpu-sync") opts.GpuSync = true;
        else if (arg == "--log") opts.LogPath = next();
        else if (arg == "--cache-dir") opts.CacheDir = next();
        else if (arg == "--verbose") Frontend::VerboseLog = true;
        else if (arg == "-h" || arg == "--help") { Usage(); exit(0); }
        else if (arg[0] == '-') { fprintf(stderr, "unknown option %s\n", arg.c_str()); return false; }
        else opts.RomPath = arg;
    }
    // a --layout is the screens together, unless --detach asks for them detached
    if (layoutGiven && !detachGiven)
        opts.SettingOverrides.emplace_back("desktop_screens", picojson::value(std::string("together")));
    return !opts.RomPath.empty();
}

// the options that are settings, from the settings (with the command line's overrides in them). The desktop's defaults stay the test
// frontend's (Compute at 4x), not the app's
void ApplySettings(Options& opts, const Frontend::Settings& settings)
{
    std::string renderer = settings.GetString("video_renderer", "compute");
    opts.Renderer = renderer == "software" ? RendererType::Software : renderer == "opengl" ? RendererType::OpenGL : RendererType::Compute;
    opts.Scale = std::clamp(settings.GetInt("video_internal_resolution", 4), 1, 16);
    opts.Linear = settings.GetString("video_filtering", "none") == "linear";
    opts.Audio = settings.GetBool("sound_enabled", true);
    opts.VSync = settings.GetBool("desktop_vsync", false);
}

const char* RendererSettingValue(RendererType type)
{
    switch (type)
    {
    case RendererType::OpenGL: return "opengl";
    case RendererType::Compute: return "compute";
    default: return "software";
    }
}

// the last --set NAME=VALUE for [name], or [fallback] without one
int SetValue(const std::vector<std::string>& sets, const char* name, int fallback)
{
    std::string prefix = std::string(name) + "=";
    for (const std::string& set : sets)
    {
        if (set.rfind(prefix, 0) == 0)
            fallback = (int)strtol(set.c_str() + prefix.size(), nullptr, 0);
    }
    return fallback;
}

MelonDSAndroid::Renderer SharedRenderer(RendererType type)
{
    switch (type)
    {
    case RendererType::OpenGL: return MelonDSAndroid::Renderer::OpenGl;
    case RendererType::Compute: return MelonDSAndroid::Renderer::Compute;
    default: return MelonDSAndroid::Renderer::Software;
    }
}

RendererType DesktopRenderer(MelonDSAndroid::Renderer renderer)
{
    switch (renderer)
    {
    case MelonDSAndroid::Renderer::OpenGl: return RendererType::OpenGL;
    case MelonDSAndroid::Renderer::Compute: return RendererType::Compute;
    default: return RendererType::Software;
    }
}

// the command line as the shared code's settings (EmulatorConfiguration, which MelonInstance::updateRenderer applies), for the renderer and
// scale the run has now. what has no setting in the app goes through the switches (Switches) instead
std::shared_ptr<MelonDSAndroid::EmulatorConfiguration> MakeConfiguration(const Options& opts, RendererType renderer, int scale)
{
    using namespace MelonDSAndroid;
    static char empty[] = "";
    auto config = std::make_shared<EmulatorConfiguration>();
    config->userInternalFirmwareAndBios = false;
    config->dsBios7Path = config->dsBios9Path = config->dsFirmwarePath = empty;
    config->dsiBios7Path = config->dsiBios9Path = config->dsiFirmwarePath = config->dsiNandPath = empty;
    config->internalFilesDir = empty;
    config->fastForwardSpeedMultiplier = 1.0f;
    config->framePacingVsync = false;
    config->framePacingBuffer = false;
    config->framePacingSmooth30Fps = opts.HalfCamera;
    config->showBootScreen = opts.BootScreen;
    config->useJit = opts.Jit;
    config->syncRtcToDevice = false;
    config->consoleType = 0;
    // no interpolation and the full 16 bits, the SPU's defaults (only applied when the settings change during a run)
    config->audioSettings = AudioSettings {
        .soundEnabled = opts.Audio,
        .muteFastForwardAudio = false,
        .volume = 256,
        .audioInterpolation = (int)AudioInterpolation::None,
        .audioBitrate = (int)AudioBitDepth::_16Bit,
        .audioLatency = AudioLatency::Medium,
        .micSource = 0,
    };
    config->rewindEnabled = 0;
    config->rewindCaptureSpacingSeconds = 10;
    config->rewindLengthSeconds = 60;
    config->firmwareConfiguration = {};
    switch (renderer)
    {
    case RendererType::OpenGL:
    {
        auto settings = std::make_unique<OpenGlRenderSettings>();
        settings->betterPolygons = opts.BetterPolygons;
        settings->scale = scale;
        config->renderSettings = std::move(settings);
        break;
    }
    case RendererType::Compute:
    {
        auto settings = std::make_unique<ComputeRenderSettings>();
        settings->scale = scale;
        settings->highResCoordinates = opts.HighResCoordinates;
        settings->colorDithering = opts.ColorDither;
        config->renderSettings = std::move(settings);
        break;
    }
    default:
    {
        auto settings = std::make_unique<SoftwareRenderSettings>();
        settings->threadedRendering = opts.ThreadedSoft3D;
        config->renderSettings = std::move(settings);
        break;
    }
    }
    config->threaded2dRendering = opts.Threaded2D;
    config->hiresCapture = opts.HiresCapture;
    config->filter2d = opts.Filter2D != 0;
    config->sharperRotated2d = false;
    // --set PreciseDepth wins, as it did when it was applied after --precise-depth
    config->preciseDepth = SetValue(opts.Sets, "PreciseDepth", opts.PreciseDepth) != 0;
    // the core's default (GPU::GLEdgeMark 1); --set GLEdgeMark goes through the switches
    config->edgeMarking = true;
    config->glSeams = false;
    config->luaDrawingsInScreenshots = false;
    config->wideViewWidth = opts.WideWidth;
    config->wideViewMargin = opts.WideMargin;
    config->dsiSdCardSettings = {};
    config->dldiSdCardSettings = {};
    config->dsiSdCardSettings.imagePath = config->dsiSdCardSettings.folderPath = empty;
    config->dldiSdCardSettings.imagePath = config->dldiSdCardSettings.folderPath = empty;
    config->renderer = SharedRenderer(renderer);
    return config;
}

// what the app's debug builds read from the debug.melonade.* switches (DebugProperties::apply), from the command line: the core's switches
// are applied to [nds] (as DebugProperties::apply does, after the instance reset the console and before the game starts), and the ones
// over the settings come back for MelonInstance::setDebugOverrides
DebugProperties::Overrides ApplySwitches(const Options& opts, NDS& nds)
{
    for (const std::string& set : opts.Sets)
    {
        if (!Frontend::ApplySet(nds.GPU, set))
            fprintf(stderr, "unknown --set %s (see --list-sets)\n", set.c_str());
    }
    nds.GPU.GPU3D.SetWideDropLogging(opts.WideDrops);
    if (opts.LdmDirect >= 0)
        nds.JIT.SetDirectBlockTransfers(opts.LdmDirect == 1);

    DebugProperties::Overrides overrides;
    if (opts.HalfCameraMode > 0)
    {
        nds.GPU.GPU3D.SetHalfCamera(opts.HalfCameraMode);
        overrides.halfCamera = opts.HalfCameraMode;
    }
    // the mode stays for the run, through renderer and scale changes, as debug.melonade.gpu2d does
    if (opts.Gpu2D > 0)
    {
        nds.GPU.SetShadow2D(std::min(opts.Gpu2D, 2), opts.Gpu2D >= 3, opts.Gpu2D == 4, opts.Gpu2DWide);
        if (nds.GPU.Shadow2D)
            nds.GPU.Shadow2D->AffineDebug = opts.Gpu2DAffine;
        overrides.gpu2d = std::min(opts.Gpu2D, 2);
    }
    overrides.gpu2dWide = opts.Gpu2DWide;
    overrides.gpu2dAffine = opts.Gpu2DAffine;
    // the 2x-map-only and identity modes have no setting
    if (opts.Filter2D >= 2)
        overrides.filter2d = opts.Filter2D;
    // the settings updateRenderer writes on every renderer change, so a --set of them holds through those
    overrides.edgeMark = SetValue(opts.Sets, "GLEdgeMark", -1);
    overrides.glSeams = SetValue(opts.Sets, "GLSeams", -1);
    overrides.wideBattles = opts.WideBattles ? -1 : 0;
    return overrides;
}

void SetDateTime(const Options& opts, NDS& nds)
{
    // the same start every run, so two runs can be compared byte for byte; otherwise MelonInstance set the PC's time
    if (opts.FixedTime)
        nds.RTC.SetDateTime(2026, 1, 1, 12, 0, 0);
}

// an empty save file, as the app makes one for a new game, since MelonInstance only loads a ROM with one there
bool EnsureSaveFile(const std::string& path)
{
    std::error_code ec;
    if (fs::exists(path, ec))
        return true;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

bool LoadStateFile(const Options& opts, MelonDSAndroid::MelonInstance& emu, const std::string& path)
{
    std::vector<u8> data;
    if (!ReadFile(path, data) || data.empty())
    {
        fprintf(stderr, "can't read savestate %s\n", path.c_str());
        return false;
    }
    // MelonInstance's load backs up the state before and goes back to it if the load fails partway
    Savestate state(data.data(), (u32)data.size(), false);
    bool loaded = emu.loadState(&state);
    if (!loaded)
    {
        // a state without the RetroAchievements section MelonInstance::saveState writes first (the app's and this frontend's states have
        // it), as the desktop saved them before the shared code: the core's part alone
        Savestate raw(data.data(), (u32)data.size(), false);
        loaded = !raw.Error && emu.getNds()->DoSavestate(&raw) && !raw.Error;
        if (loaded)
            printf("state: %s has no RetroAchievements section, loaded the core's part\n", path.c_str());
    }
    if (!loaded)
    {
        fprintf(stderr, "savestate %s didn't load\n", path.c_str());
        return false;
    }
    SetDateTime(opts, *emu.getNds());
    printf("state: %s\n", path.c_str());
    return true;
}

bool SaveStateFile(MelonDSAndroid::MelonInstance& emu, const std::string& path)
{
    Savestate state;
    if (state.Error || !emu.saveState(&state) || state.Error)
    {
        fprintf(stderr, "can't make a savestate\n");
        return false;
    }
    FILE* f = fopen(path.c_str(), "wb");
    bool ok = f && fwrite(state.Buffer(), 1, state.Length(), f) == state.Length();
    if (f)
        fclose(f);
    if (!ok)
    {
        fprintf(stderr, "can't write savestate %s\n", path.c_str());
        return false;
    }
    printf("saved state: %s (%u bytes)\n", path.c_str(), state.Length());
    return true;
}

// the renderer's scale, which drops below --scale when that doesn't fit the GPU's limits; emulator thread
int ActiveScale(MelonDSAndroid::MelonInstance& emu)
{
    Renderer3D& renderer = emu.getNds()->GPU.GetRenderer3D();
    switch (emu.getCurrentRenderer())
    {
    case MelonDSAndroid::Renderer::OpenGl: return static_cast<GLRenderer&>(renderer).GetScaleFactor();
    case MelonDSAndroid::Renderer::Compute: return static_cast<ComputeRenderer&>(renderer).GetScaleFactor();
    default: return 1;
    }
}

u32 KeyBit(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_x: return 1 << 0;      // A
    case SDLK_z: return 1 << 1;      // B
    case SDLK_RSHIFT: return 1 << 2; // Select
    case SDLK_BACKSPACE: return 1 << 2;
    case SDLK_RETURN: return 1 << 3; // Start
    case SDLK_RIGHT: return 1 << 4;
    case SDLK_LEFT: return 1 << 5;
    case SDLK_UP: return 1 << 6;
    case SDLK_DOWN: return 1 << 7;
    case SDLK_w: return 1 << 8;      // R
    case SDLK_q: return 1 << 9;      // L
    case SDLK_s: return 1 << 10;     // X
    case SDLK_a: return 1 << 11;     // Y
    default: return 0;
    }
}

u32 ControllerBit(int button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_B: return 1 << 0;  // east = A
    case SDL_CONTROLLER_BUTTON_A: return 1 << 1;  // south = B
    case SDL_CONTROLLER_BUTTON_BACK: return 1 << 2;
    case SDL_CONTROLLER_BUTTON_START: return 1 << 3;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return 1 << 4;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return 1 << 5;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return 1 << 6;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return 1 << 7;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return 1 << 8;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return 1 << 9;
    case SDL_CONTROLLER_BUTTON_Y: return 1 << 10; // north = X
    case SDL_CONTROLLER_BUTTON_X: return 1 << 11; // west = Y
    default: return 0;
    }
}

std::string ExeDir()
{
    char* base = SDL_GetBasePath();
    std::string dir = base ? base : "./";
    SDL_free(base);
    return dir;
}

// frame times over a stretch of frames, for the title bar and the --frames summary
struct FrameStats
{
    std::vector<double> WorkMs;
    void Add(double ms) { WorkMs.push_back(ms); }
    void Print(const char* label, double wallSeconds) const
    {
        if (WorkMs.empty())
            return;
        std::vector<double> sorted = WorkMs;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double ms : sorted) sum += ms;
        auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, (size_t)(p * sorted.size()))]; };
        printf("%s: %zu frames in %.2f s (%.1f fps), work per frame avg %.2f ms, p50 %.2f, p99 %.2f, max %.2f\n",
            label, sorted.size(), wallSeconds, sorted.size() / wallSeconds, sum / sorted.size(), pct(0.5), pct(0.99), sorted.back());
    }
};

// what the emulator thread shows in the title, once a second
struct TitleStats
{
    std::mutex Lock;
    FrameStats Second;
    RendererType Renderer = RendererType::Software;
    int Scale = 1;
};

// The frame to show or read on the UI thread, ready for its context the way the app's presentFrame (MelonDSAndroidJNI.cpp) makes it: its
// last present fence dropped and a GPU wait on its render fence queued. ReleaseFrame() puts a new present fence on it after the draw or read,
// which the emulator thread waits on before it renders into the frame again
Frame* AcquireFrame(MelonDSAndroid::MelonInstance& emu, std::optional<std::chrono::steady_clock::time_point> deadline)
{
    Frame* frame = emu.getPresentationFrame(deadline, 0, 0, 0);
    if (!frame || !frame->frameTexture)
        return nullptr;
    return frame;
}

void PrepareFrame(Frame* frame)
{
    if (frame->presentFence)
    {
        destroyGpuFence(GPU_FENCE_NO_DISPLAY, frame->presentFence);
        frame->presentFence = 0;
    }
    if (frame->renderFence)
        waitGpuFence(GPU_FENCE_NO_DISPLAY, frame->renderFence);
}

void ReleaseFrame(Frame* frame)
{
    frame->presentFence = createGpuFence(GPU_FENCE_NO_DISPLAY);
    // so the emulator's context sees the fence
    glFlush();
}

}

int main(int argc, char** argv)
{
    Options opts;
    if (!ParseArgs(argc, argv, opts))
    {
        Usage();
        return 1;
    }

    // Windows 11 slows down processes whose window isn't in front (EcoQoS) and ignores their timer
    // requests, which costs 2/3 of the frame rate: opt out of both, then ask for 1 ms sleeps for
    // the frame pacing instead of the default 15.6 ms
    PROCESS_POWER_THROTTLING_STATE throttling {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | kIgnoreTimerResolution;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
    timeBeginPeriod(1);
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0"); // the driver's own ES context (WGL), not ANGLE
    // per-monitor DPI aware (v2): windows are sized in real pixels on every monitor, and Windows doesn't blur them up on a scaled one.
    // A window keeps its size in pixels when it moves to a monitor with another scale
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    // the windows' close buttons are handled as events (the main window quits, the second one goes back to together)
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    std::string exeDir = ExeDir();
    Frontend::LogFile = fopen((opts.LogPath.empty() ? exeDir + "melonade.log" : opts.LogPath).c_str(), "w");

    // the profile and its settings, with the command line's settings on top for this run
    bool temporaryProfile = false;
    fs::path profileDir = Frontend::ResolveProfileDir(opts.ProfileDir, opts.Hidden || opts.Frames != 0, Frontend::PathFromUtf8(exeDir), temporaryProfile);
    if (profileDir.empty())
    {
        fprintf(stderr, "can't make the profile folder %s\n", opts.ProfileDir.empty() ? "(%APPDATA%\\melonadeDS)" : opts.ProfileDir.c_str());
        return 1;
    }
    // a temporary profile goes when the run ends, however it ends
    struct ProfileCleanup
    {
        fs::path Dir;
        ~ProfileCleanup() { std::error_code ec; if (!Dir.empty()) fs::remove_all(Dir, ec); }
    } profileCleanup {temporaryProfile ? profileDir : fs::path()};
    Frontend::Settings settings;
    settings.Load(profileDir);
    for (const auto& [key, value] : opts.SettingOverrides)
        settings.Override(key, value);
    ApplySettings(opts, settings);
    printf("profile: %s%s\n", Frontend::PathToUtf8(profileDir).c_str(), temporaryProfile ? " (temporary)" : "");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    // the main window, in its place for the mode the run starts in; with detached screens the second one opens once the context is made
    Frontend::ScreenWindows windows;
    Frontend::ScreenWindows::Options windowOptions;
    windowOptions.Hidden = opts.Hidden;
    windowOptions.VSync = opts.VSync;
    windowOptions.WindowScale = opts.WindowScale;
    windowOptions.TopW = opts.WideWidth;
    windowOptions.TopH = 192 + (opts.WideWidth != 256 ? 2 * opts.WideMargin : 0);
    windowOptions.MainOverride = opts.Window;
    windowOptions.SecondOverride = opts.Window2;
    windowOptions.SaveHiddenPlaces = opts.SaveHiddenPlaces;
    SDL_Window* window = windows.CreateMain(settings, windowOptions);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    // the UI thread's context, which presents
    SDL_GLContext uiContext = SDL_GL_CreateContext(window);
    if (!uiContext)
    {
        fprintf(stderr, "can't make a GLES 3.2 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, uiContext);
    int missing = PcGl_Load([](const char* name) { return SDL_GL_GetProcAddress(name); });
    printf("GL: %s | %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (missing)
        printf("GL: %d GLES 3.2 functions missing\n", missing);

    // the emulator thread's context, in the UI one's share group (frame textures and fences), on a hidden window of its own with the same
    // pixel format, so the two threads never share a window's device context
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_Window* emuWindow = SDL_CreateWindow("melonadeDS emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 16, 16,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext emuContext = emuWindow ? SDL_GL_CreateContext(emuWindow) : nullptr;
    if (!emuContext)
    {
        fprintf(stderr, "can't make the emulator's GLES 3.2 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    // SDL made the new context current here; it's the emulator thread's
    SDL_GL_MakeCurrent(emuWindow, nullptr);
    SDL_GL_MakeCurrent(window, uiContext);
    // the swap interval, the main window's fullscreen and the second window
    windows.Start(uiContext);

    Frontend::Presenter presenter;
    if (!presenter.Init())
    {
        fprintf(stderr, "can't set up the screen shader\n");
        return 1;
    }

    std::string cacheDir = opts.CacheDir.empty() ? exeDir + "cache" : opts.CacheDir;
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    OpenGL::SetShaderCachePath(cacheDir + "/compute_programs.bin");

    // What the shared code needs from this frontend (FrontendContext.h), before the instance is made
    auto messenger = std::make_shared<Frontend::DesktopEventMessenger>();
    MelonDSAndroid::frontendContext.internalFilesDir = exeDir;
    MelonDSAndroid::frontendContext.eventMessenger = messenger;
    MelonDSAndroid::RetroAchievements::RetroAchievementsManager::EventMessenger = messenger;
    // GPU::Comp1x: the presenter draws a screen at 1x unless it filters linearly, as the app's surfaces tell the emulator after presenting;
    // set before the first frame here, so a run's frames don't depend on when the UI thread first presents
    MelonDSAndroid::setHalvesAt1xSupported(!opts.Linear);
    // GPU::OutputCapDeband, which MelonInstance sets before each frame from what the presenter says (once it held for 30 frames, as on the
    // phone), so --set OutputCapDeband stands in for the app's deband through it
    MelonDSAndroid::setOutputDeband(SetValue(opts.Sets, "OutputCapDeband", 0) != 0);

    // The console, as before the shared code: FreeBIOS and a generated firmware unless --bios says otherwise
    auto args = std::make_unique<NDSArgs>();
#ifdef JIT_ENABLED
    // the app's JIT settings (block size 32), but fast memory only on request
    args->JIT = std::nullopt;
    if (opts.Jit)
    {
        JITArgs jit;
        jit.FastMemory = opts.FastMem;
        args->JIT = jit;
    }
#else
    args->JIT = std::nullopt;
#endif
    args->OutputSampleRate = 48000.0;
    bool haveFirmware = false;
    if (!Frontend::LoadBiosDir(opts.BiosDir, *args, haveFirmware))
        return 1;
    if (!haveFirmware)
        args->Firmware = Firmware(0);
    if (!haveFirmware && opts.BootScreen)
    {
        fprintf(stderr, "--boot-screen needs the official BIOS and firmware (--bios)\n");
        opts.BootScreen = false;
    }

    // the renderer and scale of the run, which hotkeys and --at change; emulator thread
    RendererType renderer = opts.Renderer;
    int scale = opts.Scale;
    std::vector<u32> screenshotBuffer(256 * 384);
    auto instance = std::make_unique<MelonDSAndroid::MelonInstance>(0, MakeConfiguration(opts, renderer, scale), std::move(args),
        std::make_shared<Net>(), std::make_unique<MelonDSAndroid::ScreenshotRenderer>(screenshotBuffer.data()), 0);
    instance->setDebugOverrides(ApplySwitches(opts, *instance->getNds()));

    if (opts.SavePath.empty())
        opts.SavePath = fs::path(opts.RomPath).replace_extension(".sav").string();
    std::vector<u8> sav;
    if (ReadFile(opts.SavePath, sav) && !sav.empty())
        printf("save: %s\n", opts.SavePath.c_str());
    if (!EnsureSaveFile(opts.SavePath) || !instance->loadRom(opts.RomPath, opts.SavePath))
    {
        fprintf(stderr, "can't load ROM %s with the save %s (not a DS ROM the core can parse, or a file it can't read or write)\n",
            opts.RomPath.c_str(), opts.SavePath.c_str());
        return 1;
    }
    if (!opts.GbaRomPath.empty())
    {
        std::string gbaSave = fs::path(opts.GbaRomPath).replace_extension(".sav").string();
        if (!EnsureSaveFile(gbaSave) || !instance->loadGbaRom(opts.GbaRomPath, gbaSave))
            fprintf(stderr, "%s isn't a GBA ROM the core can parse\n", opts.GbaRomPath.c_str());
    }
    MelonDSAndroid::MelonInstance* emu = instance.get();

    SDL_AudioDeviceID audio = 0;
    if (opts.Audio)
    {
        SDL_AudioSpec want {}, have {};
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audio)
            SDL_PauseAudioDevice(audio, 0);
        else
            fprintf(stderr, "no audio: %s\n", SDL_GetError());
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
            SDL_GameControllerOpen(i);
    }

    // The input the UI thread reads, which the emulator thread applies before each frame along with --press, so scripted runs are frame exact
    std::atomic<u32> keyMask = 0, padMask = 0, stickMask = 0;
    std::atomic_bool stopRequested = false; // the core asked to stop (SignalStop)
    std::atomic_bool stateFailed = false;   // an --at state load failed: the run stops
    std::atomic_bool startFailed = false;   // the --state didn't load: nothing ran
    std::atomic<u32> currentFrame = 0;
    bool touching = false, paused = false, running = true;
    bool linear = opts.Linear;
    // where the bottom screen is in the drawable of the window that shows it, for --touch on the emulator thread: as planned for the
    // first frame (from the run's top screen size), then as each present drew it
    std::mutex touchLock;
    Frontend::Rect touchRect;
    {
        Frontend::FrameView planned;
        bool wide = renderer != RendererType::Software && opts.WideWidth != 256;
        planned.TopWidth = wide ? opts.WideWidth : 256;
        planned.TopMargin = wide ? opts.WideMargin : 0;
        windows.SetFrameShape(planned);
        touchRect = windows.BottomRect();
    }
    bool scriptTouching = false; // emulator thread
    u32 presented = 0;
    FrameStats stats;
    TitleStats titleStats;
    std::string statsLabel = RendererName(renderer);
    auto runStart = std::chrono::steady_clock::now();
    auto secondStart = runStart;

    messenger->OnStop = [&](Platform::StopReason)
    {
        stopRequested = true;
        running = false;
    };

    // UI thread: saves the newest frame (the last one the emulator pushed) at the internal resolution
    auto screenshot = [&](const std::string& path)
    {
        std::vector<u8> pixels;
        int w = 0, h = 0;
        Frame* frame = AcquireFrame(*emu, std::nullopt);
        bool ok = false;
        if (frame)
        {
            PrepareFrame(frame);
            ok = presenter.ReadFrame(Frontend::FrameView::Of(*frame), pixels, w, h);
            ReleaseFrame(frame);
        }
        if (ok && Frontend::WriteBMP(path.c_str(), pixels, w, h))
            printf("screenshot %s (%dx%d)\n", path.c_str(), w, h);
        else
            fprintf(stderr, "screenshot %s failed\n", path.c_str());
    };

    // emulator thread: [task] on the UI thread, waited for
    auto runOnUiThread = [&](std::function<void()> task)
    {
        std::promise<void> done;
        messenger->Post([&] { task(); done.set_value(); });
        done.get_future().wait();
    };

    // emulator thread: a new configuration for the renderer and scale (applied at the next frame), and the renderer line printed after it
    bool printRenderer = true;
    auto updateConfiguration = [&](MelonDSAndroid::MelonInstance& instance)
    {
        instance.updateConfiguration(MakeConfiguration(opts, renderer, scale));
        printRenderer = true;
    };

    // UI thread: a hotkey pressed with [mod] in the window [windowId] (0: the main one); set below, also run by --at key=
    std::function<void(SDL_Keycode key, Uint16 mod, Uint32 windowId)> hotkey;

    Frontend::EmulatorThread emuThread;
    Frontend::EmulatorThread::Callbacks callbacks;
    callbacks.OnStart = [&](MelonDSAndroid::MelonInstance& instance)
    {
        SetDateTime(opts, *instance.getNds());
        if (!opts.StatePath.empty() && !LoadStateFile(opts, instance, opts.StatePath))
        {
            startFailed = true;
            return false;
        }
        return true;
    };
    int lastShadowMode = 0, lastScene = -2;
    std::vector<s16> audioBuf(2 * 1024);
    callbacks.BeforeFrame = [&](MelonDSAndroid::MelonInstance& instance, u32 frame)
    {
        if (stopRequested || (opts.Frames && frame >= opts.Frames))
            return false;

        // the keys held this frame, all of them sent every frame (the instance's pressKey and releaseKey), as the desktop set the whole mask
        // before the shared code: a reset (Ctrl+R, --at reset) releases every key in the core and a state load brings its own, behind the
        // instance's back, so sending only the changes would leave a key held through them released
        u32 mask = keyMask | padMask | stickMask | Frontend::PressMask(opts.Presses, frame);
        for (u32 bit = 0; bit < 12; bit++)
            (mask & (1u << bit)) ? instance.pressKey(bit) : instance.releaseKey(bit);

        // --touch: a point of the bottom screen's window, through the same mapping as the mouse
        if (!opts.Touches.empty())
        {
            bool held = false;
            for (const Options::Touch& touch : opts.Touches)
            {
                if (frame < touch.From || frame > touch.To)
                    continue;
                Frontend::Rect r;
                {
                    std::lock_guard guard(touchLock);
                    r = touchRect;
                }
                int tx = r.W > 0 ? (int)((touch.X - r.X) / r.W * 256) : -1, ty = r.H > 0 ? (int)((touch.Y - r.Y) / r.H * 192) : -1;
                if (tx < 0 || tx > 255 || ty < 0 || ty > 191)
                {
                    if (frame == touch.From)
                        printf("touch %.0f,%.0f at frame %u: off the bottom screen\n", touch.X, touch.Y, frame);
                    continue;
                }
                if (frame == touch.From)
                    printf("touch %.0f,%.0f at frame %u: DS %d,%d\n", touch.X, touch.Y, frame, tx, ty);
                instance.touchScreen((u16)tx, (u16)ty);
                held = true;
                break;
            }
            if (!held && scriptTouching)
                instance.releaseScreen();
            scriptTouching = held;
        }

        for (const Options::At& at : opts.Ats)
        {
            if (at.Frame != frame)
                continue;
            if (at.Action == "reset")
            {
                instance.reset();
                SetDateTime(opts, *instance.getNds());
            }
            else if (at.Action == "state")
            {
                if (opts.StatePath.empty())
                    fprintf(stderr, "--at %u:state: no state to load\n", at.Frame);
                else if (!LoadStateFile(opts, instance, opts.StatePath))
                {
                    fprintf(stderr, "--at %u:state: the state didn't load, stopping\n", at.Frame);
                    stateFailed = true;
                    return false;
                }
            }
            else if (at.Action.rfind("save=", 0) == 0)
                SaveStateFile(instance, at.Action.substr(5));
            else if (at.Action.rfind("scale=", 0) == 0)
            {
                scale = std::clamp(atoi(at.Action.c_str() + 6), 1, 16);
                updateConfiguration(instance);
            }
            else if (at.Action.rfind("shot=", 0) == 0)
            {
                std::string path = at.Action.substr(5);
                runOnUiThread([&] { screenshot(path); });
            }
            else if (at.Action.rfind("key=", 0) == 0)
            {
                // a hotkey as if it was typed in the main window: key=F9, key=ctrl+r, key=shift+f11
                std::string name = at.Action.substr(4);
                Uint16 mod = KMOD_NONE;
                for (;;)
                {
                    if (_strnicmp(name.c_str(), "ctrl+", 5) == 0) { mod |= KMOD_LCTRL; name.erase(0, 5); }
                    else if (_strnicmp(name.c_str(), "shift+", 6) == 0) { mod |= KMOD_LSHIFT; name.erase(0, 6); }
                    else if (_strnicmp(name.c_str(), "alt+", 4) == 0) { mod |= KMOD_LALT; name.erase(0, 4); }
                    else break;
                }
                SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
                if (key == SDLK_UNKNOWN)
                    fprintf(stderr, "--at %u:%s: unknown key\n", at.Frame, at.Action.c_str());
                else
                    runOnUiThread([&] { hotkey(key, mod, 0); });
            }
            else if (at.Action == "renderer=soft" || at.Action == "renderer=gl" || at.Action == "renderer=compute")
            {
                renderer = at.Action == "renderer=gl" ? RendererType::OpenGL
                         : at.Action == "renderer=compute" ? RendererType::Compute : RendererType::Software;
                updateConfiguration(instance);
            }
            else if (!ApplySet(instance.getNds()->GPU, at.Action))
                fprintf(stderr, "unknown --at %u:%s (see --list-sets)\n", at.Frame, at.Action.c_str());
            else if (at.Action.rfind("OutputCapDeband=", 0) == 0)
                MelonDSAndroid::setOutputDeband(SetValue({at.Action}, "OutputCapDeband", 0) != 0);
            printf("at frame %u: %s\n", at.Frame, at.Action.c_str());
        }
        return true;
    };
    callbacks.AfterFrame = [&](MelonDSAndroid::MelonInstance& instance, u32 frame, u32 lines, double workMs)
    {
        NDS& nds = *instance.getNds();
        RendererType current = DesktopRenderer(instance.getCurrentRenderer());
        if (printRenderer)
        {
            printf("renderer: %s, scale %d, width %d, margin %d\n", RendererName(current), ActiveScale(instance), opts.WideWidth, opts.WideMargin);
            printRenderer = false;
        }
        stats.Add(workMs);
        statsLabel = RendererName(current);
        {
            std::lock_guard guard(titleStats.Lock);
            titleStats.Second.Add(workMs);
            titleStats.Renderer = current;
            titleStats.Scale = ActiveScale(instance);
        }
        currentFrame = frame + 1;

        if (opts.GameState)
        {
            // MelonInstance::applyShadow2DMode turns the GPU 2D shadow on and off around a Gen 4 game's battles before the frame
            int mode = nds.GPU.Shadow2D ? nds.GPU.Shadow2D->Mode : 0;
            if (mode != lastShadowMode)
            {
                printf("frame %u: GPU 2D shadow mode %d\n", frame, mode);
                lastShadowMode = mode;
            }
            // Polled every frame, as the camera does
            int scene = instance.pollGameScene();
            if (scene != lastScene)
            {
                const char* name = MelonDSAndroid::GameState::getSceneName(scene);
                printf("frame %u: game state %s\n", frame, name ? name : "unknown");
                lastScene = scene;
            }
        }

        // the core's audio, dropped while it would only pile up (fast forward, a slow frame)
        int got;
        while ((got = instance.readAudioOutput(audioBuf.data(), (int)audioBuf.size() / 2)) > 0)
        {
            if (audio && !emuThread.IsFastForward() && !opts.Unthrottled && SDL_GetQueuedAudioSize(audio) < 48000 * 4 / 10)
                SDL_QueueAudio(audio, audioBuf.data(), got * 4);
        }
    };
    callbacks.OnStop = [&](MelonDSAndroid::MelonInstance&)
    {
        OpenGL::ShaderCacheStats cacheStats;
        OpenGL::SaveShaderCache(cacheStats);
    };

    emuThread.SetLimitFps(!opts.Unthrottled);
    emuThread.SetGpuSync(opts.GpuSync);
    emuThread.Start(emuWindow, emuContext, std::move(instance), std::move(callbacks));

    // the mouse at window point x,y of the window that shows the bottom screen
    auto touchAt = [&](int x, int y)
    {
        Frontend::Rect r = windows.BottomRect();
        if (r.W <= 0)
            return;
        // window points to drawable pixels, as the rect is in the latter
        SDL_Window* bottom = windows.BottomWindow();
        int ww, wh, dw, dh;
        SDL_GetWindowSize(bottom, &ww, &wh);
        SDL_GL_GetDrawableSize(bottom, &dw, &dh);
        float px = x * (float)dw / ww, py = y * (float)dh / wh;
        int tx = (int)((px - r.X) / r.W * 256), ty = (int)((py - r.Y) / r.H * 192);
        if (!touching && (tx < 0 || tx > 255 || ty < 0 || ty > 191))
            return;
        touching = true;
        emu->touchScreen((u16)std::clamp(tx, 0, 255), (u16)std::clamp(ty, 0, 191));
    };

    // --touch's rect again, after anything that moves the bottom screen: a present (a new frame shape), a window resize, a hotkey (F9,
    // F1, F2, F10), so a touch right after one uses the right window and rect. Stored even when the screen isn't shown (W 0: no touch)
    auto refreshTouchRect = [&]
    {
        Frontend::Rect r = windows.BottomRect();
        std::lock_guard guard(touchLock);
        touchRect = r;
    };

    // draws [frame] into the windows and shows them
    auto present = [&](Frame* frame)
    {
        PrepareFrame(frame);
        windows.Present(presenter, Frontend::FrameView::Of(*frame), linear, [&] { ReleaseFrame(frame); });
        presented++;
        refreshTouchRect();
    };

    u64 lastSequence = 0;
    bool redraw = false;
    hotkey = [&](SDL_Keycode key, Uint16 mod, Uint32 windowId)
    {
        switch (key)
        {
        case SDLK_ESCAPE: running = false; break;
        case SDLK_p:
            paused = !paused;
            paused ? emuThread.Pause() : emuThread.Resume();
            break;
        case SDLK_r:
            if (mod & KMOD_CTRL)
            {
                emuThread.Post([&](MelonDSAndroid::MelonInstance& instance)
                {
                    instance.reset();
                    SetDateTime(opts, *instance.getNds());
                    printf("reset\n");
                });
            }
            break;
        case SDLK_F5:
        case SDLK_F6:
        case SDLK_F7:
        {
            RendererType requested = key == SDLK_F5 ? RendererType::Software : key == SDLK_F6 ? RendererType::OpenGL : RendererType::Compute;
            settings.Set("video_renderer", picojson::value(std::string(RendererSettingValue(requested))));
            emuThread.Post([&, requested](MelonDSAndroid::MelonInstance& instance) { renderer = requested; updateConfiguration(instance); });
            break;
        }
        case SDLK_F3:
        case SDLK_F4:
        {
            int step = key == SDLK_F3 ? -1 : 1;
            emuThread.Post([&, step](MelonDSAndroid::MelonInstance& instance)
            {
                scale = std::clamp(scale + step, 1, 16);
                updateConfiguration(instance);
                // the settings are the UI thread's
                messenger->Post([&, value = scale] { settings.Set("video_internal_resolution", picojson::value(std::to_string(value))); });
            });
            break;
        }
        case SDLK_F2: windows.NextLayout(); redraw = true; break;
        case SDLK_F8:
            linear = !linear;
            MelonDSAndroid::setHalvesAt1xSupported(!linear);
            settings.Set("video_filtering", picojson::value(std::string(linear ? "linear" : "none")));
            redraw = true;
            break;
        case SDLK_F1: windows.ToggleSwap(); redraw = true; break;
        case SDLK_F9: windows.SetDetached(!windows.IsDetached()); redraw = true; break;
        case SDLK_F10: windows.CycleFit(windowId); redraw = true; break;
        case SDLK_F11: windows.ToggleFullscreen(windowId); redraw = true; break;
        case SDLK_F12:
        {
            fs::create_directories(exeDir + "screenshots", ec);
            char name[64];
            snprintf(name, sizeof(name), "screenshots/frame%06u.bmp", currentFrame.load());
            screenshot(exeDir + name);
            break;
        }
        default: break;
        }
        refreshTouchRect();
    };
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            switch (ev.type)
            {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
            {
                bool quit = false;
                windows.OnWindowEvent(ev.window, quit);
                if (quit)
                    running = false;
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_EXPOSED
                    || ev.window.event == SDL_WINDOWEVENT_CLOSE)
                {
                    redraw = true;
                    refreshTouchRect();
                }
                break;
            }
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                // the keys work in whichever window has the focus
                bool down = ev.type == SDL_KEYDOWN;
                SDL_Keycode key = ev.key.keysym.sym;
                if (u32 bit = KeyBit(key))
                    keyMask = down ? (keyMask | bit) : (keyMask & ~bit);
                if (key == SDLK_TAB)
                    emuThread.SetFastForward(down);
                if (down && !ev.key.repeat)
                    hotkey(key, ev.key.keysym.mod, ev.key.windowID);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                // touch comes from the window that shows the bottom screen
                if (ev.button.button == SDL_BUTTON_LEFT && ev.button.windowID == SDL_GetWindowID(windows.BottomWindow()))
                    touchAt(ev.button.x, ev.button.y);
                break;
            case SDL_MOUSEMOTION:
                if (touching && ev.motion.windowID == SDL_GetWindowID(windows.BottomWindow()))
                    touchAt(ev.motion.x, ev.motion.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT && touching)
                {
                    touching = false;
                    emu->releaseScreen();
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                if (u32 bit = ControllerBit(ev.cbutton.button))
                    padMask = ev.type == SDL_CONTROLLERBUTTONDOWN ? (padMask | bit) : (padMask & ~bit);
                break;
            case SDL_CONTROLLERAXISMOTION:
            {
                const int dead = 16000;
                auto setAxis = [&](u32 negBit, u32 posBit, int value)
                {
                    u32 m = stickMask & ~(negBit | posBit);
                    if (value < -dead) m |= negBit;
                    if (value > dead) m |= posBit;
                    stickMask = m;
                };
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) setAxis(1 << 5, 1 << 4, ev.caxis.value);
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) setAxis(1 << 6, 1 << 7, ev.caxis.value);
                break;
            }
            default: break;
            }
        }
        // the emulator's events (its stop), and what the emulator thread waits for (--at shot, --at key)
        messenger->Drain();
        if (!running || emuThread.IsFinished())
            break;

        // the newest frame, waiting a little for one when nothing new is queued, drawn once (or again when the window needs it)
        Frame* frame = AcquireFrame(*emu, std::chrono::steady_clock::now() + std::chrono::milliseconds(8));
        if (frame && (frame->sequence != lastSequence || redraw))
        {
            present(frame);
            lastSequence = frame->sequence;
            redraw = false;
        }

        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - secondStart).count();
        if (secs >= 1.0)
        {
            std::lock_guard guard(titleStats.Lock);
            const std::vector<double>& work = titleStats.Second.WorkMs;
            double sum = 0;
            for (double ms : work)
                sum += ms;
            char title[256];
            snprintf(title, sizeof(title), " - %s %dx - %.1f fps - %.2f ms%s", RendererName(titleStats.Renderer),
                titleStats.Scale, work.size() / secs, work.empty() ? 0.0 : sum / work.size(), paused ? " (paused)" : "");
            windows.UpdateTitles(title);
            titleStats.Second.WorkMs.clear();
            secondStart = now;
        }
    }

    // Out of the frame loop first, still serving what the emulator thread may wait for, so the last frame stays for the shots below
    emuThread.RequestFinish();
    while (!emuThread.IsFinished())
    {
        messenger->Drain();
        SDL_Delay(1);
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();

    if (opts.Frames && currentFrame >= opts.Frames && !opts.WindowShotPath.empty())
    {
        // the last frame drawn again into each window and read from its back buffer, before any swap
        if (Frame* frame = AcquireFrame(*emu, std::nullopt))
        {
            PrepareFrame(frame);
            windows.WindowShot(presenter, Frontend::FrameView::Of(*frame), linear || opts.WindowShotLinear, opts.WindowShotPath);
            ReleaseFrame(frame);
        }
    }

    stats.Print(statsLabel.c_str(), wall);
    printf("presented: %u frames in %.2f s (%.1f per second), %s\n", presented, wall, presented / wall,
        windows.IsDetached() ? "detached (two windows)" : "together");
    if (!opts.ScreenshotPath.empty() && !startFailed)
        screenshot(opts.ScreenshotPath);

    // the instance goes on the emulator thread, with its context current; its saves are written as their SaveManagers go
    emuThread.Stop();
    MelonDSAndroid::frontendContext.eventMessenger = nullptr;
    presenter.Free();
    if (audio)
        SDL_CloseAudioDevice(audio);
    SDL_GL_DeleteContext(emuContext);
    SDL_DestroyWindow(emuWindow);
    // the window places saved, and the second window closed
    windows.Close();
    SDL_GL_DeleteContext(uiContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    timeEndPeriod(1);
    if (Frontend::LogFile)
        fclose(Frontend::LogFile);
    return startFailed ? 1 : stateFailed ? 2 : 0;
}
