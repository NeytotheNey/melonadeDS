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
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "Args.h"
#include "SPI_Firmware.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"
#include "GPU3D_Compute.h"
#include "OpenGLSupport.h"
#include "Platform.h"
#include "Savestate.h"

#include "Frontend.h"
#include "Presenter.h"

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
    // both off: in this core the x86-64 JIT runs slower than the interpreter, with or without
    // fast memory (see README), where the app runs the ARM64 one with fast memory
    bool Jit = false;
    bool FastMem = false;
    bool BetterPolygons = false;
    bool HighResCoordinates = true;
    bool ColorDither = false;
    bool HiresCapture = false;
    int Filter2D = 0;          // GPU::Filter2D: 0 off, 1 on, 2 the 2x map only, 3 identity
    int Gpu2D = 0;             // the GPU 2D shadow renderer (GPU::SetShadow2D): 0 off, 1, 2 all
    bool Threaded2D = true;
    bool ThreadedSoft3D = true;
    bool HalfCamera = false;
    bool BootScreen = false;
    bool FixedTime = false;    // the RTC starts at 2026-01-01 12:00 instead of now
    bool VSync = false;
    bool Unthrottled = false;
    bool Audio = true;
    bool Linear = false;
    bool Hidden = false;
    bool GpuSync = false;      // glFinish after each frame, so the work time includes the GPU's
    Frontend::Layout Layout = Frontend::Layout::Vertical;
    int WindowScale = 2;
    u32 Frames = 0;            // 0 runs until the window is closed
    std::vector<std::string> Sets;
    bool WideDrops = false;    // the WIDEDROPS log line (GPU3D::SetWideDropLogging)
    std::string StatePath;     // a savestate (.mln/.ml1...) loaded after the reset
    struct Press { u32 From, To, Mask; };
    std::vector<Press> Presses; // --press: keys held over frame ranges, for scripted runs
    struct At { u32 Frame; std::string Action; };
    std::vector<At> Ats;       // --at: a reset, a state load or save, or a switch change before a frame
};

// --press FRAME[-LAST]:key[+key...], keys a b x y l r start select up down left right; held for
// FRAME..LAST, or 6 frames without a LAST. frames count from 0 after the reset (or --state)
bool ParsePress(const std::string& spec, Options::Press& press)
{
    static const struct { const char* Name; u32 Bit; } kKeys[] = {
        {"a", 0}, {"b", 1}, {"select", 2}, {"start", 3}, {"right", 4}, {"left", 5}, {"up", 6},
        {"down", 7}, {"r", 8}, {"l", 9}, {"x", 10}, {"y", 11},
    };
    size_t colon = spec.find(':');
    if (colon == std::string::npos) return false;
    std::string range = spec.substr(0, colon);
    size_t dash = range.find('-');
    press.From = strtoul(range.c_str(), nullptr, 10);
    press.To = dash == std::string::npos ? press.From + 5 : strtoul(range.c_str() + dash + 1, nullptr, 10);
    press.Mask = 0;
    std::string keys = spec.substr(colon + 1);
    size_t pos = 0;
    for (;;)
    {
        size_t plus = keys.find('+', pos);
        std::string key = keys.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
        bool found = false;
        for (const auto& k : kKeys)
        {
            if (key == k.Name) { press.Mask |= 1u << k.Bit; found = true; }
        }
        if (!found) return false;
        if (plus == std::string::npos) break;
        pos = plus + 1;
    }
    return press.To >= press.From;
}

void Usage()
{
    printf(
        "usage: melonade-desktop [options] game.nds\n"
        "\n"
        "  --renderer soft|gl|compute  3D renderer (default compute)\n"
        "  --scale N                   internal resolution for gl/compute, 1-16 (default 4)\n"
        "  --wide W                    WideMelon top screen width, even, 256-768 (default 256, off)\n"
        "  --better-polygons           OpenGL: better polygon splitting\n"
        "  --no-high-res-coords        Compute: native-resolution vertex coordinates\n"
        "  --dither                    Compute: smooth gradients (color dithering)\n"
        "  --hires-capture             upscaled display capture (gl/compute)\n"
        "  --filter2d                  MMPX-filtered 2D (gl/compute, scale 2+)\n"
        "  --filter2d-mode N           filtered 2D as debug.melonade.filter2d: 2 the 2x map only, 3 identity\n"
        "  --gpu2d N                   GPU 2D shadow renderer: 0 off, 1 lines that gain, 2 all (gl/compute)\n"
        "  --smooth30                  Compute: in-between frames for 30 fps games (half camera)\n"
        "  --no-threaded2d             2D on the emulator thread\n"
        "  --no-threaded-soft          software 3D on the emulator thread\n"
        "  --jit                       x86-64 JIT (off by default: slower than the interpreter here)\n"
        "  --fastmem                   JIT with fast memory, as the app runs it\n"
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
        "  --at F:reset|state|save=FILE|NAME=VALUE\n"
        "                              before frame F: a reset, the --state loaded again, a savestate\n"
        "                              written to FILE, or a --set\n"
        "  --list-sets                 list the switches --set knows\n"
        "  --widedrops                 log a WIDEDROPS line once a second (WideMelon's dropped polygons)\n"
        "\n"
        "  --layout vertical|horizontal|top|bottom\n"
        "  --window-scale N            window size in DS screens (default 2)\n"
        "  --linear                    linear filtering when drawing to the window\n"
        "  --vsync                     wait for vsync on swap (pacing stays at the DS's 59.83 Hz)\n"
        "  --unthrottled               run as fast as possible (benchmarks)\n"
        "  --no-audio\n"
        "  --frames N                  quit after N frames (prints timing stats)\n"
        "  --screenshot FILE.bmp       save the last frame at the internal resolution when quitting\n"
        "  --window-shot-linear        draw the window shot with linear filtering (the run keeps --linear's)\n"
        "  --hidden                  no visible window (with --frames)\n"
        "  --log FILE                  core log file (default melonade.log next to the exe)\n"
        "  --cache-dir DIR             Compute program cache directory\n"
        "  --verbose                   debug log lines on the console too\n"
        "\n"
        "keys: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Right Shift = Select\n"
        "      mouse on the bottom screen = touch, Tab (hold) = fast forward, P = pause, Ctrl+R = reset\n"
        "      F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up, F2 = layout, F8 = linear filter\n"
        "      F11 = fullscreen, F12 = screenshot, Esc = quit\n"
        "      game controllers work too (Nintendo layout: east = A, south = B)\n");
}

// the core's debug switches that --set can change, all on GPU. they're read as the frame runs
// or when a renderer is made, and are applied before the first frame
struct BoolSwitch { const char* Name; bool GPU::* Field; };
struct IntSwitch { const char* Name; int GPU::* Field; };

const BoolSwitch kBoolSwitches[] = {
    {"DeferCapture2D", &GPU::DeferCapture2D},
    {"EngineBNoHold", &GPU::EngineBNoHold},
    {"EarlyFlushCapture", &GPU::EarlyFlushCapture},
    {"ShadowMaskLineClear", &GPU::ShadowMaskLineClear},
    {"ShadowMaskDepthTest", &GPU::ShadowMaskDepthTest},
    {"EqualDepthMargin", &GPU::EqualDepthMargin},
    {"ComputeDirectDispatch", &GPU::ComputeDirectDispatch},
    {"ComputeGrow", &GPU::ComputeGrow},
    {"ComputeRooms", &GPU::ComputeRooms},
    {"ComputeWarm", &GPU::ComputeWarm},
    {"ShaderCache", &GPU::ShaderCache},
    {"ComputeTimers", &GPU::ComputeTimers},
    {"ComputePeaks", &GPU::ComputePeaks},
    {"ComputeSync", &GPU::ComputeSync},
    {"ComputePaceLog", &GPU::ComputePaceLog},
    {"ComputeWhy", &GPU::ComputeWhy},
    {"BlitFlushCapture", &GPU::BlitFlushCapture},
    {"LateBlit", &GPU::LateBlit},
    {"BlitAfterReadback", &GPU::BlitAfterReadback},
    {"Filter2DLogging", &GPU::Filter2DLogging},
    {"Hash2DLogging", &GPU::Hash2DLogging},
    {"Hash3DLogging", &GPU::Hash3DLogging},
    {"ComputeSSBO", &GPU::ComputeSSBO},
    {"WidePolys", &GPU::WidePolys},
    // the frontend's deband flag (GPU::OutputCap): the window doesn't deband, so this stands in for the app's
    {"OutputCapDeband", &GPU::OutputCapDeband},
};

const IntSwitch kIntSwitches[] = {
    {"CaptureSpinUs", &GPU::CaptureSpinUs},
    {"HiresCaptureDebug", &GPU::HiresCaptureDebug},
    {"GLTransSkip", &GPU::GLTransSkip},
    {"GLEdgeSkip", &GPU::GLEdgeSkip},
    {"GLClearFast", &GPU::GLClearFast},
    {"GLNoDiscard", &GPU::GLNoDiscard},
    {"GLTexProbe", &GPU::GLTexProbe},
    {"GLTexCache", &GPU::GLTexCache},
    {"CompClear", &GPU::CompClear},
    {"Comp1x", &GPU::Comp1x},
    {"Filter2DRows", &GPU::Filter2DRows},
    {"OutputCap", &GPU::OutputCap},
    {"OutputCapNeed", &GPU::OutputCapNeed},
    {"OutputCap3D", &GPU::OutputCap3D},
    {"GLTimers", &GPU::GLTimers},
    {"SceneStats", &GPU::SceneStats},
    {"ComputeCheck", &GPU::ComputeCheck},
    {"ComputeBin", &GPU::ComputeBin},
    {"ComputeCalc", &GPU::ComputeCalc},
    {"ComputeFlat", &GPU::ComputeFlat},
};

bool ApplySet(GPU& gpu, const std::string& set)
{
    size_t eq = set.find('=');
    if (eq == std::string::npos)
        return false;
    std::string name = set.substr(0, eq);
    int value = (int)strtol(set.c_str() + eq + 1, nullptr, 0);
    for (const BoolSwitch& s : kBoolSwitches)
    {
        if (name == s.Name)
        {
            gpu.*s.Field = value != 0;
            return true;
        }
    }
    for (const IntSwitch& s : kIntSwitches)
    {
        if (name == s.Name)
        {
            gpu.*s.Field = value;
            return true;
        }
    }
    return false;
}

bool ReadFile(const std::string& path, std::vector<u8>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseeko64(f, 0, SEEK_END);
    s64 len = ftello64(f);
    fseeko64(f, 0, SEEK_SET);
    out.resize((size_t)std::max<s64>(len, 0));
    bool ok = len >= 0 && fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool ParseArgs(int argc, char** argv, Options& opts)
{
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

        if (arg == "--renderer")
        {
            std::string r = next();
            if (r == "soft" || r == "software") opts.Renderer = RendererType::Software;
            else if (r == "gl" || r == "opengl") opts.Renderer = RendererType::OpenGL;
            else if (r == "compute") opts.Renderer = RendererType::Compute;
            else { fprintf(stderr, "unknown renderer %s\n", r.c_str()); return false; }
        }
        else if (arg == "--scale") opts.Scale = std::clamp(atoi(next().c_str()), 1, 16);
        else if (arg == "--wide")
        {
            opts.WideWidth = atoi(next().c_str());
            if (opts.WideWidth < 256 || opts.WideWidth > 768 || (opts.WideWidth & 1))
            {
                fprintf(stderr, "--wide needs an even width 256-768\n");
                return false;
            }
        }
        else if (arg == "--better-polygons") opts.BetterPolygons = true;
        else if (arg == "--no-high-res-coords") opts.HighResCoordinates = false;
        else if (arg == "--dither") opts.ColorDither = true;
        else if (arg == "--hires-capture") opts.HiresCapture = true;
        else if (arg == "--filter2d") opts.Filter2D = 1;
        else if (arg == "--filter2d-mode") opts.Filter2D = std::clamp(atoi(next().c_str()), 0, 3);
        else if (arg == "--gpu2d") opts.Gpu2D = std::clamp(atoi(next().c_str()), 0, 2);
        else if (arg == "--smooth30") opts.HalfCamera = true;
        else if (arg == "--no-threaded2d") opts.Threaded2D = false;
        else if (arg == "--no-threaded-soft") opts.ThreadedSoft3D = false;
        else if (arg == "--jit") opts.Jit = true;
        else if (arg == "--no-jit") opts.Jit = false;
        else if (arg == "--fastmem") opts.Jit = opts.FastMem = true;
        else if (arg == "--bios") opts.BiosDir = next();
        else if (arg == "--gba") opts.GbaRomPath = next();
        else if (arg == "--sav") opts.SavePath = next();
        else if (arg == "--state") opts.StatePath = next();
        else if (arg == "--press")
        {
            Options::Press press;
            std::string spec = next();
            if (!ParsePress(spec, press)) { fprintf(stderr, "bad --press %s\n", spec.c_str()); return false; }
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
            for (const BoolSwitch& s : kBoolSwitches) printf("%s (0/1)\n", s.Name);
            for (const IntSwitch& s : kIntSwitches) printf("%s (number)\n", s.Name);
            exit(0);
        }
        else if (arg == "--layout")
        {
            std::string l = next();
            if (l == "vertical") opts.Layout = Frontend::Layout::Vertical;
            else if (l == "horizontal") opts.Layout = Frontend::Layout::Horizontal;
            else if (l == "top") opts.Layout = Frontend::Layout::TopOnly;
            else if (l == "bottom") opts.Layout = Frontend::Layout::BottomOnly;
            else { fprintf(stderr, "unknown layout %s\n", l.c_str()); return false; }
        }
        else if (arg == "--window-scale") opts.WindowScale = std::clamp(atoi(next().c_str()), 1, 8);
        else if (arg == "--linear") opts.Linear = true;
        else if (arg == "--vsync") opts.VSync = true;
        else if (arg == "--unthrottled") opts.Unthrottled = true;
        else if (arg == "--no-audio") opts.Audio = false;
        else if (arg == "--frames") opts.Frames = strtoul(next().c_str(), nullptr, 0);
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
    return !opts.RomPath.empty();
}

// the emulator, its renderer and the window, on one thread: the core's GL renderers and the
// presenter share the window's context
class Emulator
{
public:
    Options Opts;
    std::unique_ptr<NDS> Nds;
    RendererType Current = RendererType::Software;
    RendererType Requested = RendererType::Software;
    bool RendererDirty = true;
    Frontend::Presenter Present;

    bool Create()
    {
        NDSArgs args {};
#ifdef JIT_ENABLED
        // the app's JIT settings (block size 32), but fast memory only on request
        args.JIT = std::nullopt;
        if (Opts.Jit)
        {
            JITArgs jit;
            jit.FastMemory = Opts.FastMem;
            args.JIT = jit;
        }
#else
        args.JIT = std::nullopt;
#endif
        args.OutputSampleRate = 48000.0;

        bool haveFirmware = false;
        if (!Opts.BiosDir.empty())
        {
            std::vector<u8> bios9, bios7, firmware;
            if (!ReadFile(Opts.BiosDir + "/bios9.bin", bios9) || bios9.size() != ARM9BIOSSize
                || !ReadFile(Opts.BiosDir + "/bios7.bin", bios7) || bios7.size() != ARM7BIOSSize)
            {
                fprintf(stderr, "can't read bios9.bin and bios7.bin in %s\n", Opts.BiosDir.c_str());
                return false;
            }
            memcpy(args.ARM9BIOS->data(), bios9.data(), ARM9BIOSSize);
            memcpy(args.ARM7BIOS->data(), bios7.data(), ARM7BIOSSize);

            std::string fwPath = Opts.BiosDir + "/firmware.bin";
            if (Platform::FileHandle* f = Platform::OpenFile(fwPath, Platform::FileMode::Read))
            {
                args.Firmware = Firmware(f);
                Platform::CloseFile(f);
                haveFirmware = args.Firmware.Buffer() != nullptr;
                if (haveFirmware)
                    args.Firmware.UpdateChecksums();
            }
        }
        if (!haveFirmware)
            args.Firmware = Firmware(0);
        if (!haveFirmware && Opts.BootScreen)
        {
            fprintf(stderr, "--boot-screen needs the official BIOS and firmware (--bios)\n");
            Opts.BootScreen = false;
        }

        Nds = std::make_unique<NDS>(std::move(args), &Frontend::App);

        for (const std::string& set : Opts.Sets)
        {
            if (!ApplySet(Nds->GPU, set))
                fprintf(stderr, "unknown --set %s (see --list-sets)\n", set.c_str());
        }
        Nds->GPU.GPU3D.SetWideDropLogging(Opts.WideDrops);

        if (!LoadRom())
            return false;
        Requested = Opts.Renderer;
        Reset();
        if (!Opts.StatePath.empty() && !LoadState(Opts.StatePath))
            return false;
        return true;
    }

    bool LoadState(const std::string& path)
    {
        std::vector<u8> data;
        if (!ReadFile(path, data) || data.empty())
        {
            fprintf(stderr, "can't read savestate %s\n", path.c_str());
            return false;
        }
        // a state that fails part of the way through leaves the console half loaded, so it can't run on
        Savestate state(data.data(), (u32)data.size(), false);
        if (state.Error || !Nds->DoSavestate(&state) || state.Error)
        {
            fprintf(stderr, "savestate %s didn't load\n", path.c_str());
            return false;
        }
        printf("state: %s\n", path.c_str());
        return true;
    }

    bool SaveState(const std::string& path)
    {
        Savestate state;
        if (state.Error || !Nds->DoSavestate(&state) || state.Error)
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

    bool LoadRom()
    {
        std::vector<u8> rom;
        if (!ReadFile(Opts.RomPath, rom) || rom.empty())
        {
            fprintf(stderr, "can't read ROM %s\n", Opts.RomPath.c_str());
            return false;
        }
        auto romData = std::make_unique<u8[]>(rom.size());
        memcpy(romData.get(), rom.data(), rom.size());
        u32 romLen = (u32)rom.size();
        rom.clear();
        rom.shrink_to_fit();

        if (Opts.SavePath.empty())
            Opts.SavePath = fs::path(Opts.RomPath).replace_extension(".sav").string();
        Frontend::App.NDSSave.Path = Opts.SavePath;

        NDSCart::NDSCartArgs cartArgs {};
        std::vector<u8> sav;
        if (ReadFile(Opts.SavePath, sav) && !sav.empty())
        {
            cartArgs.SRAM = std::make_unique<u8[]>(sav.size());
            memcpy(cartArgs.SRAM.get(), sav.data(), sav.size());
            cartArgs.SRAMLength = (u32)sav.size();
            Frontend::App.NDSSave.Data = std::move(sav);
            printf("save: %s\n", Opts.SavePath.c_str());
        }

        auto cart = NDSCart::ParseROM(std::move(romData), romLen, &Frontend::App, std::move(cartArgs));
        if (!cart)
        {
            fprintf(stderr, "%s isn't a DS ROM the core can parse\n", Opts.RomPath.c_str());
            return false;
        }
        Nds->SetNDSCart(std::move(cart));

        if (!Opts.GbaRomPath.empty())
        {
            std::vector<u8> gba, gbaSav;
            if (!ReadFile(Opts.GbaRomPath, gba) || gba.empty())
            {
                fprintf(stderr, "can't read GBA ROM %s\n", Opts.GbaRomPath.c_str());
                return false;
            }
            auto gbaData = std::make_unique<u8[]>(gba.size());
            memcpy(gbaData.get(), gba.data(), gba.size());
            Frontend::App.GBASave.Path = fs::path(Opts.GbaRomPath).replace_extension(".sav").string();
            std::unique_ptr<u8[]> gbaSram;
            u32 gbaSramLen = 0;
            if (ReadFile(Frontend::App.GBASave.Path, gbaSav) && !gbaSav.empty())
            {
                gbaSram = std::make_unique<u8[]>(gbaSav.size());
                memcpy(gbaSram.get(), gbaSav.data(), gbaSav.size());
                gbaSramLen = (u32)gbaSav.size();
            }
            auto gbaCart = GBACart::ParseROM(std::move(gbaData), (u32)gba.size(), std::move(gbaSram), gbaSramLen, &Frontend::App);
            if (gbaCart)
                Nds->SetGBACart(std::move(gbaCart));
            else
                fprintf(stderr, "%s isn't a GBA ROM the core can parse\n", Opts.GbaRomPath.c_str());
        }
        return true;
    }

    void Reset()
    {
        Nds->Reset();
        SetDateTime();
        if (!Opts.BootScreen || Nds->NeedsDirectBoot())
            Nds->SetupDirectBoot(fs::path(Opts.RomPath).filename().string());
        Nds->Start();
    }

    void SetDateTime()
    {
        // the same start every run, so two runs can be compared byte for byte
        if (Opts.FixedTime)
        {
            Nds->RTC.SetDateTime(2026, 1, 1, 12, 0, 0);
            return;
        }
        time_t now = time(nullptr);
        tm local {};
        localtime_s(&local, &now);
        Nds->RTC.SetDateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    }

    // what the app's MelonInstance::updateRenderer does, between frames
    void UpdateRenderer()
    {
        if (Requested != Current)
        {
            std::unique_ptr<Renderer3D> renderer;
            switch (Requested)
            {
            case RendererType::Software: renderer = std::make_unique<SoftRenderer>(); break;
            case RendererType::OpenGL: renderer = GLRenderer::New(); break;
            case RendererType::Compute: renderer = ComputeRenderer::New(); break;
            }
            if (!renderer && Requested != RendererType::Software)
            {
                Platform::Log(Platform::LogLevel::Error, "Failed to create the %s renderer, using the software renderer\n", RendererName(Requested));
                Requested = RendererType::Software;
                renderer = std::make_unique<SoftRenderer>();
            }
            Current = Requested;
            Nds->GPU.SetRenderer3D(std::move(renderer));
            Present.ResetLayouts();
        }

        switch (Current)
        {
        case RendererType::Software:
            static_cast<SoftRenderer&>(Nds->GPU.GetRenderer3D()).SetThreaded(Opts.ThreadedSoft3D, Nds->GPU);
            break;
        case RendererType::OpenGL:
        {
            auto& gl = static_cast<GLRenderer&>(Nds->GPU.GetRenderer3D());
            gl.SetViewWidth(Opts.WideWidth);
            gl.SetRenderSettings(Opts.BetterPolygons, Opts.Scale);
            break;
        }
        case RendererType::Compute:
        {
            auto& compute = static_cast<ComputeRenderer&>(Nds->GPU.GetRenderer3D());
            compute.SetViewWidth(Opts.WideWidth);
            compute.SetRenderSettings(Opts.Scale, Opts.HighResCoordinates);
            compute.SetColorDither(Opts.ColorDither);
            break;
        }
        }

        Nds->GPU.SetThreaded2D(Opts.Threaded2D);
        Nds->GPU.SetHiresCapture(Opts.HiresCapture);
        Nds->GPU.Filter2D = Opts.Filter2D;
        Nds->GPU.SetShadow2D(Opts.Gpu2D, false, false, 0);
        Nds->GPU.GPU3D.SetHalfCameraSmoothing(Opts.HalfCamera && Current == RendererType::Compute);

        printf("renderer: %s, scale %d, width %d\n", RendererName(Current), ActiveScale(), Opts.WideWidth);
    }

    // the renderer's scale, which drops below --scale when that doesn't fit the GPU's limits
    int ActiveScale()
    {
        switch (Current)
        {
        case RendererType::OpenGL: return static_cast<GLRenderer&>(Nds->GPU.GetRenderer3D()).GetScaleFactor();
        case RendererType::Compute: return static_cast<ComputeRenderer&>(Nds->GPU.GetRenderer3D()).GetScaleFactor();
        default: return 1;
        }
    }

    u32 RunFrame()
    {
        if (RendererDirty)
        {
            RendererDirty = false;
            UpdateRenderer();
        }

        // GPU::OutputCap: the window has no need of its own, so only --set OutputCapNeed caps the output
        if (Current == RendererType::OpenGL)
            static_cast<GLRenderer&>(Nds->GPU.GetRenderer3D()).SetOutputNeed(Nds->GPU, 0);
        else if (Current == RendererType::Compute)
            static_cast<ComputeRenderer&>(Nds->GPU.GetRenderer3D()).SetOutputNeed(Nds->GPU, 0);

        Renderer3D& renderer = Nds->GPU.GetRenderer3D();
        if (renderer.NeedsShaderCompile())
        {
            auto start = std::chrono::steady_clock::now();
            do
            {
                int current, count;
                renderer.ShaderCompileStep(current, count);
            }
            while (renderer.NeedsShaderCompile());
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            printf("shaders compiled in %.0f ms\n", ms);
        }

        // GPU::Comp1x: the presenter shows a half at 1x nearest-upscaled, so with linear filtering
        // every half is drawn in full, as the app does with any filter but none
        Nds->GPU.Comp1xFrontend = !Opts.Linear;
        return Nds->RunFrame();
    }

    int TopWidth()
    {
        return Nds->GPU.GetRenderer3D().Accelerated ? Nds->GPU.GetRenderer3D().GetOutputWidth() : 256;
    }
};

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

}

int main(int argc, char** argv)
{
    Emulator emu;
    Options& opts = emu.Opts;
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    std::string exeDir = ExeDir();
    Frontend::LogFile = fopen((opts.LogPath.empty() ? exeDir + "melonade.log" : opts.LogPath).c_str(), "w");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    int boxW = opts.Layout == Frontend::Layout::Horizontal ? opts.WideWidth + 256 : opts.WideWidth;
    int boxH = opts.Layout == Frontend::Layout::Vertical ? 384 : 192;
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (opts.Hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    SDL_Window* window = SDL_CreateWindow("melonadeDS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        boxW * opts.WindowScale, boxH * opts.WindowScale, flags);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
    {
        fprintf(stderr, "can't make a GLES 3.2 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    int missing = PcGl_Load([](const char* name) { return SDL_GL_GetProcAddress(name); });
    printf("GL: %s | %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (missing)
        printf("GL: %d GLES 3.2 functions missing\n", missing);
    SDL_GL_SetSwapInterval(opts.VSync ? 1 : 0);

    if (!emu.Present.Init())
    {
        fprintf(stderr, "can't set up the screen shader\n");
        return 1;
    }

    std::string cacheDir = opts.CacheDir.empty() ? exeDir + "cache" : opts.CacheDir;
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    OpenGL::SetShaderCachePath(cacheDir + "/compute_programs.bin");

    if (!emu.Create())
        return 1;

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

    u32 keyMask = 0, padMask = 0, stickMask = 0;
    bool touching = false, paused = false, fastForward = false, running = true;
    bool stateFailed = false; // an --at state load failed: the run stops, and nothing is saved
    u32 frame = 0;
    std::vector<s16> audioBuf(2 * 1024);
    FrameStats stats, second;
    auto runStart = std::chrono::steady_clock::now();
    auto secondStart = runStart;
    auto nextFrame = runStart;
    // the DS's refresh rate: 33513982 Hz / (6 * 355 * 263)
    const auto frameTime = std::chrono::duration<double>(6.0 * 355 * 263 / 33513982.0);

    auto screenshot = [&](const std::string& path)
    {
        std::vector<u8> pixels;
        int w, h;
        if (emu.Present.ReadFrame(pixels, w, h) && Frontend::WriteBMP(path.c_str(), pixels, w, h))
            printf("screenshot %s (%dx%d)\n", path.c_str(), w, h);
        else
            fprintf(stderr, "screenshot %s failed\n", path.c_str());
    };

    auto touchAt = [&](int x, int y)
    {
        const Frontend::Rect& r = emu.Present.BottomRect();
        if (r.W <= 0)
            return;
        // window points to drawable pixels, as the rect is in the latter
        int ww, wh, dw, dh;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        float px = x * (float)dw / ww, py = y * (float)dh / wh;
        int tx = (int)((px - r.X) / r.W * 256), ty = (int)((py - r.Y) / r.H * 192);
        if (!touching && (tx < 0 || tx > 255 || ty < 0 || ty > 191))
            return;
        touching = true;
        emu.Nds->TouchScreen((u16)std::clamp(tx, 0, 255), (u16)std::clamp(ty, 0, 191));
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
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                bool down = ev.type == SDL_KEYDOWN;
                SDL_Keycode key = ev.key.keysym.sym;
                if (u32 bit = KeyBit(key))
                    keyMask = down ? (keyMask | bit) : (keyMask & ~bit);
                if (key == SDLK_TAB)
                    fastForward = down;
                if (!down || ev.key.repeat)
                    break;
                switch (key)
                {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_p: paused = !paused; break;
                case SDLK_r:
                    if (ev.key.keysym.mod & KMOD_CTRL)
                    {
                        emu.Reset();
                        printf("reset\n");
                    }
                    break;
                case SDLK_F5: emu.Requested = RendererType::Software; emu.RendererDirty = true; break;
                case SDLK_F6: emu.Requested = RendererType::OpenGL; emu.RendererDirty = true; break;
                case SDLK_F7: emu.Requested = RendererType::Compute; emu.RendererDirty = true; break;
                case SDLK_F3: opts.Scale = std::max(1, opts.Scale - 1); emu.RendererDirty = true; break;
                case SDLK_F4: opts.Scale = std::min(16, opts.Scale + 1); emu.RendererDirty = true; break;
                case SDLK_F2: opts.Layout = (Frontend::Layout)(((int)opts.Layout + 1) % (int)Frontend::Layout::Count); break;
                case SDLK_F8: opts.Linear = !opts.Linear; break;
                case SDLK_F11:
                    SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    break;
                case SDLK_F12:
                {
                    fs::create_directories(exeDir + "screenshots", ec);
                    char name[64];
                    snprintf(name, sizeof(name), "screenshots/frame%06u.bmp", frame);
                    screenshot(exeDir + name);
                    break;
                }
                default: break;
                }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT)
                    touchAt(ev.button.x, ev.button.y);
                break;
            case SDL_MOUSEMOTION:
                if (touching)
                    touchAt(ev.motion.x, ev.motion.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT && touching)
                {
                    touching = false;
                    emu.Nds->ReleaseScreen();
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
                    stickMask &= ~(negBit | posBit);
                    if (value < -dead) stickMask |= negBit;
                    if (value > dead) stickMask |= posBit;
                };
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) setAxis(1 << 5, 1 << 4, ev.caxis.value);
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) setAxis(1 << 6, 1 << 7, ev.caxis.value);
                break;
            }
            default: break;
            }
        }
        if (Frontend::App.StopRequested)
            running = false;
        if (!running)
            break;

        int dw, dh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);

        if (!paused)
        {
            u32 scriptMask = 0;
            for (const Options::Press& press : opts.Presses)
            {
                if (frame >= press.From && frame <= press.To)
                    scriptMask |= press.Mask;
            }
            emu.Nds->SetKeyMask(~(keyMask | padMask | stickMask | scriptMask) & 0xFFF);

            for (const Options::At& at : opts.Ats)
            {
                if (at.Frame != frame)
                    continue;
                if (at.Action == "reset")
                    emu.Reset();
                else if (at.Action == "state")
                {
                    if (opts.StatePath.empty())
                        fprintf(stderr, "--at %u:state: no state to load\n", at.Frame);
                    else if (!emu.LoadState(opts.StatePath))
                    {
                        fprintf(stderr, "--at %u:state: the state didn't load, stopping\n", at.Frame);
                        stateFailed = true;
                        running = false;
                        break;
                    }
                }
                else if (at.Action.rfind("save=", 0) == 0)
                    emu.SaveState(at.Action.substr(5));
                else if (!ApplySet(emu.Nds->GPU, at.Action))
                    fprintf(stderr, "unknown --at %u:%s (see --list-sets)\n", at.Frame, at.Action.c_str());
                printf("at frame %u: %s\n", at.Frame, at.Action.c_str());
            }
            if (!running)
                break;

            auto workStart = std::chrono::steady_clock::now();
            emu.RunFrame();
            emu.Present.Draw(*emu.Nds, dw, dh, emu.TopWidth(), opts.Layout, opts.Linear);
            if (opts.GpuSync)
                glFinish();
            double workMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - workStart).count();
            stats.Add(workMs);
            second.Add(workMs);
            frame++;

            // the core's audio, dropped while it would only pile up (fast forward, a slow frame)
            int got;
            while ((got = emu.Nds->SPU.ReadOutput(audioBuf.data(), (int)audioBuf.size() / 2)) > 0)
            {
                if (audio && !fastForward && !opts.Unthrottled && SDL_GetQueuedAudioSize(audio) < 48000 * 4 / 10)
                    SDL_QueueAudio(audio, audioBuf.data(), got * 4);
            }

            Frontend::FlushSaves(false);
        }
        else
        {
            emu.Present.Draw(*emu.Nds, dw, dh, emu.TopWidth(), opts.Layout, opts.Linear);
        }
        SDL_GL_SwapWindow(window);

        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - secondStart).count();
        if (secs >= 1.0)
        {
            char title[256];
            snprintf(title, sizeof(title), "melonadeDS - %s %dx - %.1f fps - %.2f ms%s", RendererName(emu.Current),
                emu.ActiveScale(), second.WorkMs.size() / secs,
                second.WorkMs.empty() ? 0.0 : [&] { double s = 0; for (double ms : second.WorkMs) s += ms; return s / second.WorkMs.size(); }(),
                paused ? " (paused)" : "");
            SDL_SetWindowTitle(window, title);
            second.WorkMs.clear();
            secondStart = now;
        }

        if (opts.Frames && frame >= opts.Frames)
        {
            if (!opts.WindowShotPath.empty())
            {
                // the frame drawn again and read from the back buffer, before any swap
                emu.Present.Draw(*emu.Nds, dw, dh, emu.TopWidth(), opts.Layout, opts.Linear || opts.WindowShotLinear);
                std::vector<u8> pixels((size_t)dw * dh * 4), flipped(pixels.size());
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < dh; y++)
                    memcpy(&flipped[(size_t)y * dw * 4], &pixels[(size_t)(dh - 1 - y) * dw * 4], (size_t)dw * 4);
                if (Frontend::WriteBMP(opts.WindowShotPath.c_str(), flipped, dw, dh))
                    printf("window shot %s (%dx%d)\n", opts.WindowShotPath.c_str(), dw, dh);
            }
            break;
        }

        // the DS's pace, unless fast forwarding; a frame that ran late doesn't make the next ones hurry
        if (paused)
            SDL_Delay(16);
        else if (!fastForward && !opts.Unthrottled)
        {
            nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameTime);
            if (nextFrame < now - std::chrono::milliseconds(50))
                nextFrame = now;
            auto sleepUntil = nextFrame - std::chrono::milliseconds(2);
            if (sleepUntil > now)
                std::this_thread::sleep_until(sleepUntil);
            while (std::chrono::steady_clock::now() < nextFrame)
                std::this_thread::yield();
        }
        else
            nextFrame = std::chrono::steady_clock::now();
    }

    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
    stats.Print(RendererName(emu.Current), wall);
    if (!opts.ScreenshotPath.empty())
        screenshot(opts.ScreenshotPath);

    if (!stateFailed)
        Frontend::FlushSaves(true);
    OpenGL::ShaderCacheStats cacheStats;
    OpenGL::SaveShaderCache(cacheStats);
    emu.Nds->Stop();
    emu.Nds.reset();
    emu.Present.Free();
    if (audio)
        SDL_CloseAudioDevice(audio);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    timeEndPeriod(1);
    if (Frontend::LogFile)
        fclose(Frontend::LogFile);
    return stateFailed ? 2 : 0;
}
