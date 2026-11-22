// melonadeDS for PC: runs a ROM with the same core and OpenGL/Compute renderers as the Android app, in an SDL2 window with a GLES 3.2
// context, with the menus (ui/) drawn over it. Started without a ROM it opens the game library. See desktop/README.md
#include <SDL.h>
#include <windows.h>
#include <objbase.h>
#include <timeapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
#include "EmulatorArgsBuilder.h"
#include "MelonInstance.h"
#include "gamestate/GameState.h"
#include "renderer/FrameQueue.h"
#include "renderer/GpuFence.h"
#include "renderer/ScreenshotRenderer.h"
#include "retroachievements/RetroAchievementsManager.h"

#include "Audio.h"
#include "Controls.h"
#include "DesktopEventMessenger.h"
#include "EmulatorThread.h"
#include "FileDialog.h"
#include "Frontend.h"
#include "ImageFile.h"
#include "Input.h"
#include "Library.h"
#include "MetadataSync.h"
#include "Secret.h"
#include "Presenter.h"
#include "RomFile.h"
#include "RunOptions.h"
#include "SaveSlots.h"
#include "ScreenWindows.h"
#include "Settings.h"
#include "UiScript.h"
#include "version.h"
#include "ui/Host.h"
#include "ui/Ui.h"

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
    std::string DepthShotPath;     // --depth-shot: the last frame's depth as a PNG, with its stats printed
    std::string DepthRawPath;      // --depth-raw: the last frame's depth texture as it is, for scripts
    std::string CameraDumpPath;    // --camera-dump: each 3D scene's projections and viewports, a JSON line per scene
    std::string CameraShownPath;   // --camera-shown: the depth camera each presented frame came with, a JSON line per frame
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
    bool HighResCoordinates = true;
    bool ColorDither = false;
    // the renderer settings the app has (video_hires_capture, video_filter_2d, ...), from the settings (ApplySettings); the emulator
    // thread's once a game runs, which a change in the menus sets there
    struct RenderToggles
    {
        bool HiresCapture = false;
        int Filter2D = 0;      // GPU::Filter2D: 0 off, 1 on, 2 the 2x map only, 3 identity (--filter2d-mode)
        bool SharperRotated2D = false;
        bool EdgeMarking = true;
        bool PreciseDepth = false; // GPU::PreciseDepth, the app's "Precise depth" setting
        bool GlSeams = false;
    } Toggles;
    int Filter2DMode = -1;     // --filter2d-mode: 2 or 3, the debug modes without a setting
    int Gpu2D = 0;             // the GPU 2D shadow renderer (GPU::SetShadow2D): 0 off, 1, 2 all, 3 all + log, 4 check
    int Gpu2DWide = 0;         // debug.melonade.gpu2dwide: 1 lets every layer draw past the edge
    int Gpu2DAffine = 0;       // debug.melonade.gpu2daffine: 1-2 draw rotated BGs with the plain composite, -1 lifts the 6x cap
    bool Threaded2D = true;
    bool ThreadedSoft3D = true;
    bool HalfCamera = false;
    int HalfCameraMode = 0;    // debug.melonade.halfcam (GPU3D::SetHalfCamera), replaces --smooth30
    bool BootScreen = false;
    std::string DsiBiosDir;    // dsi_bios_dir, for console_type dsi
    // the files of a DSi boot (console_type dsi), which the configuration points at while the game runs
    struct DsiFiles { std::string Bios9, Bios7, Firmware, Nand, DsBios9, DsBios7; } Dsi;
    int ConsoleType = 0;       // console_type: 0 DS, 1 DSi
    float FastForwardSpeed = 0; // fast_forward_speed_multiplier, 0 for unlimited
    bool FixedTime = false;    // the RTC starts at 2026-01-01 12:00 instead of now
    bool VSync = false;
    bool Unthrottled = false;
    bool Audio = true;
    std::string AudioWav;      // --audio-wav: what the audio callback plays, as a WAV
    bool AudioLog = false;     // --audio-log: the ring's fill and the ratio once a second
    std::string AudioTrace;    // --audio-trace: every callback and frame's ring fill, as a CSV
    std::string VirtualPad;    // --virtual-pad: an SDL virtual controller for tests
    // the screens' look (video_filtering, video_deband, video_ds_screen_colors, video_screen_shadow), from the settings (ApplySettings); the
    // emulator thread's once a game runs (deband also dithers on Compute), the presenter keeps its own copy
    Frontend::RendererConfig Render;
    // --tilt-shift: the Lua tilt-shift's passes over the top screen, for tests until the scripts drive it (phase 4)
    std::optional<Frontend::TiltShift> TiltShift;
    // --bloom, --bloom-all and --haze: the Lua bloom's, vignette's and haze's passes over the top screen, the same way
    std::optional<Frontend::Bloom> Bloom;
    // --dof: the Lua depth of field's passes the same way, with the frames' depth asked of the core (MelonInstance::setDepthWanted)
    std::optional<Frontend::DepthOfField> DepthOfField;
    // --shade: the Lua shading's passes the same way, with the frames' depth asked of the core too
    std::optional<Frontend::Shade> Shade;
    // --output-cap: the windows' output scale need goes to the core (GPU::OutputCap) after each present, as the app's surfaces send theirs
    bool OutputCap = false;
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
    // that have a setting (Renderer, Scale, Render, Audio, VSync) are filled from the settings once they're read (ApplySettings)
    std::vector<std::pair<std::string, picojson::value>> SettingOverrides;
    std::optional<Frontend::WindowPlace> Window, Window2; // --window, --window2
    bool SaveHiddenPlaces = false;

    std::string OpenScreen;    // --open: a menu screen opened at the start
    std::string UiScriptPath;  // --ui-script
    std::string StateDir;      // --state-dir: the savestate slots' folder instead of the ROM's
    std::vector<std::string> RomFolders; // --rom-folder: the library's folders for this run (rom_search_dirs)
    // --rom-config KEY=VALUE: over the booted game's own settings (RomConfig) for this run, never stored
    std::vector<std::pair<std::string, std::string>> RomConfigOverrides;
    bool PretendTitleArt = false; // --pretend-title-art: every game counts as having title art (the mosaic's wide tiles)
    int PretendWideOneIn = 0;     // --pretend-wide-tiles N: one in N of them wide, by path
    // the metadata sync (MetadataSync.h): --no-sync, --sync-now, the test servers, and a Web API key for the run only
    bool NoSync = false, SyncNow = false;
    std::string MetadataBaseUrl, ThumbnailsBaseUrl, DatUrl, RaWebApiKey;
    int SyncBackoffMs = 0;     // --sync-backoff S: the first wait after a sync stopped early (tests; 15 minutes otherwise)
};

void Usage()
{
    printf(
        "melonadeDS for PC " MELONADE_VERSION_NAME "\n"
        "usage: melonade-desktop [options] [game.nds|.dsi|.zip|.7z]\n"
        "\n"
        "  Without a ROM the game library opens. With one the game boots (a zip or 7z unpacked first), and Esc or\n"
        "  a controller's Start or Guide opens the pause menu. The options below are for testing and scripted runs.\n"
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
        "  --sav FILE                  the command line's game's save file (default: the ROM's path with .sav, or\n"
        "                              in a test run a copy of it in the profile); games opened from the menus\n"
        "                              use their own the same way\n"
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
        "  --state-dir DIR             the command line's game's savestate slots (.ml0-.ml8) in DIR instead of\n"
        "                              next to the ROM (without it, a test run keeps them in its profile)\n"
        "  --list-sets                 list the switches --set knows\n"
        "  --widedrops                 log a WIDEDROPS line once a second (WideMelon's dropped polygons)\n"
        "\n"
        "  --profile DIR               settings.json in DIR instead of %%APPDATA%%\\melonadeDS (or the exe's folder with a\n"
        "                              portable.txt there); a scripted run (--hidden or --frames) without it\n"
        "                              gets a fresh temporary one, so it never reads or writes your settings\n"
        "  --setting KEY=VALUE         a setting for this run only, e.g. desktop_fit_bottom=integer (README lists them)\n"
        "  (the options that change a setting, --renderer, --scale, --filter, --linear, --deband, --ds-colors,\n"
        "  --screen-shadow, --vsync, --no-audio, --layout, --detach and --rom-folder, are for the run only too: none\n"
        "  of them is written to settings.json)\n"
        "\n"
        "  --layout vertical|horizontal|top|bottom   the screens together in one window (with --detach: the\n"
        "                              layout for when they're together again)\n"
        "  --detach                    the screens detached: the top and bottom screens in two windows\n"
        "  --window W,H[,X,Y]          the main window's size in pixels (and desktop position) for this run\n"
        "  --window2 W,H[,X,Y]         the same for the second window when detached\n"
        "  --window-scale N            default window size in DS screens (default 2), times the monitor's\n"
        "                              scale (150%%: 3 screens) unless --hidden\n"
        "  --filter NAME               the screen filter for this run, the app's video_filtering: none, linear, xbr2,\n"
        "                              hq2x, hq4x, quilez, lcd, scanlines, scale2x, fxaa, sharp, mmpx (any case)\n"
        "  --linear                    the same as --filter linear\n"
        "  --deband on|off             smooth gradients (video_deband; also Compute's color dithering, as in the app)\n"
        "  --ds-colors on|off          the DS screen's colors (video_ds_screen_colors)\n"
        "  --screen-shadow on|off      a soft shadow around the screens (video_screen_shadow)\n"
        "  --tilt-shift TOP,BOTTOM,CENTER,WIDTH,RADIUS[,SATURATION,CONTRAST[,COLOR]]\n"
        "                              the Lua tilt-shift's passes over the top screen, for tests: the blur's strength\n"
        "                              above and below (0-1), the sharp band's center and width (0-1 of the screen), the\n"
        "                              radius in DS pixels, the color boost and what it follows (0-1)\n"
        "  --bloom STRENGTH[,THRESHOLD,RADIUS[,VIGNETTE,SIZE]]\n"
        "                              the Lua bloom's and vignette's passes over the top screen, for tests: the glow's\n"
        "                              strength (0-2), the luma it starts at (0-1, 0.7) and its radius in DS pixels (0-8,\n"
        "                              3), the vignette's strength (0-1) and where it starts (0-1 of the half-diagonal, 0.6);\n"
        "                              only the 3D glows when the frame has its depth (OpenGL), as Lua's only_3d\n"
        "  --bloom-all                 with --bloom, every bright pixel glows, 2D such as text boxes too (only_3d off)\n"
        "  --haze STRENGTH[,START,FALLOFF[,RRGGBB]]\n"
        "                              the Lua haze over the top screen's 3D, for tests (OpenGL only): how far the far 3D\n"
        "                              goes towards the color (0-1), the depth it starts at and how far past that it's full\n"
        "                              (0-1, 0.9968 and 0.0008), and the color in hex (DCE6F0)\n"
        "  --dof STRENGTH,RADIUS[,RANGE,FALLOFF[,FOCUS_X,FOCUS_Y[,FOCUS_DEPTH]]]\n"
        "                              the Lua depth of field's passes over the top screen, for tests (OpenGL only): its\n"
        "                              strength (0-1) and radius in DS pixels (0-12), the sharp range around the focus and\n"
        "                              the falloff past it in depth (0-1, 0.0002 and 0.0015), the focus point in DS pixels\n"
        "                              (128,96) or a focus depth (0-1)\n"
        "  --shade STRENGTH[,RADIUS,RANGE[,RRGGBB]]\n"
        "                              the Lua shading over the top screen's 3D, for tests (OpenGL only): how far the most\n"
        "                              shaded pixels go towards the tint (0-1), the depth blur's radius in DS pixels (1-16,\n"
        "                              8), how much farther than the blurred depth a pixel is when fully shaded (0-1,\n"
        "                              0.0008), and the tint in hex (33384F)\n"
        "  --output-cap                send the windows' screen size to the core (GPU::OutputCap), as the app does: the\n"
        "                              frame is drawn no bigger than the screens are shown\n"
        "  --vsync                     wait for vsync on swap (pacing stays at the DS's 59.83 Hz)\n"
        "  --unthrottled               run as fast as possible (benchmarks)\n"
        "  --no-audio\n"
        "  --frames N                  quit after N frames (prints timing stats): the game's frames, plus the UI's\n"
        "                              while no game frame runs (the start screen, a menu over the paused game)\n"
        "  --game-state                print the app's Pokemon game state scene whenever it changes, and the\n"
        "                              GPU 2D shadow's mode, which a Gen 4 game's battles turn on and off\n"
        "  --no-wide-battles           with --wide, Gen 4 Pokemon battles stop at the edge as in other games\n"
        "  --screenshot FILE.bmp       save the last frame at the internal resolution when quitting\n"
        "  --window-shot FILE.bmp      save the window as drawn, menus included, when --frames or --ui-script ends\n"
        "                              the run; detached: the windows as FILE_top.bmp and FILE_bottom.bmp\n"
        "  --window-shot-linear        draw the window shot with the linear filter (the run keeps its own)\n"
        "  --depth-shot FILE.png       save the last frame's top screen depth (OpenGL only) when --frames or --ui-script\n"
        "                              ends the run, near dark to far light, and print how many depth values it has\n"
        "  --depth-raw FILE.png        the same frame's depth texture as it is (RGB the 24-bit depth, A the flags)\n"
        "  --camera-dump FILE          debug: a JSON line per 3D scene with the projection matrices (20.12) and\n"
        "                              viewports its polygons were built under, how many polygons each, and the pick\n"
        "  --camera-shown FILE         debug: a JSON line per presented frame with the depth camera it came with\n"
        "  --hidden                    no visible window (with --frames); never fullscreen, saves no window places\n"
        "  --save-window-places        save the window places with --hidden too (tests)\n"
        "  --log FILE                  core log file (default melonade.log next to the exe)\n"
        "  --cache-dir DIR             Compute program cache directory\n"
        "  --verbose                   debug log lines on the console too\n"
        "\n"
        "  --open SCREEN               open a menu screen at the start: library (or start), library/details or\n"
        "                              library/settings (the first game's details or game settings, once the scan\n"
        "                              is done), pause, pause/save, pause/load,\n"
        "                              settings, settings/video (display, emulation, audio, controls, library),\n"
        "                              input, keyboard (a text field and the on-screen keyboard); with a\n"
        "                              game, once its first frame is shown\n"
        "  --rom-folder DIR            a folder for the game library (repeat it for more), for this run only:\n"
        "                              in place of the saved ones (rom_search_dirs), never written\n"
        "  --rom-config KEY=VALUE      over the game's own settings (library.db's rom_config) for this run, for\n"
        "                              whichever game boots (repeat it for more): console=default|ds|dsi,\n"
        "                              gba-slot=none|gba-rom|rumble-pak|memory-expansion, gba-rom=FILE.gba,\n"
        "                              name=TEXT; never stored. --console and --gba beat the stored ones too\n"
        "  --pretend-title-art         every game counts as having title-screen art, so the Mosaic view gives one in\n"
        "                              six of them a wide tile (without downloaded art)\n"
        "  --pretend-wide-tiles N      one in N games with title-screen art (downloaded, or with --pretend-title-art)\n"
        "                              is wide, picked by path instead of the ROM hash (a library of copies of a few\n"
        "                              ROMs has a few hashes), to check the packing with many wide tiles\n"
        "  --no-sync                   no network at all for this run: the metadata and art sync makes no requests\n"
        "                              (the art already downloaded still shows, and ROM hashes are still computed)\n"
        "  --sync-now                  scan the library and sync its metadata at the start, without opening the\n"
        "                              library, with RetroAchievements' hash library fetched whatever its age\n"
        "  --ra-web-api-key KEY        a RetroAchievements Web API key for this run only, never stored\n"
        "  --metadata-base-url URL     RetroAchievements' hosts for this run (a test server): URL/dorequest.php,\n"
        "                              URL/API/API_GetGame.php and URL/Images/... in place of retroachievements.org\n"
        "                              and media.retroachievements.org\n"
        "  --thumbnails-base-url URL   the libretro thumbnails' host for this run: URL/Named_Boxarts/<name>.png and\n"
        "                              URL/Named_Titles/<name>.png\n"
        "  --dat-url URL               the No-Intro DS DAT's address for this run (default: libretro-database's)\n"
        "  --sync-backoff S            a sync that stopped early tries again after S seconds, then twice that, ...\n"
        "                              (default 900, the app's 15 minutes), for tests\n"
        "  --ui-script FILE            drive the menus from FILE, one step a line: up, down, left, right, confirm,\n"
        "                              back, tab-next, tab-prev, menu, text STRING, key NAME, pad NAME, wait N,\n"
        "                              wait-scan (until the library's scan is done), wait-sync (until the metadata\n"
        "                              sync is idle and its art drawn), frame N (the game held at its\n"
        "                              frame N), shot FILE.bmp, screenshot FILE.bmp, quit. It starts on the library\n"
        "                              or once the game's first frame is shown, and the run ends after its last line\n"
        "                              (README: UI scripts)\n"
        "\n"
        "  --virtual-pad xbox|switch|ps|generic  an SDL virtual controller (generic: one with no mapping), for --ui-script's\n"
        "                              pad and axis steps\n"
        "  --audio-log                 the sound's buffer fill and ratio once a second\n"
        "  --audio-wav FILE.wav        what the audio callback plays, before the volume\n"
        "  --audio-trace FILE.csv      every callback's and frame's buffer fill, with the times\n"
        "  --console ds|dsi            the console (DSi needs --bios and --dsi-bios: bios9/7, firmware, nand.bin)\n"
        "  --dsi-bios DIR              the DSi BIOS folder\n"
        "  --sharper-rotated-2d, --gl-seams, --no-edge-marking   the app's renderer settings\n"
        "  --gba rumble                the Rumble Pak in the GBA slot\n"
        "\n"
        "keys (the input mapping's defaults; Settings > Controls changes them, input.json keeps them):\n"
        "      arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Right Shift = Select,\n"
        "      Esc = pause menu, Tab (hold) or ` = fast forward, Ctrl+S / Ctrl+L = quick save / load,\n"
        "      F1 = swap screens, F9 = screens together/detached, F11 = fullscreen of the focused window,\n"
        "      F12 = screenshot, mouse on the bottom screen = touch\n"
        "      fixed: P = pause, Ctrl+R = reset, F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up,\n"
        "      F2 = layout, F8 = linear filter on/off, F10 = fit of the focused detached window\n"
        "      menus: arrows move, Enter chooses, Esc or Backspace goes back, Delete clears, Q/W switch tabs\n"
        "      controllers: Guide or Select+Start held = pause menu, the right stick moves a touch cursor and the\n"
        "      right trigger taps; face buttons by the pad's layout (Nintendo: east = A, others: south = A)\n");
}

using Frontend::ApplySet;
using Frontend::ReadFile;

bool ParseArgs(int argc, char** argv, Options& opts)
{
    bool layoutGiven = false, detachGiven = false;
    // --bloom's glow and vignette, --bloom-all and --haze each set their part, in any order
    Frontend::Bloom bloom;
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
        else if (arg == "--precise-depth") setting("video_precise_depth", picojson::value(true));
        else if (arg == "--no-high-res-coords") opts.HighResCoordinates = false;
        else if (arg == "--dither") opts.ColorDither = true;
        else if (arg == "--hires-capture") setting("video_hires_capture", picojson::value(true));
        else if (arg == "--filter2d") setting("video_filter_2d", picojson::value(true));
        else if (arg == "--filter2d-mode")
        {
            int mode = std::clamp(atoi(next().c_str()), 0, 3);
            setting("video_filter_2d", picojson::value(mode != 0));
            opts.Filter2DMode = mode >= 2 ? mode : -1;
        }
        else if (arg == "--sharper-rotated-2d") setting("video_sharper_rotated_2d", picojson::value(true));
        else if (arg == "--gl-seams") setting("video_gl_seams", picojson::value(true));
        else if (arg == "--no-edge-marking") setting("video_edge_marking", picojson::value(false));
        else if (arg == "--gpu2d") opts.Gpu2D = std::clamp(atoi(next().c_str()), 0, 4);
        else if (arg == "--gpu2d-wide") opts.Gpu2DWide = 1;
        else if (arg == "--gpu2d-affine") opts.Gpu2DAffine = std::clamp(atoi(next().c_str()), -1, 2);
        else if (arg == "--smooth30") opts.HalfCamera = true;
        else if (arg == "--halfcam") opts.HalfCameraMode = std::clamp(atoi(next().c_str()), 0, 5);
        else if (arg == "--no-threaded2d") opts.Threaded2D = false;
        else if (arg == "--no-threaded-soft") opts.ThreadedSoft3D = false;
        else if (arg == "--jit") setting("enable_jit", picojson::value(true));
        else if (arg == "--no-jit") setting("enable_jit", picojson::value(false));
        else if (arg == "--fastmem") { setting("enable_jit", picojson::value(true)); opts.FastMem = true; }
        else if (arg == "--ldmdirect") opts.LdmDirect = atoi(next().c_str()) ? 1 : 0;
        else if (arg == "--bios") { setting("bios_dir", text(next().c_str())); setting("use_custom_bios", picojson::value(true)); }
        else if (arg == "--console") setting("console_type", text(next().c_str()));
        else if (arg == "--dsi-bios") setting("dsi_bios_dir", text(next().c_str()));
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
        else if (arg == "--boot-screen") setting("show_bios", picojson::value(true));
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
        else if (arg == "--filter")
        {
            std::string name = next();
            Frontend::VideoFiltering filtering = Frontend::VideoFiltering::None;
            if (!Frontend::ParseVideoFiltering(name, filtering)) { fprintf(stderr, "unknown filter %s (--help lists them)\n", name.c_str()); return false; }
            // stored as the app stores it, the enum's name in lower case
            std::string value = Frontend::VideoFilteringName(filtering);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)tolower(c); });
            setting("video_filtering", picojson::value(value));
        }
        else if (arg == "--deband" || arg == "--ds-colors" || arg == "--screen-shadow")
        {
            std::string value = next();
            if (value != "on" && value != "off") { fprintf(stderr, "%s needs on or off\n", arg.c_str()); return false; }
            setting(arg == "--deband" ? "video_deband" : arg == "--ds-colors" ? "video_ds_screen_colors" : "video_screen_shadow",
                picojson::value(value == "on"));
        }
        else if (arg == "--tilt-shift")
        {
            std::string spec = next();
            Frontend::TiltShift tilt;
            float color = -1;
            int count = sscanf(spec.c_str(), "%f,%f,%f,%f,%f,%f,%f,%f", &tilt.Top, &tilt.Bottom, &tilt.Center, &tilt.Width, &tilt.Radius,
                &tilt.Saturation, &tilt.Contrast, &color);
            if (count != 5 && count != 7 && count != 8)
            {
                fprintf(stderr, "bad --tilt-shift %s (TOP,BOTTOM,CENTER,WIDTH,RADIUS[,SATURATION,CONTRAST[,COLOR]])\n", spec.c_str());
                return false;
            }
            // held to what a script could ask for: the fades, the band and the color in 0-1, the radius to a test-sized 64 DS pixels
            tilt.Top = std::clamp(tilt.Top, 0.f, 1.f);
            tilt.Bottom = std::clamp(tilt.Bottom, 0.f, 1.f);
            tilt.Center = std::clamp(tilt.Center, 0.f, 1.f);
            tilt.Width = std::clamp(tilt.Width, 0.f, 1.f);
            tilt.Radius = std::clamp(tilt.Radius, 0.f, 64.f);
            tilt.Saturation = std::clamp(tilt.Saturation, 0.f, 4.f);
            tilt.Contrast = std::clamp(tilt.Contrast, 0.f, 4.f);
            if (count == 8)
                tilt.Color = std::clamp(color, 0.f, 1.f);
            opts.TiltShift = tilt;
        }
        else if (arg == "--bloom")
        {
            std::string spec = next();
            Frontend::Bloom given;
            int count = sscanf(spec.c_str(), "%f,%f,%f,%f,%f", &given.Strength, &given.Threshold, &given.Radius, &given.Vignette,
                &given.VignetteSize);
            if (count != 1 && count != 3 && count != 5)
            {
                fprintf(stderr, "bad --bloom %s (STRENGTH[,THRESHOLD,RADIUS[,VIGNETTE,SIZE]])\n", spec.c_str());
                return false;
            }
            // held to what a script could ask for (melon.screen.bloom and vignette)
            bloom.Strength = std::clamp(given.Strength, 0.f, 2.f);
            bloom.Threshold = std::clamp(given.Threshold, 0.f, 1.f);
            bloom.Radius = std::clamp(given.Radius, 0.f, 8.f);
            bloom.Vignette = std::clamp(given.Vignette, 0.f, 1.f);
            bloom.VignetteSize = std::clamp(given.VignetteSize, 0.f, 1.f);
        }
        else if (arg == "--bloom-all") bloom.OnlyThreeD = false;
        else if (arg == "--haze")
        {
            std::string spec = next();
            Frontend::Bloom given;
            unsigned int color = given.HazeColor;
            int count = sscanf(spec.c_str(), "%f,%f,%f,%x", &given.Haze, &given.HazeStart, &given.HazeFalloff, &color);
            if (count != 1 && count != 3 && count != 4)
            {
                fprintf(stderr, "bad --haze %s (STRENGTH[,START,FALLOFF[,RRGGBB]])\n", spec.c_str());
                return false;
            }
            // held to what a script could ask for (melon.screen.haze)
            bloom.Haze = std::clamp(given.Haze, 0.f, 1.f);
            bloom.HazeStart = std::clamp(given.HazeStart, 0.f, 1.f);
            bloom.HazeFalloff = std::clamp(given.HazeFalloff, 0.f, 1.f);
            bloom.HazeColor = color & 0xFFFFFF;
        }
        else if (arg == "--dof")
        {
            std::string spec = next();
            Frontend::DepthOfField dof;
            int count = sscanf(spec.c_str(), "%f,%f,%f,%f,%f,%f,%f", &dof.Strength, &dof.Radius, &dof.Range, &dof.Falloff, &dof.FocusX, &dof.FocusY,
                &dof.FocusDepth);
            if (count != 2 && count != 4 && count != 6 && count != 7)
            {
                fprintf(stderr, "bad --dof %s (STRENGTH,RADIUS[,RANGE,FALLOFF[,FOCUS_X,FOCUS_Y[,FOCUS_DEPTH]]])\n", spec.c_str());
                return false;
            }
            // held to what a script could ask for (melon.screen.depth_of_field)
            dof.Strength = std::clamp(dof.Strength, 0.f, 1.f);
            dof.Radius = std::clamp(dof.Radius, 0.f, 12.f);
            dof.Range = std::clamp(dof.Range, 0.f, 1.f);
            dof.Falloff = std::clamp(dof.Falloff, 0.f, 1.f);
            dof.FocusX = std::clamp(dof.FocusX, -256.f, 512.f);
            dof.FocusY = std::clamp(dof.FocusY, -192.f, 384.f);
            dof.FocusDepth = count == 7 ? std::clamp(dof.FocusDepth, 0.f, 1.f) : -1.f;
            opts.DepthOfField = dof.IsOff() ? std::nullopt : std::optional<Frontend::DepthOfField>(dof);
        }
        else if (arg == "--shade")
        {
            std::string spec = next();
            Frontend::Shade shade;
            unsigned int color = shade.Color;
            int count = sscanf(spec.c_str(), "%f,%f,%f,%x", &shade.Strength, &shade.Radius, &shade.Range, &color);
            if (count != 1 && count != 3 && count != 4)
            {
                fprintf(stderr, "bad --shade %s (STRENGTH[,RADIUS,RANGE[,RRGGBB]])\n", spec.c_str());
                return false;
            }
            // held to what a script could ask for (melon.screen.shade)
            shade.Strength = std::clamp(shade.Strength, 0.f, 1.f);
            shade.Radius = std::clamp(shade.Radius, 1.f, 16.f);
            shade.Range = std::clamp(shade.Range, 0.f, 1.f);
            shade.Color = color & 0xFFFFFF;
            opts.Shade = shade.IsOff() ? std::nullopt : std::optional<Frontend::Shade>(shade);
        }
        else if (arg == "--output-cap") opts.OutputCap = true;
        else if (arg == "--vsync") setting("desktop_vsync", picojson::value(true));
        else if (arg == "--unthrottled") opts.Unthrottled = true;
        else if (arg == "--no-audio") setting("sound_enabled", picojson::value(false));
        else if (arg == "--audio-wav") opts.AudioWav = next();
        else if (arg == "--audio-log") opts.AudioLog = true;
        else if (arg == "--audio-trace") opts.AudioTrace = next();
        else if (arg == "--virtual-pad") opts.VirtualPad = next();
        else if (arg == "--frames") opts.Frames = strtoul(next().c_str(), nullptr, 0);
        else if (arg == "--game-state") opts.GameState = true;
        else if (arg == "--no-wide-battles") opts.WideBattles = false;
        else if (arg == "--screenshot") opts.ScreenshotPath = next();
        else if (arg == "--window-shot") opts.WindowShotPath = next();
        else if (arg == "--window-shot-linear") opts.WindowShotLinear = true;
        else if (arg == "--depth-shot") opts.DepthShotPath = next();
        else if (arg == "--depth-raw") opts.DepthRawPath = next();
        else if (arg == "--camera-dump") opts.CameraDumpPath = next();
        else if (arg == "--camera-shown") opts.CameraShownPath = next();
        else if (arg == "--hidden") opts.Hidden = true;
        else if (arg == "--gpu-sync") opts.GpuSync = true;
        else if (arg == "--log") opts.LogPath = next();
        else if (arg == "--cache-dir") opts.CacheDir = next();
        else if (arg == "--verbose") Frontend::VerboseLog = true;
        else if (arg == "--open") opts.OpenScreen = next();
        else if (arg == "--ui-script") opts.UiScriptPath = next();
        else if (arg == "--state-dir") opts.StateDir = next();
        else if (arg == "--rom-folder") opts.RomFolders.push_back(next());
        else if (arg == "--rom-config")
        {
            std::string spec = next();
            size_t eq = spec.find('=');
            std::string error;
            Frontend::RomConfig check;
            if (eq == std::string::npos || !check.Apply(spec.substr(0, eq), spec.substr(eq + 1), error))
            {
                fprintf(stderr, "bad --rom-config %s (KEY=VALUE: %s)\n", spec.c_str(), error.empty() ? "no =" : error.c_str());
                return false;
            }
            opts.RomConfigOverrides.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
        }
        else if (arg == "--pretend-title-art") opts.PretendTitleArt = true;
        else if (arg == "--no-sync") opts.NoSync = true;
        else if (arg == "--sync-now") opts.SyncNow = true;
        else if (arg == "--metadata-base-url") opts.MetadataBaseUrl = next();
        else if (arg == "--thumbnails-base-url") opts.ThumbnailsBaseUrl = next();
        else if (arg == "--dat-url") opts.DatUrl = next();
        else if (arg == "--ra-web-api-key") opts.RaWebApiKey = next();
        else if (arg == "--sync-backoff") opts.SyncBackoffMs = std::max(1, atoi(next().c_str())) * 1000;
        else if (arg == "--pretend-wide-tiles")
        {
            opts.PretendWideOneIn = std::max(1, atoi(next().c_str()));
        }
        else if (arg == "-h" || arg == "--help") { Usage(); exit(0); }
        else if (arg[0] == '-') { fprintf(stderr, "unknown option %s\n", arg.c_str()); return false; }
        else opts.RomPath = arg;
    }
    // as the app's LuaBloomRequests, which drops a request for none of them
    opts.Bloom = bloom.IsOff() ? std::nullopt : std::optional<Frontend::Bloom>(bloom);
    // a --layout is the screens together, unless --detach asks for them detached
    if (layoutGiven && !detachGiven)
        opts.SettingOverrides.emplace_back("desktop_screens", picojson::value(std::string("together")));
    // the library's folders for the run, in place of the saved ones
    if (!opts.RomFolders.empty())
    {
        picojson::array folders;
        for (const std::string& folder : opts.RomFolders)
            folders.emplace_back(Frontend::Library::NormalizePath(folder));
        opts.SettingOverrides.emplace_back("rom_search_dirs", picojson::value(folders));
    }
    return true;
}

// the renderer settings from the settings (with the command line's in them); the desktop's defaults stay the test frontend's
Options::RenderToggles ReadToggles(const Options& opts, const Frontend::Settings& settings)
{
    Options::RenderToggles t;
    t.HiresCapture = settings.GetBool("video_hires_capture", false);
    t.Filter2D = settings.GetBool("video_filter_2d", false) ? (opts.Filter2DMode >= 2 ? opts.Filter2DMode : 1) : 0;
    t.SharperRotated2D = settings.GetBool("video_sharper_rotated_2d", false);
    t.EdgeMarking = settings.GetBool("video_edge_marking", true);
    t.PreciseDepth = settings.GetBool("video_precise_depth", false);
    t.GlSeams = settings.GetBool("video_gl_seams", false);
    return t;
}

// the screens' look from the settings, with the app's keys and defaults. The app took its DS screen colors switch out and reads it as off
// whatever is stored; here it's a setting of its own still
Frontend::RendererConfig ReadRendererConfig(const Frontend::Settings& settings)
{
    Frontend::RendererConfig config;
    if (!Frontend::ParseVideoFiltering(settings.GetString("video_filtering", "none"), config.Filtering))
        config.Filtering = Frontend::VideoFiltering::None;
    config.Deband = settings.GetBool("video_deband", false);
    config.DsScreenColors = settings.GetBool("video_ds_screen_colors", false);
    config.ScreenShadow = settings.GetBool("video_screen_shadow", false);
    return config;
}

// fast_forward_speed_multiplier: the app's list values as text ("-1" unlimited, "1.5", "2", ...), 0 for unlimited
float FastForwardSpeed(const Frontend::Settings& settings)
{
    float speed = (float)atof(settings.GetString("fast_forward_speed_multiplier", "-1").c_str());
    return speed > 0 ? std::clamp(speed, 1.f, 16.f) : 0.f;
}

// the options that are settings, from the settings (with the command line's overrides in them). The desktop's defaults stay the test
// frontend's (Compute at 4x, no hi-res capture, no JIT), not the app's
void ApplySettings(Options& opts, const Frontend::Settings& settings)
{
    std::string renderer = settings.GetString("video_renderer", "compute");
    opts.Renderer = renderer == "software" ? RendererType::Software : renderer == "opengl" ? RendererType::OpenGL : RendererType::Compute;
    opts.Scale = std::clamp(settings.GetInt("video_internal_resolution", 4), 1, 16);
    opts.Render = ReadRendererConfig(settings);
    opts.Audio = settings.GetBool("sound_enabled", true);
    opts.VSync = settings.GetBool("desktop_vsync", false);
    opts.Toggles = ReadToggles(opts, settings);
    // the interpreter by default here (the JIT is slower on x86-64, see the README), where the app's default is on
    opts.Jit = settings.GetBool("enable_jit", false);
    // FreeBIOS unless the custom BIOS is on and its folder set (--bios sets both)
    bool custom = settings.GetBool("use_custom_bios", false);
    opts.BiosDir = custom ? settings.GetString("bios_dir", "") : "";
    opts.DsiBiosDir = settings.GetString("dsi_bios_dir", "");
    opts.BootScreen = settings.GetBool("show_bios", false);
    opts.ConsoleType = settings.GetString("console_type", "ds") == "dsi" ? 1 : 0;
    opts.FastForwardSpeed = FastForwardSpeed(settings);
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
    config->fastForwardSpeedMultiplier = opts.FastForwardSpeed > 0 ? opts.FastForwardSpeed : -1.0f;
    config->framePacingVsync = false;
    config->framePacingBuffer = false;
    config->framePacingSmooth30Fps = opts.HalfCamera;
    config->showBootScreen = opts.BootScreen;
    config->useJit = opts.Jit;
    config->syncRtcToDevice = false;
    config->consoleType = opts.ConsoleType;
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
        // the app's "Smooth gradients" (deband) dithers Compute's colors too (RendererConfiguration.colorDithering)
        settings->colorDithering = opts.ColorDither || opts.Render.Deband;
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
    config->hiresCapture = opts.Toggles.HiresCapture;
    config->filter2d = opts.Toggles.Filter2D != 0;
    config->sharperRotated2d = opts.Toggles.SharperRotated2D;
    // --set PreciseDepth wins, as it did when it was applied after --precise-depth
    config->preciseDepth = SetValue(opts.Sets, "PreciseDepth", opts.Toggles.PreciseDepth) != 0;
    // on by default, the core's default (GPU::GLEdgeMark 1); --set GLEdgeMark goes through the switches
    config->edgeMarking = opts.Toggles.EdgeMarking;
    config->glSeams = opts.Toggles.GlSeams;
    config->luaDrawingsInScreenshots = false;
    config->wideViewWidth = opts.WideWidth;
    config->wideViewMargin = opts.WideMargin;
    config->dsiSdCardSettings = {};
    config->dldiSdCardSettings = {};
    config->dsiSdCardSettings.imagePath = config->dsiSdCardSettings.folderPath = empty;
    if (opts.ConsoleType == 1)
    {
        // BuildArgsFromConfiguration reads these for a DSi (they stay as long as the options do)
        config->dsBios9Path = const_cast<char*>(opts.Dsi.DsBios9.c_str());
        config->dsBios7Path = const_cast<char*>(opts.Dsi.DsBios7.c_str());
        config->dsiBios9Path = const_cast<char*>(opts.Dsi.Bios9.c_str());
        config->dsiBios7Path = const_cast<char*>(opts.Dsi.Bios7.c_str());
        config->dsiFirmwarePath = const_cast<char*>(opts.Dsi.Firmware.c_str());
        config->dsiNandPath = const_cast<char*>(opts.Dsi.Nand.c_str());
    }
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
    nds.GPU.GPU3D.SetCameraLogging(!opts.CameraDumpPath.empty());
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
    if (opts.Toggles.Filter2D >= 2)
        overrides.filter2d = opts.Toggles.Filter2D;
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

bool LoadStateBytes(const Options& opts, MelonDSAndroid::MelonInstance& emu, const std::vector<u8>& data, const std::string& name)
{
    // MelonInstance's load backs up the state before and goes back to it if the load fails partway. The save it holds is only taken
    // into memory (SaveManager::SyncFromState), so the .sav on disk stays as it is until the game saves again
    Savestate state(const_cast<u8*>(data.data()), (u32)data.size(), false);
    bool loaded = emu.loadState(&state);
    if (!loaded)
    {
        // a state without the RetroAchievements section MelonInstance::saveState writes first (the app's and this frontend's states have
        // it), as the desktop saved them before the shared code: the core's part alone. Only then: a state that has the section failed in
        // its core part, and loadState already put the game back as it was, which a second try would undo. The core doesn't stop at the
        // first error either, so this load gets a backup of its own to go back to
        Savestate raw(const_cast<u8*>(data.data()), (u32)data.size(), false);
        if (!raw.Error && !raw.HasSection("RCHV"))
        {
            Savestate backup;
            bool backedUp = !backup.Error && emu.saveState(&backup) && !backup.Error;
            if (backedUp)
            {
                Savestate rawLoad(const_cast<u8*>(data.data()), (u32)data.size(), false);
                loaded = !rawLoad.Error && emu.getNds()->DoSavestate(&rawLoad) && !rawLoad.Error;
                if (loaded)
                    printf("state: %s has no RetroAchievements section, loaded the core's part\n", name.c_str());
                else
                {
                    Savestate back(backup.Buffer(), backup.Length(), false);
                    if (back.Error || !emu.loadState(&back))
                        fprintf(stderr, "savestate %s: the game couldn't be put back as it was\n", name.c_str());
                }
            }
        }
    }
    if (!loaded)
    {
        fprintf(stderr, "savestate %s didn't load\n", name.c_str());
        return false;
    }
    SetDateTime(opts, *emu.getNds());
    printf("state: %s\n", name.c_str());
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
    return LoadStateBytes(opts, emu, data, path);
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

// a slot's state, as the app writes one: the file only opened once the state is made, so a failed save keeps the slot's old state (and
// through a temporary file here, so a failed write does too)
bool SaveStateToPath(MelonDSAndroid::MelonInstance& emu, const fs::path& path)
{
    Savestate state;
    if (state.Error || !emu.saveState(&state) || state.Error)
    {
        fprintf(stderr, "can't make a savestate\n");
        return false;
    }
    fs::path temp = path;
    temp += ".tmp";
    bool ok;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        ok = out && out.write((const char*)state.Buffer(), state.Length()) && out.flush();
    }
    std::error_code ec;
    if (ok)
        fs::rename(temp, path, ec);
    if (!ok || ec)
    {
        fs::remove(temp, ec);
        fprintf(stderr, "can't write savestate %s\n", Frontend::PathToUtf8(path).c_str());
        return false;
    }
    printf("saved state: %s (%u bytes)\n", Frontend::PathToUtf8(path).c_str(), state.Length());
    return true;
}

bool ReadPath(const fs::path& path, std::vector<u8>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0)
        return false;
    out.resize((size_t)size);
    return (bool)in.read((char*)out.data(), size);
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

// a frame's depth camera (Frame::depthProjection and the rest, as FrameView has them) as JSON members, for --camera-shown and
// --depth-raw: whether the frame has depth, whether the camera is valid, the projection's 16 floats column-major and the viewport's
// left, top, width and height. %.9g gives a float back exactly
void PrintDepthCamera(FILE* file, bool hasDepth, bool valid, const std::array<float, 16>& proj, const std::array<float, 4>& viewport)
{
    fprintf(file, "\"depth\":%d,\"valid\":%d,\"proj\":[", hasDepth ? 1 : 0, valid ? 1 : 0);
    for (int i = 0; i < 16; i++)
        fprintf(file, "%s%.9g", i ? "," : "", proj[i]);
    fprintf(file, "],\"vp\":[%.9g,%.9g,%.9g,%.9g]", viewport[0], viewport[1], viewport[2], viewport[3]);
}

// --depth-shot: [frame]'s top screen depth (GLCompositor::RenderDepth's 24-bit values and flags) into a grayscale PNG at [path], the 3D
// from near (dark) to far (light) over the range it covers, 2D on top black and transparent 3D white, and its stats printed: how many
// distinct depth values the 3D has, and how many of them the old 16-bit depth could tell apart. With [rawPath] (--depth-raw) the texture
// as it is too, top row first, for scripts; an empty [path] writes only that
void DepthShot(const Frame& frame, const std::string& path, const std::string& rawPath)
{
    if (!frame.hasDepth || !frame.depthTexture)
    {
        fprintf(stderr, "--depth-shot: the frame has no depth (OpenGL only, and not on its first frame)\n");
        return;
    }
    const int width = (int) frame.depthWidth, height = (int) frame.depthHeight;
    std::vector<u8> texels((size_t) width * height * 4);
    GLuint framebuffer = 0;
    GLint previous = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame.depthTexture, 0);
    // into client memory, at the alignment the other readbacks set for themselves (RGBA rows are 4-aligned anyway)
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint) previous);
    glDeleteFramebuffers(1, &framebuffer);

    if (!rawPath.empty())
    {
        const bool rawWritten = Frontend::WritePng(Frontend::PathFromUtf8(rawPath), texels, width, height);
        printf("depth raw: %s%s, %dx%d\n", rawPath.c_str(), rawWritten ? "" : " (not written)", width, height);
        // the camera this depth came with, for rebuilding positions from it
        printf("depth camera: {");
        PrintDepthCamera(stdout, frame.hasDepth, frame.depthCameraValid, frame.depthProjection, frame.depthViewport);
        printf("}\n");
    }
    if (path.empty())
        return;

    std::vector<u32> depths;
    u32 count2D = 0, countBehind = 0, countW = 0, countOther = 0, nearest = 0xFFFFFF, farthest = 0;
    for (size_t i = 0; i < texels.size(); i += 4)
    {
        const u8 flags = texels[i + 3];
        const u32 depth = (u32) texels[i] << 16 | (u32) texels[i + 1] << 8 | texels[i + 2];
        if (flags & 4)
            countW++;
        if (flags & 8)
            countOther++;
        if (flags & 1)
            count2D++;
        else if (flags & 2)
            countBehind++;
        else
        {
            depths.push_back(depth);
            nearest = std::min(nearest, depth);
            farthest = std::max(farthest, depth);
        }
    }

    // the PNG, top row first, as the texture's rows are (the compositor draws its top line into row 0)
    std::vector<u8> rgba((size_t) width * height * 4);
    const double span = farthest > nearest ? (double) (farthest - nearest) : 1.0;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            const u8* texel = &texels[((size_t) y * width + x) * 4];
            u8* pixel = &rgba[((size_t) y * width + x) * 4];
            const u32 depth = (u32) texel[0] << 16 | (u32) texel[1] << 8 | texel[2];
            u8 gray = 0;
            if (texel[3] & 1)
                gray = 0;
            else if (texel[3] & 2)
                gray = 255;
            else
                gray = (u8) (16 + 223 * ((double) (depth - nearest) / span));
            pixel[0] = pixel[1] = pixel[2] = gray;
            pixel[3] = 255;
        }
    }
    const bool written = Frontend::WritePng(Frontend::PathFromUtf8(path), rgba, width, height);

    std::sort(depths.begin(), depths.end());
    const size_t pixels3D = depths.size();
    depths.erase(std::unique(depths.begin(), depths.end()), depths.end());
    size_t distinct16 = 0;
    for (size_t i = 0; i < depths.size(); i++)
    {
        if (i == 0 || depths[i] >> 8 != depths[i - 1] >> 8)
            distinct16++;
    }
    printf("depth shot: %s%s, %dx%d; 3D %zu px, depth 0x%06X..0x%06X, %zu distinct values (%zu at 16 bits); 2D %u px, "
           "transparent 3D %u px, W-buffered %u px, other camera %u px\n",
           path.c_str(), written ? "" : " (not written)", width, height, pixels3D, pixels3D ? nearest : 0, farthest, depths.size(),
           distinct16, count2D, countBehind, countW, countOther);
}

// --camera-dump: the 3D scene the core latched last (GPU3D::CameraLog), as a JSON line into [file], once per swap: the frame, the
// scene's polygons, every projection matrix (raw 20.12) and viewport (x0, y0, x1, y1, width, height) they were built under with their
// polygon counts, the polygons past the table's room, and which of those the core picked for the depth (-1 none) and whether it's valid
void DumpCameraScene(FILE* file, u32 frame, const GPU3D::CameraLog& log)
{
    const int pick = log.Pick.Index == GPU3D::NoSceneCamera ? -1 : (int) log.Pick.Index;
    fprintf(file, "{\"frame\":%u,\"swap\":%u,\"polys\":%u,\"other\":%u,\"pick\":%d,\"valid\":%d,\"uses\":[", frame, log.Swaps,
            log.Polygons, log.Other, pick, log.Pick.Valid ? 1 : 0);
    for (u32 i = 0; i < log.NumCameras; i++)
    {
        const GPU3D::CameraCount& use = log.Cameras[i];
        fprintf(file, "%s{\"polys\":%u,\"vp\":[%u,%u,%u,%u,%u,%u],\"proj\":[", i ? "," : "", use.Polygons, use.Viewport[0],
                use.Viewport[1], use.Viewport[2], use.Viewport[3], use.Viewport[4], use.Viewport[5]);
        for (int j = 0; j < 16; j++)
            fprintf(file, "%s%d", j ? "," : "", use.Proj[j]);
        fprintf(file, "]}");
    }
    fprintf(file, "]}\n");
}

// The exe is a GUI program, so it has no console of its own. Started from one (PowerShell, cmd, bash) it writes there: to the handles it
// was given when they're redirected or piped (a file, mintty's pipe), else to the parent's console, attached. A double-clicked exe has
// neither, and its output goes nowhere, as before. True when it attached; [outConsole] and [errConsole] say which streams it reopened
// to that console (a redirected one keeps its file or pipe)
bool AttachParentConsole(bool& outConsole, bool& errConsole)
{
    auto usable = [](DWORD which)
    {
        HANDLE handle = GetStdHandle(which);
        return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
    };
    bool out = usable(STD_OUTPUT_HANDLE), err = usable(STD_ERROR_HANDLE);
    outConsole = errConsole = false;
    if (out && err)
        return false;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return false;
    if (!out)
        outConsole = freopen("CONOUT$", "w", stdout) != nullptr;
    if (!err)
        errConsole = freopen("CONOUT$", "w", stderr) != nullptr;
    return true;
}

// A run to play (not a test, nor --help): the parent's console attached above goes again, so Ctrl+C typed there, or the console closing,
// can't end the game (PowerShell doesn't wait for a GUI program, so its prompt is back and takes keys meanwhile). What's printed from here
// on to the console goes nowhere, as for a double-clicked exe; a redirected stream (a file, a pipe) keeps going there, also when the other
// one was the console (game.nds > log.txt)
void DetachParentConsole(bool outConsole, bool errConsole)
{
    fflush(stdout);
    fflush(stderr);
    if (outConsole)
        freopen("NUL", "w", stdout);
    if (errConsole)
        freopen("NUL", "w", stderr);
    FreeConsole();
}

// the recent games in the settings (desktop_recent_roms), newest first
constexpr size_t kMaxRecent = 10;

std::vector<std::string> RecentRoms(const Frontend::Settings& settings)
{
    std::vector<std::string> list;
    const picojson::value* value = settings.Find("desktop_recent_roms");
    if (value && value->is<picojson::array>())
    {
        for (const picojson::value& item : value->get<picojson::array>())
        {
            if (item.is<std::string>() && !item.get<std::string>().empty())
                list.push_back(item.get<std::string>());
        }
    }
    return list;
}

void AddRecentRom(Frontend::Settings& settings, const std::string& path)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(Frontend::PathFromUtf8(path), ec);
    std::string full = Frontend::PathToUtf8(ec ? Frontend::PathFromUtf8(path) : absolute.lexically_normal());
    picojson::array list;
    list.emplace_back(full);
    for (const std::string& item : RecentRoms(settings))
    {
        if (_stricmp(item.c_str(), full.c_str()) != 0 && list.size() < kMaxRecent)
            list.emplace_back(item);
    }
    settings.Set("desktop_recent_roms", picojson::value(list));
}

}

int main(int argc, char** argv)
{
    bool outConsole = false, errConsole = false;
    bool attached = AttachParentConsole(outConsole, errConsole);
    Options opts;
    if (!ParseArgs(argc, argv, opts))
    {
        Usage();
        return 1;
    }
    // the runs whose output is the point keep the console: tests and scripted runs, and --verbose
    bool testRun = opts.Hidden || opts.Frames != 0 || !opts.UiScriptPath.empty() || Frontend::VerboseLog;
    // (MELONADE_TEST_DETACH_CONSOLE=1 lets go of it in a test run too, to check that path in a hidden one)
    const char* forceDetach = getenv("MELONADE_TEST_DETACH_CONSOLE");
    if (forceDetach && forceDetach[0] == '1')
        printf("console: %s (stdout %s, stderr %s)\n", attached ? "attached" : "not attached", outConsole ? "the console" : "its own",
            errConsole ? "the console" : "its own");
    if (attached && (!testRun || (forceDetach && forceDetach[0] == '1')))
    {
        printf("console: let go of the parent's console (stdout %s, stderr %s)\n", outConsole ? "was the console" : "kept",
            errConsole ? "was the console" : "kept");
        DetachParentConsole(outConsole, errConsole);
    }
    std::vector<Frontend::UiStep> script;
    if (!opts.UiScriptPath.empty() && !Frontend::LoadUiScript(opts.UiScriptPath, script))
        return 1;

    // Windows 11 slows down processes whose window isn't in front (EcoQoS) and ignores their timer
    // requests, which costs 2/3 of the frame rate: opt out of both, then ask for 1 ms sleeps for
    // the frame pacing instead of the default 15.6 ms
    PROCESS_POWER_THROTTLING_STATE throttling {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | kIgnoreTimerResolution;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
    timeBeginPeriod(1);
    // COM on the UI thread, for the file dialog and the slot screenshots (WIC)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0"); // the driver's own ES context (WGL), not ANGLE
    // per-monitor DPI aware (v2): windows are sized in real pixels on every monitor, and Windows doesn't blur them up on a scaled one.
    // A window keeps its size in pixels when it moves to a monitor with another scale
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    // the windows' close buttons are handled as events (the main window quits, the second one goes back to together)
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    // the controllers' face buttons by position (the input mapping's face layout decides where the DS's A is), Nintendo pads too
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    // a hidden window never has the focus, and its virtual pad (--virtual-pad) has to be heard anyway
    if (opts.Hidden)
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
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

    // the input mapping (input.json), with the controller mappings shipped next to the exe and the profile's own on top
    Frontend::Input input;
    input.Load(profileDir, opts.Hidden);
    for (const fs::path& db : {Frontend::PathFromUtf8(exeDir) / "gamecontrollerdb.txt", profileDir / "gamecontrollerdb.txt"})
    {
        std::error_code dbError;
        if (!fs::exists(db, dbError))
            continue;
        int added = SDL_GameControllerAddMappingsFromFile(Frontend::PathToUtf8(db).c_str());
        if (added < 0)
            fprintf(stderr, "input: %s: %s\n", Frontend::PathToUtf8(db).c_str(), SDL_GetError());
        else
            printf("input: %d controller mappings from %s\n", added, Frontend::PathToUtf8(db).c_str());
    }
    input.RumbleEnabled = settings.GetBool("desktop_rumble", true);
    if (!opts.VirtualPad.empty() && !input.AttachVirtualPad(opts.VirtualPad))
        return 1;

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
        fprintf(stderr, "can't set up the screen shaders\n");
        return 1;
    }
    presenter.SetRendererConfig(opts.Render);
    presenter.SetTiltShift(opts.TiltShift);
    presenter.SetBloom(opts.Bloom);
    presenter.SetDepthOfField(opts.DepthOfField);
    presenter.SetShade(opts.Shade);

    std::string cacheDir = opts.CacheDir.empty() ? exeDir + "cache" : opts.CacheDir;
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    OpenGL::SetShaderCachePath(cacheDir + "/compute_programs.bin");

    // What the shared code needs from this frontend (FrontendContext.h), before an instance is made
    auto messenger = std::make_shared<Frontend::DesktopEventMessenger>();
    MelonDSAndroid::frontendContext.internalFilesDir = exeDir;
    MelonDSAndroid::frontendContext.eventMessenger = messenger;
    MelonDSAndroid::RetroAchievements::RetroAchievementsManager::EventMessenger = messenger;
    // GPU::OutputCapDeband, which MelonInstance sets before each frame from what the presenter says (once it held for 30 frames, as on the
    // phone): the deband setting, or --set OutputCapDeband (and --at) on top of it. Sent again after each present (sendPresentation)
    std::atomic_bool forcedOutputDeband = SetValue(opts.Sets, "OutputCapDeband", 0) != 0;
    MelonDSAndroid::setOutputDeband(forcedOutputDeband || presenter.DebandsOutput());

    // the game library (library.db in the profile), its scan on a worker thread once the library is first shown
    Frontend::Library library;
    library.PostToUi = [&](std::function<void()> task) { messenger->Post(std::move(task)); };
    library.SetFolders(settings.GetStringList("rom_search_dirs"), settings.IsOverridden("rom_search_dirs"));
    library.PretendTitleArt = opts.PretendTitleArt;
    library.PretendWideOneIn = opts.PretendWideOneIn;
    // the metadata and art sync, asked for after each scan (on the library's worker)
    Frontend::MetadataSync metadata;
    library.OnScanDone = [&] { metadata.Request(); };
    library.Open(profileDir, Frontend::BannerLanguage());
    {
        Frontend::MetadataSync::Config config;
        config.NoSync = opts.NoSync;
        config.SyncNow = opts.SyncNow;
        if (opts.SyncBackoffMs > 0)
            config.BackoffMs = opts.SyncBackoffMs;
        auto base = [](std::string url) { while (!url.empty() && url.back() == '/') url.pop_back(); return url; };
        if (!opts.MetadataBaseUrl.empty())
            config.RaBase = config.MediaBase = base(opts.MetadataBaseUrl);
        if (!opts.ThumbnailsBaseUrl.empty())
            config.ThumbnailsBase = base(opts.ThumbnailsBaseUrl);
        if (!opts.DatUrl.empty())
            config.DatUrl = opts.DatUrl;
        // a generic User-Agent: the app and its version, nothing about the user
        config.UserAgent = "melonadeDS-desktop/" MELONADE_VERSION_NAME;
        // the key: the command line's for the run, else the one saved (DPAPI, settings.json's desktop_ra_web_api_key)
        std::string key = opts.RaWebApiKey;
        if (key.empty())
        {
            std::string stored = settings.GetString("desktop_ra_web_api_key", "");
            if (!stored.empty() && !Frontend::UnprotectSecret(stored, key))
                fprintf(stderr, "metadata: the saved Web API key can't be read (another Windows account's?); it's ignored\n");
        }
        metadata.Open(library, config, key, settings.GetBool("desktop_libretro_thumbnails", true));
        // --sync-now: the scan now (its end asks for the sync), whatever screen opens
        if (opts.SyncNow)
            library.EnsureScanned();
    }

    // the sound, through the SDL audio callback (Audio.h)
    Frontend::AudioOutput audio;
    auto audioConfig = [&]
    {
        Frontend::AudioOutput::Config config;
        config.Enabled = settings.GetBool("sound_enabled", true);
        config.Volume = std::clamp(settings.GetInt("volume", 256), 0, 256);
        config.Latency = settings.GetString("audio_latency", "medium");
        config.MuteFastForward = settings.GetBool("mute_fast_forward_audio", true);
        return config;
    };
    if (!opts.AudioTrace.empty())
        audio.TraceTo(opts.AudioTrace);
    if (!opts.AudioWav.empty())
        audio.RecordTo(opts.AudioWav);
    audio.Apply(audioConfig());

    // the controllers there now (a hidden run opens only its virtual one: Input::OpenController); the rest come as they're plugged in
    Frontend::Input::LogDriverHints();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        std::string name;
        input.DescribeJoystick(i, name);
        input.OpenController(i);
    }

    // The game running now (none on the start screen). The command line's ROM is booted with its --state, --at, --press, --touch and --gba;
    // a game opened from the menus gets none of those
    MelonDSAndroid::MelonInstance* emu = nullptr;
    std::string romPath;
    bool commandLineGame = false;
    Frontend::SaveSlots slots;

    // The input the UI thread reads, which the emulator thread applies before each frame along with --press, so scripted runs are frame exact
    // (the input mapping's DS buttons, Input::GameMask)
    std::atomic<u32> keyMask = 0;
    std::atomic_bool stopRequested = false; // the core asked to stop (SignalStop)
    std::atomic_bool stateFailed = false;   // an --at state load failed: the run stops
    std::atomic_bool startFailed = false;   // the --state didn't load: nothing ran
    std::atomic<u32> currentFrame = 0;
    // UI frames while no game frame runs (the start screen, a menu over the paused game), which --frames counts too
    std::atomic<u32> uiFrames = 0;
    bool touching = false, paused = false, running = true;
    // the right stick's touch cursor is tapping; fast forward held and toggled (the input mapping's)
    bool padTouching = false, ffHold = false, ffToggle = false;
    // the windows want drawing again (a layout, fit or filter change)
    bool redraw = false;
    // fast forward on when it's held or toggled and the game runs with no menu over it, at fast_forward_speed_multiplier; set once the
    // menus are there
    std::function<void()> updateFastForward = [] {};
    // where the bottom screen is in the drawable of the window that shows it, for --touch on the emulator thread: as planned for the
    // first frame (from the run's top screen size), then as each present drew it
    std::mutex touchLock;
    Frontend::Rect touchRect;
    bool scriptTouching = false; // emulator thread
    u32 presented = 0, presentedThisGame = 0;
    // GPU::Comp1x: presented frames with a screen the compositor drew at 1x, and the first one's number, for the run's summary
    u32 presentedAt1x = 0, firstPresentedAt1x = 0;
    FrameStats stats;
    TitleStats titleStats;
    std::string statsLabel = RendererName(opts.Renderer);
    auto runStart = std::chrono::steady_clock::now();
    auto secondStart = runStart;
    // the renderer and scale of the game, which hotkeys, --at and the settings change; emulator thread (set before it starts)
    RendererType renderer = opts.Renderer;
    int scale = opts.Scale;
    std::vector<u32> screenshotBuffer(256 * 384);

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
        Frame* frame = emu ? AcquireFrame(*emu, std::nullopt) : nullptr;
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

    // --camera-dump's file: written on the emulator thread (AfterFrame), closed after emuThread below has stopped
    std::unique_ptr<FILE, int (*)(FILE*)> cameraDump(nullptr, fclose);
    u32 cameraDumpSwaps = 0;
    if (!opts.CameraDumpPath.empty())
    {
        cameraDump.reset(fopen(opts.CameraDumpPath.c_str(), "w"));
        if (!cameraDump)
            fprintf(stderr, "--camera-dump: can't write %s\n", opts.CameraDumpPath.c_str());
    }
    // --camera-shown's file: written on the UI thread (present below), once per frame sequence
    std::unique_ptr<FILE, int (*)(FILE*)> cameraShown(nullptr, fclose);
    u64 cameraShownSequence = 0;
    if (!opts.CameraShownPath.empty())
    {
        cameraShown.reset(fopen(opts.CameraShownPath.c_str(), "w"));
        if (!cameraShown)
            fprintf(stderr, "--camera-shown: can't write %s\n", opts.CameraShownPath.c_str());
    }

    Frontend::EmulatorThread emuThread;
    Frontend::EmulatorThread::Callbacks callbacks;
    callbacks.OnStart = [&](MelonDSAndroid::MelonInstance& instance)
    {
        SetDateTime(opts, *instance.getNds());
        // as LuaEngine::wantsDepth: the depth of field, the shading, the haze and a 3D-only glow read the depth
        bool bloomWantsDepth = opts.Bloom && (opts.Bloom->HasHaze() || (opts.Bloom->HasBloom() && opts.Bloom->OnlyThreeD));
        instance.setDepthWanted(opts.DepthOfField.has_value() || opts.Shade.has_value() || bloomWantsDepth || !opts.DepthShotPath.empty()
                                || !opts.DepthRawPath.empty());
        if (commandLineGame && !opts.StatePath.empty() && !LoadStateFile(opts, instance, opts.StatePath))
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
        if (stopRequested || (opts.Frames && frame + uiFrames >= opts.Frames))
            return false;
        // the trim a (re)started audio stream asked for, at the frame's start
        if (audio.IsOpen())
            audio.BeforeFrame(instance);

        // the keys held this frame, all of them sent every frame (the instance's pressKey and releaseKey), as the desktop set the whole mask
        // before the shared code: a reset (Ctrl+R, --at reset) releases every key in the core and a state load brings its own, behind the
        // instance's back, so sending only the changes would leave a key held through them released
        u32 mask = keyMask | (commandLineGame ? Frontend::PressMask(opts.Presses, frame) : 0);
        for (u32 bit = 0; bit < 12; bit++)
            (mask & (1u << bit)) ? instance.pressKey(bit) : instance.releaseKey(bit);

        // --touch: a point of the bottom screen's window, through the same mapping as the mouse
        if (commandLineGame && !opts.Touches.empty())
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
            if (!commandLineGame || at.Frame != frame)
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
                    runOnUiThread([&] { hotkey(key, mod, 0); input.TypeKeyHotkeys(key, mod); });
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
            {
                forcedOutputDeband = SetValue({at.Action}, "OutputCapDeband", 0) != 0;
                MelonDSAndroid::setOutputDeband(forcedOutputDeband);
            }
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

        if (cameraDump)
        {
            const GPU3D::CameraLog* log = nds.GPU.GPU3D.GetCameraLog();
            if (log && log->Swaps != cameraDumpSwaps)
            {
                DumpCameraScene(cameraDump.get(), frame, *log);
                cameraDumpSwaps = log->Swaps;
            }
        }

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

        // the audio callback reads the core's audio itself; without sound it's dropped, so the ring doesn't sit full
        if (audio.IsOpen())
            audio.AfterFrame(instance);
        else
            while (instance.readAudioOutput(audioBuf.data(), (int)audioBuf.size() / 2) > 0) {}
    };
    callbacks.OnStop = [&](MelonDSAndroid::MelonInstance&)
    {
        OpenGL::ShaderCacheStats cacheStats;
        OpenGL::SaveShaderCache(cacheStats);
    };
    emuThread.SetLimitFps(!opts.Unthrottled);
    emuThread.SetGpuSync(opts.GpuSync);

    // --touch's rect again, after anything that moves the bottom screen: a present (a new frame shape), a window resize, a hotkey (F9,
    // F1, F2, F10), so a touch right after one uses the right window and rect. Stored even when the screen isn't shown (W 0: no touch)
    auto refreshTouchRect = [&]
    {
        Frontend::Rect r = windows.BottomRect();
        std::lock_guard guard(touchLock);
        touchRect = r;
    };

    // Boots [path] (UTF-8): the instance made and its ROM loaded with its save, then the emulator thread started. The command line's
    // game (commandLine) also gets its --gba, --state, --at, --press and --touch. False, with the reason on stderr, when the ROM doesn't load
    auto stopGame = [&]
    {
        if (!emu)
            return;
        // the audio callback lets go of the instance first
        audio.SetInstance(nullptr);
        input.StopRumble();
        // out of the frame loop, still serving what the emulator thread may wait for, then the instance goes on its thread
        emuThread.RequestFinish();
        while (!emuThread.IsFinished())
        {
            messenger->Drain();
            SDL_Delay(1);
        }
        emuThread.Stop();
        emu = nullptr;
        romPath.clear();
        slots.Clear();
        commandLineGame = false;
        touching = false;
        paused = false;
        keyMask = 0;
        padTouching = false;
        ffToggle = false;
        updateFastForward();
        windows.UpdateTitles("");
        // back to the library: the sync that was held off, or asked for meanwhile
        metadata.GameEnded();
        // with the process's handle count, so a leak over many boots shows
        DWORD handles = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handles);
        printf("game stopped (%lu handles)\n", (unsigned long)handles);
    };
    // [romFile]: the file to load, when it isn't [path] itself (a zip or 7z's ROM, unpacked); [path] names the game (its save, slots,
    // last-played time). A zip or 7z given without one is unpacked here
    // why the last boot failed, in words for a toast, when there's more to say than "Couldn't open" (a DSi boot without its files)
    std::string bootError;
    auto bootGame = [&](const std::string& path, bool commandLine, std::string romFile = "") -> bool
    {
        stopGame();
        bootError.clear();
        if (romFile.empty())
        {
            std::string error;
            romFile = library.PrepareNow(path, error);
            if (romFile.empty())
            {
                fprintf(stderr, "can't open %s: %s\n", path.c_str(), error.c_str());
                return false;
            }
        }
        // the settings as they are now (the menus may have changed them), with the command line's on top
        ApplySettings(opts, settings);
        renderer = opts.Renderer;
        scale = opts.Scale;
        presenter.SetRendererConfig(opts.Render);
        // what the library waited to write (its last focused game)
        settings.SaveIfChanged();

        // The game's own settings (library.db, by its RetroAchievements hash: from the library, or read from the ROM for a game outside
        // it), with --rom-config's on top: the console here, the GBA slot below. The command line beats them: --rom-config console=, then
        // --console (console_type given for the run), then a DSiWare title's DSi (the app's RomConfig.forDsiWareTitle), then the game's own
        Frontend::Library::Identity identity = library.IdentityFor(path, romFile);
        Frontend::RomConfig stored = library.GetConfig(Frontend::Library::ConfigKey(identity.RaHash, path));
        Frontend::RomConfig game = stored;
        bool consoleArg = false;
        for (const auto& [key, value] : opts.RomConfigOverrides)
        {
            std::string error;
            game.Apply(key, value, error);
            consoleArg |= key == "console";
        }
        const char* consoleFrom = "the Console setting";
        using Console = Frontend::RomConfig::Console;
        if (consoleArg && game.ConsoleType != Console::Default)
        {
            opts.ConsoleType = game.ConsoleType == Console::Dsi ? 1 : 0;
            consoleFrom = "--rom-config";
        }
        else if (consoleArg || settings.IsOverridden("console_type"))
            consoleFrom = consoleArg ? "the Console setting (--rom-config console=default)" : "--console";
        else if (identity.DsiWare)
        {
            opts.ConsoleType = 1;
            consoleFrom = "DSiWare";
        }
        else if (stored.ConsoleType != Console::Default)
        {
            opts.ConsoleType = stored.ConsoleType == Console::Dsi ? 1 : 0;
            consoleFrom = "the game's settings";
        }
        printf("game settings: %s%s (by %s); boots as %s (%s)\n", game.Describe().c_str(), opts.RomConfigOverrides.empty() ? "" : " with --rom-config",
            identity.RaHash.empty() ? "path" : identity.RaHash.c_str(), opts.ConsoleType == 1 ? "DSi" : "DS", consoleFrom);

        // The console, as before the shared code: FreeBIOS and a generated firmware unless --bios says otherwise
        std::unique_ptr<NDSArgs> args;
        if (opts.ConsoleType == 1)
        {
            // a DSi, as the app boots one (EmulatorArgsBuilder): its BIOS, firmware and NAND from the DSi folder, the DS BIOS from the DS one
            if (opts.DsiBiosDir.empty() || !settings.GetBool("use_custom_bios", false) || settings.GetString("bios_dir", "").empty())
            {
                fprintf(stderr, "DSi mode needs the custom BIOS on, with the DS BIOS folder and the DSi BIOS folder (bios9.bin, bios7.bin, "
                    "firmware.bin, nand.bin) set\n");
                bootError = "DSi mode needs the custom BIOS and the DSi BIOS folder (Settings > Emulation)";
                return false;
            }
            std::string ds = settings.GetString("bios_dir", ""), dsi = opts.DsiBiosDir;
            opts.Dsi = {dsi + "/bios9.bin", dsi + "/bios7.bin", dsi + "/firmware.bin", dsi + "/nand.bin", ds + "/bios9.bin", ds + "/bios7.bin"};
            auto config = MakeConfiguration(opts, renderer, scale);
            config->userInternalFirmwareAndBios = false;
            std::optional<std::unique_ptr<NDSArgs>> built = MelonDSAndroid::BuildArgsFromConfiguration(*config, 0);
            if (!built || !*built)
            {
                fprintf(stderr, "the DSi BIOS, firmware or NAND in %s didn't load\n", dsi.c_str());
                bootError = "The DSi BIOS, firmware or NAND didn't load";
                return false;
            }
            args = std::move(*built);
        }
        else
            args = std::make_unique<NDSArgs>();
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
        bool haveFirmware = opts.ConsoleType == 1;
        if (opts.ConsoleType == 0 && !Frontend::LoadBiosDir(opts.BiosDir, *args, haveFirmware))
            return false;
        if (!haveFirmware)
            args->Firmware = Firmware(0);
        if (!haveFirmware && opts.BootScreen)
        {
            fprintf(stderr, "--boot-screen needs the official BIOS and firmware (--bios)\n");
            opts.BootScreen = false;
        }

        // GPU::Comp1x: the presenter draws a screen at 1x only with no filter, deband or Lua effect, as the app's surfaces tell the emulator
        // after presenting; set before the first frame here, so a run's frames don't depend on when the UI thread first presents
        MelonDSAndroid::setHalvesAt1xSupported(presenter.CanDrawHalvesAt1x(windows.SurfaceCount()));
        auto instance = std::make_unique<MelonDSAndroid::MelonInstance>(0, MakeConfiguration(opts, renderer, scale), std::move(args),
            std::make_shared<Net>(), std::make_unique<MelonDSAndroid::ScreenshotRenderer>(screenshotBuffer.data()), opts.ConsoleType);
        instance->setDebugOverrides(ApplySwitches(opts, *instance->getNds()));

        // the save: --sav for the command line's game only; otherwise the one next to the ROM, or in a test run (a temporary profile) a
        // copy of it in the profile, so a test never writes a real save
        std::string savePath = Frontend::PathToUtf8(Frontend::PathFromUtf8(path).replace_extension(".sav"));
        if (commandLine && !opts.SavePath.empty())
            savePath = opts.SavePath;
        else if (temporaryProfile)
        {
            fs::path copy = profileDir / "saves" / Frontend::PathFromUtf8(path).filename().replace_extension(".sav");
            std::error_code saveError;
            fs::create_directories(copy.parent_path(), saveError);
            if (!fs::exists(copy, saveError) && fs::exists(Frontend::PathFromUtf8(savePath), saveError))
                fs::copy_file(Frontend::PathFromUtf8(savePath), copy, saveError);
            savePath = Frontend::PathToUtf8(copy);
        }
        std::vector<u8> sav;
        if (ReadFile(savePath, sav) && !sav.empty())
            printf("save: %s\n", savePath.c_str());
        if (!EnsureSaveFile(savePath) || !instance->loadRom(romFile, savePath))
        {
            fprintf(stderr, "can't load ROM %s with the save %s (not a DS ROM the core can parse, or a file it can't read or write)\n",
                romFile.c_str(), savePath.c_str());
            return false;
        }
        // The GBA slot: the command line's --gba for its game, else the game's own setting (empty by default, as in the app)
        using Slot = Frontend::RomConfig::GbaSlot;
        Slot slot = game.Gba;
        std::string gbaRom = game.GbaRomPath;
        if (commandLine && !opts.GbaRomPath.empty())
        {
            slot = opts.GbaRomPath == "rumble" ? Slot::RumblePak : Slot::GbaRom;
            gbaRom = opts.GbaRomPath;
        }
        if (slot == Slot::RumblePak)
        {
            instance->loadRumblePak();
            printf("GBA slot: Rumble Pak\n");
        }
        else if (slot == Slot::MemoryExpansion)
        {
            instance->loadGbaMemoryExpansion();
            printf("GBA slot: Memory expansion\n");
        }
        else if (slot == Slot::GbaRom && gbaRom.empty())
            fprintf(stderr, "GBA slot: no GBA ROM chosen, so it's empty\n");
        else if (slot == Slot::GbaRom)
        {
            // its save next to it, as --gba has always done
            std::string gbaSave = Frontend::PathToUtf8(Frontend::PathFromUtf8(gbaRom).replace_extension(".sav"));
            if (!EnsureSaveFile(gbaSave) || !instance->loadGbaRom(gbaRom, gbaSave))
                fprintf(stderr, "%s isn't a GBA ROM the core can parse\n", gbaRom.c_str());
            else
                printf("GBA slot: %s\n", gbaRom.c_str());
        }
        emu = instance.get();
        romPath = path;
        commandLineGame = commandLine;
        // no sync while a game runs (one running stops), as the app's EmulatorViewModel
        metadata.GameStarted();
        // the slots: --state-dir for the command line's game only; otherwise next to the ROM, or in a test run in the profile
        fs::path stateDir = commandLine && !opts.StateDir.empty() ? Frontend::PathFromUtf8(opts.StateDir)
            : temporaryProfile ? profileDir / "states" : fs::path();
        if (!stateDir.empty())
        {
            std::error_code dirError;
            fs::create_directories(stateDir, dirError);
        }
        slots.SetGame(Frontend::PathFromUtf8(path), stateDir, profileDir);
        currentFrame = 0;
        presentedThisGame = 0;
        stats = {};
        statsLabel = RendererName(renderer);
        printRenderer = true;
        lastShadowMode = 0;
        lastScene = -2;
        scriptTouching = false;
        stopRequested = false;
        {
            Frontend::FrameView planned;
            bool wide = renderer != RendererType::Software && opts.WideWidth != 256;
            planned.TopWidth = wide ? opts.WideWidth : 256;
            planned.TopMargin = wide ? opts.WideMargin : 0;
            windows.SetFrameShape(planned);
            refreshTouchRect();
        }
        AddRecentRom(settings, path);
        library.MarkPlayed(path);
        if (romFile != path)
            printf("game: %s (unpacked to %s)\n", path.c_str(), romFile.c_str());
        else
            printf("game: %s\n", path.c_str());
        MelonDSAndroid::MelonInstance* started = instance.get();
        emuThread.SetFastForwardSpeed(opts.FastForwardSpeed);
        emuThread.Start(emuWindow, emuContext, std::move(instance), callbacks);
        audio.SetInstance(started);
        audio.SetRunning(true);
        return true;
    };

    // the mouse at window point x,y of the window that shows the bottom screen
    auto touchAt = [&](int x, int y)
    {
        Frontend::Rect r = windows.BottomRect();
        if (r.W <= 0 || !emu)
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

    // The menus. They draw into the main window after the game, only when a screen, toast or the keyboard is up (uiBuilt)
    Ui::App ui;
    Ui::Host host;
    bool uiBuilt = false;
    windows.MainOverlay = [&] { if (uiBuilt) ui.Render(); };
    // the touch cursor over the bottom screen, in whichever window shows it (not under a menu)
    windows.BottomOverlay = [&](Frontend::Presenter& p, const Frontend::Rect& r, int w, int h)
    {
        Frontend::Input::Cursor cursor = input.GetCursor();
        if (!emu || ui.IsOpen() || !cursor.Visible)
            return;
        float px = r.X + (cursor.X + 0.5f) / 256 * r.W, py = r.Y + (cursor.Y + 0.5f) / 192 * r.H;
        p.DrawCursor(px, py, std::max(6.f, r.W / 256 * 6), cursor.Pressed, w, h);
    };
    updateFastForward = [&]
    {
        bool on = emu && !ui.IsOpen() && (ffHold || ffToggle);
        emuThread.SetFastForwardSpeed(opts.FastForwardSpeed);
        emuThread.SetFastForward(on);
        audio.SetFastForward(on, opts.FastForwardSpeed);
    };
    // the rumble the game asks for (the Rumble Pak), on the pad that played last
    messenger->OnRumble = [&](int durationMs) { input.Rumble(durationMs); };
    messenger->OnRumbleStop = [&] { input.StopRumble(); };
    host.Settings = &settings;
    host.Library = &library;
    host.Metadata = &metadata;
    // the key in use: the command line's (for the run), the saved one, or none
    std::string raKeySource = !opts.RaWebApiKey.empty() ? "command line" : metadata.HasApiKey() ? "saved" : "";
    host.RaKeySource = [&] { return raKeySource; };
    host.SaveRaKey = [&](const std::string& key) -> bool
    {
        bool kept = true;
        std::string stored = Frontend::ProtectSecret(key);
        if (stored.empty())
        {
            ui.Toast("Windows couldn't encrypt the key; it's used for this run only");
            raKeySource = "command line";
            kept = false;
        }
        else
        {
            // stored, not an override: a key typed in the menus is kept even when --ra-web-api-key gave the run's
            settings.SetStored("desktop_ra_web_api_key", picojson::value(stored));
            settings.Save();
            raKeySource = "saved";
        }
        printf("settings: desktop_ra_web_api_key = (encrypted)\n");
        fflush(stdout);
        metadata.SetApiKey(key);
        return kept;
    };
    host.RemoveRaKey = [&]
    {
        settings.SetStored("desktop_ra_web_api_key", picojson::value(std::string()));
        settings.Save();
        raKeySource.clear();
        printf("settings: desktop_ra_web_api_key removed\n");
        fflush(stdout);
        metadata.SetApiKey("");
    };
    host.Hidden = opts.Hidden;
    host.GameRunning = [&] { return emu != nullptr; };
    host.GameTitle = [&] { return Frontend::PathToUtf8(Frontend::PathFromUtf8(romPath).stem()); };
    host.RendererStatus = [&]
    {
        std::lock_guard guard(titleStats.Lock);
        if (titleStats.Renderer == RendererType::Software)
            return std::string("Software");
        return std::string(RendererName(titleStats.Renderer)) + " " + std::to_string(titleStats.Scale) + "x";
    };
    host.Reset = [&]
    {
        if (emu)
            emuThread.Post([&](MelonDSAndroid::MelonInstance& instance)
            {
                instance.reset();
                SetDateTime(opts, *instance.getNds());
                printf("reset\n");
            });
    };
    host.ListSlots = [&] { return slots.List(); };
    host.SlotFileNames = [&] { return slots.FileNames(); };
    host.SaveSlot = [&](int slot, std::function<void(bool)> done)
    {
        if (!emu)
        {
            done(false);
            return;
        }
        fs::path file = slots.StatePath(slot);
        // the state on the emulator thread (between frames, the game paused under the menu), then the slot's screenshot here from the
        // frame it's at, which is the newest one the emulator pushed
        emuThread.Post([&, slot, file, done](MelonDSAndroid::MelonInstance& instance)
        {
            bool ok = SaveStateToPath(instance, file);
            messenger->Post([&, slot, ok, done]
            {
                if (ok && emu)
                {
                    // the old picture goes first, as the app deletes it before it takes the new one
                    slots.DeleteScreenshot(slot);
                    std::vector<u8> pixels;
                    int w = 0, h = 0;
                    Frontend::FrameView view;
                    if (Frame* frame = AcquireFrame(*emu, std::nullopt))
                    {
                        PrepareFrame(frame);
                        view = Frontend::FrameView::Of(*frame);
                        bool read = presenter.ReadFrame(view, pixels, w, h);
                        ReleaseFrame(frame);
                        if (read && slots.WriteScreenshot(slot, pixels, w, h, view.TopWidth, view.TopMargin))
                            printf("slot %d screenshot: %s\n", slot, Frontend::PathToUtf8(slots.ScreenshotPath(slot)).c_str());
                        else
                            fprintf(stderr, "slot %d: no screenshot\n", slot);
                    }
                }
                done(ok);
            });
        });
    };
    host.LoadSlot = [&](int slot, std::function<void(bool)> done)
    {
        if (!emu)
        {
            done(false);
            return;
        }
        fs::path file = slots.StatePath(slot);
        emuThread.Post([&, file, done](MelonDSAndroid::MelonInstance& instance)
        {
            std::vector<u8> data;
            bool ok = ReadPath(file, data) && LoadStateBytes(opts, instance, data, Frontend::PathToUtf8(file));
            messenger->Post([done, ok] { done(ok); });
        });
    };
    host.QuitToStart = [&]
    {
        messenger->Post([&]
        {
            stopGame();
            ui.ShowStart();
        });
    };
    host.Quit = [&] { running = false; };
    // a game from the menus: [file] is the ROM to load (a zip or 7z's, unpacked), or empty with [error] when it couldn't be
    // a game from the menus that's being unpacked: no other one starts meanwhile
    bool menuBooting = false;
    auto bootFromMenus = [&](const std::string& path, const std::string& file, const std::string& error)
    {
        menuBooting = false;
        if (!file.empty() && bootGame(path, false, file))
            ui.CloseAll();
        else
        {
            if (!error.empty())
                fprintf(stderr, "can't open %s: %s\n", path.c_str(), error.c_str());
            // back to the library (the game that ran before is stopped)
            if (!emu && ui.TopId() != "library")
                ui.ShowStart();
            ui.Toast(!bootError.empty() ? bootError : "Couldn't open " + Frontend::PathToUtf8(Frontend::PathFromUtf8(path).filename()));
        }
    };
    host.BootRom = [&](const std::string& path) -> bool
    {
        if (menuBooting)
        {
            ui.Toast("Another game is starting");
            return false;
        }
        menuBooting = true;
        // a zip or 7z unpacked on the library's worker first, so the menus keep drawing
        if (Frontend::IsArchiveName(Frontend::PathFromUtf8(path)))
            library.Prepare(path, [&, path](const std::string& file, const std::string& error) { bootFromMenus(path, file, error); });
        else
            messenger->Post([&, path] { bootFromMenus(path, path, ""); });
        return true;
    };
    host.OpenRom = [&]
    {
        if (opts.Hidden)
        {
            ui.Toast("The file dialog doesn't open in a hidden run");
            return;
        }
        messenger->Post([&]
        {
            std::vector<std::string> recent = RecentRoms(settings);
            std::string start = recent.empty() ? "" : Frontend::PathToUtf8(Frontend::PathFromUtf8(recent[0]).parent_path());
            if (std::optional<std::string> path = Frontend::OpenRomDialog(window, start))
                host.BootRom(*path);
        });
    };
    host.IsDetached = [&] { return windows.IsDetached(); };
    host.SetDetached = [&](bool detached)
    {
        windows.SetDetached(detached);
        refreshTouchRect();
    };
    host.IsSwapped = [&] { return windows.IsSwapped(); };
    host.ToggleSwap = [&]
    {
        windows.ToggleSwap();
        refreshTouchRect();
    };
    host.Input = &input;
    host.DeleteSlot = [&](int slot) { return slots.Delete(slot); };
    host.PickFolder = [&](const std::string& start) -> std::optional<std::string>
    {
        if (opts.Hidden)
            return std::nullopt;
        return Frontend::OpenFolderDialog(window, start);
    };
    host.PickGbaRom = [&](const std::string& start) -> std::optional<std::string>
    {
        if (opts.Hidden)
            return std::nullopt;
        return Frontend::OpenGbaRomDialog(window, start);
    };
    // the renderer settings again, on the emulator thread when a game runs (MakeConfiguration reads them there)
    auto applyToggles = [&]
    {
        Options::RenderToggles toggles = ReadToggles(opts, settings);
        if (emu)
            emuThread.Post([&, toggles](MelonDSAndroid::MelonInstance& instance) { opts.Toggles = toggles; updateConfiguration(instance); });
        else
            opts.Toggles = toggles;
    };
    // UI thread, after each present and when the screens' look changes: what the windows can show, for the emulator's next frame, as the
    // app's FrameRenderCoordinator sends it after its surfaces drew. The output need only with --output-cap, so the frame keeps the
    // internal scale in every other run (the scripted renderer comparisons)
    auto sendPresentation = [&]
    {
        int surfaces = windows.SurfaceCount();
        MelonDSAndroid::setHalvesAt1xSupported(presenter.CanDrawHalvesAt1x(surfaces));
        if (opts.OutputCap)
            MelonDSAndroid::setOutputScaleNeed(presenter.OutputScaleNeed(surfaces));
        MelonDSAndroid::setOutputDeband(forcedOutputDeband || presenter.DebandsOutput());
    };
    // the screens' look again from the settings: the presenter's at once, and deband's Compute dithering on the emulator thread
    auto applyRendererConfig = [&]
    {
        Frontend::RendererConfig config = ReadRendererConfig(settings);
        presenter.SetRendererConfig(config);
        sendPresentation();
        if (emu)
            emuThread.Post([&, config](MelonDSAndroid::MelonInstance& instance)
            {
                bool ditherChanged = config.Deband != opts.Render.Deband;
                opts.Render = config;
                if (ditherChanged)
                    updateConfiguration(instance);
            });
        else
            opts.Render = config;
        redraw = true;
    };
    host.ChangeSetting = [&](const std::string& key, const picojson::value& value) -> bool
    {
        printf("settings: %s = %s\n", key.c_str(), value.serialize().c_str());
        fflush(stdout);
        auto text = [&] { return value.is<std::string>() ? value.get<std::string>() : value.to_str(); };
        // the windows' own, which they store
        if (key == "desktop_screens")
        {
            windows.SetDetached(text() == "detached");
            refreshTouchRect();
            return true;
        }
        if (key == "desktop_swap_screens")
        {
            if (value.evaluate_as_boolean() != windows.IsSwapped())
                windows.ToggleSwap();
            refreshTouchRect();
            return true;
        }
        if (key == "desktop_layout")
        {
            static const char* const kNames[] = {"vertical", "horizontal", "top", "bottom"};
            for (int i = 0; i < 4; i++)
            {
                if (text() == kNames[i])
                    windows.SetLayout((Frontend::Layout)i);
            }
            refreshTouchRect();
            return true;
        }
        if (key == "desktop_fit_top" || key == "desktop_fit_bottom")
        {
            std::string fit = text();
            windows.SetFit(key == "desktop_fit_top", fit == "integer" ? Frontend::Fit::Integer : fit == "stretch" ? Frontend::Fit::Stretch
                                                                                                                 : Frontend::Fit::Aspect);
            refreshTouchRect();
            return true;
        }
        settings.Set(key, value);
        if (key == "rom_search_dirs")
        {
            library.SetFolders(settings.GetStringList(key), settings.IsOverridden(key));
            library.Rescan();
            return true;
        }
        if (key == "desktop_libretro_thumbnails")
        {
            metadata.SetLibretro(settings.GetBool(key, true));
            return true;
        }
        if (key == "video_renderer")
        {
            std::string r = text();
            RendererType requested = r == "software" ? RendererType::Software : r == "opengl" ? RendererType::OpenGL : RendererType::Compute;
            if (emu)
                emuThread.Post([&, requested](MelonDSAndroid::MelonInstance& instance) { renderer = requested; updateConfiguration(instance); });
            // the toggles shown for it change (edge marking for software, seams for OpenGL)
            return true;
        }
        if (key == "video_internal_resolution")
        {
            int value = std::clamp(settings.GetInt(key, 4), 1, 16);
            if (emu)
                emuThread.Post([&, value](MelonDSAndroid::MelonInstance& instance) { scale = value; updateConfiguration(instance); });
            return true;
        }
        if (key == "video_hires_capture" || key == "video_filter_2d" || key == "video_sharper_rotated_2d" || key == "video_edge_marking"
            || key == "video_precise_depth" || key == "video_gl_seams")
        {
            applyToggles();
            return true;
        }
        if (key == "video_filtering" || key == "video_deband" || key == "video_ds_screen_colors" || key == "video_screen_shadow")
        {
            applyRendererConfig();
            return true;
        }
        if (key == "desktop_vsync")
        {
            windows.SetVSync(settings.GetBool("desktop_vsync", false));
            return true;
        }
        if (key == "desktop_theme")
        {
            ui.ApplyTheme();
            return true;
        }
        if (key == "desktop_bottom_no_title" || key == "desktop_bottom_on_top")
        {
            windows.ApplySecondStyle();
            return true;
        }
        if (key == "sound_enabled" || key == "volume" || key == "audio_latency" || key == "mute_fast_forward_audio")
        {
            audio.Apply(audioConfig());
            return true;
        }
        if (key == "fast_forward_speed_multiplier")
        {
            opts.FastForwardSpeed = FastForwardSpeed(settings);
            updateFastForward();
            return true;
        }
        if (key == "desktop_rumble")
        {
            input.RumbleEnabled = settings.GetBool("desktop_rumble", true);
            if (!input.RumbleEnabled)
                input.StopRumble();
            return true;
        }
        // the console, the BIOS, the JIT and the GBA slot are set up as a game boots
        if (key == "enable_jit" || key == "console_type" || key == "use_custom_bios" || key == "bios_dir" || key == "dsi_bios_dir"
            || key == "show_bios")
            return false;
        // the rest is read when it's used (desktop_fullscreen_mode at the next F11)
        return true;
    };
    if (!ui.Init(window, uiContext, host, opts.Hidden))
    {
        fprintf(stderr, "can't set up the menus (ImGui)\n");
        return 1;
    }

    // the command line's ROM, unless --open asks for the library
    bool openLibrary = opts.OpenScreen == "start" || opts.OpenScreen == "library" || opts.OpenScreen == "library/details"
        || opts.OpenScreen == "library/settings";
    if (!opts.RomPath.empty() && !openLibrary)
    {
        if (!bootGame(opts.RomPath, true))
            return 1;
    }
    else
        ui.ShowStart(opts.OpenScreen == "library/details" ? Ui::LibraryOpen::Details
                     : opts.OpenScreen == "library/settings" ? Ui::LibraryOpen::Settings : Ui::LibraryOpen::None);
    // --open: with a game once its first frame is shown, like the script
    std::string pendingOpen = openLibrary ? "" : opts.OpenScreen;

    // --ui-script's state
    size_t scriptPos = 0;
    bool scriptStarted = false, scriptEnded = false;
    int scriptDelay = 0;
    // a sleep step's end (real time)
    std::chrono::steady_clock::time_point scriptSleepUntil {};
    bool scriptWaitFrame = false;
    bool scriptWaitScan = false;
    bool scriptWaitSync = false;
    int scriptSyncQuiet = 0;
    u32 scriptFrame = 0;
    std::function<void()> scriptRelease;

    // UI thread: the window as drawn now (the game's newest frame, then the menus' last frame), into [path] (_top/_bottom when detached)
    auto windowShot = [&](const std::string& path)
    {
        // --window-shot-linear: the shot with the linear filter, the run with its own
        Frontend::RendererConfig config = presenter.GetRendererConfig();
        if (opts.WindowShotLinear)
        {
            Frontend::RendererConfig shotConfig = config;
            shotConfig.Filtering = Frontend::VideoFiltering::Linear;
            presenter.SetRendererConfig(shotConfig);
        }
        Frame* frame = emu ? AcquireFrame(*emu, std::nullopt) : nullptr;
        if (frame)
        {
            PrepareFrame(frame);
            windows.WindowShot(presenter, Frontend::FrameView::Of(*frame), path);
            ReleaseFrame(frame);
        }
        else
            windows.WindowShot(presenter, Frontend::FrameView(), path);
        presenter.SetRendererConfig(config);
    };

    // draws [frame] (null: no game, black) into the windows, with the menus over the main one, and shows them
    auto present = [&](Frame* frame)
    {
        if (frame)
        {
            PrepareFrame(frame);
            const Frontend::FrameView view = Frontend::FrameView::Of(*frame);
            if (cameraShown && view.Sequence != cameraShownSequence)
            {
                fprintf(cameraShown.get(), "{\"seq\":%llu,", (unsigned long long) view.Sequence);
                PrintDepthCamera(cameraShown.get(), view.DepthTexture != 0, view.DepthCameraValid, view.DepthProjection, view.DepthViewport);
                fprintf(cameraShown.get(), "}\n");
                cameraShownSequence = view.Sequence;
            }
            windows.Present(presenter, view, [&] { ReleaseFrame(frame); });
            presentedThisGame++;
            if (frame->halvesAt1x != 0 && presentedAt1x++ == 0)
                firstPresentedAt1x = presented + 1;
        }
        else
            windows.Present(presenter, Frontend::FrameView(), [] {});
        sendPresentation();
        presented++;
        refreshTouchRect();
    };

    u64 lastSequence = 0;
    hotkey = [&](SDL_Keycode key, Uint16 mod, Uint32 windowId)
    {
        switch (key)
        {
        case SDLK_p:
            if (!emu || ui.IsOpen())
                break;
            paused = !paused;
            paused ? emuThread.Pause() : emuThread.Resume();
            audio.SetRunning(!paused);
            break;
        case SDLK_r:
            if ((mod & KMOD_CTRL) && emu)
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
            if (emu)
                emuThread.Post([&, requested](MelonDSAndroid::MelonInstance& instance) { renderer = requested; updateConfiguration(instance); });
            break;
        }
        case SDLK_F3:
        case SDLK_F4:
        {
            int step = key == SDLK_F3 ? -1 : 1;
            if (!emu)
            {
                int value = std::clamp(settings.GetInt("video_internal_resolution", 4) + step, 1, 16);
                settings.Set("video_internal_resolution", picojson::value(std::to_string(value)));
                break;
            }
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
        {
            // linear on and off (from another filter, linear)
            bool linear = presenter.GetRendererConfig().Filtering == Frontend::VideoFiltering::Linear;
            settings.Set("video_filtering", picojson::value(std::string(linear ? "none" : "linear")));
            applyRendererConfig();
            break;
        }
        case SDLK_F10: windows.CycleFit(windowId); redraw = true; break;
        default: break;
        }
        refreshTouchRect();
    };

    // the input mapping's hotkeys (F1 swap, F9 detach, F11 fullscreen, F12 screenshot, Esc the pause menu, Tab fast forward, ... by
    // default; Input.cpp has the tables)
    bool quickBusy = false;
    input.OnAction = [&](Frontend::Action action, bool down, SDL_JoystickID source)
    {
        using Frontend::Action;
        // the window with the focus, for fullscreen (0, the main one, when none has it, as in a hidden run)
        SDL_Window* focus = SDL_GetKeyboardFocus();
        Uint32 focusId = focus ? SDL_GetWindowID(focus) : 0;
        switch (action)
        {
        case Action::Pause:
            if (emu && !ui.IsOpen())
                ui.OpenPause();
            break;
        case Action::FastForwardHold:
            ffHold = down;
            updateFastForward();
            break;
        case Action::FastForward:
            if (!emu || ui.IsOpen())
                break;
            ffToggle = !ffToggle;
            updateFastForward();
            ui.Toast(ffToggle ? "Fast forward on" : "Fast forward off");
            break;
        case Action::QuickSave:
        case Action::QuickLoad:
        {
            if (!emu || ui.IsOpen() || quickBusy)
                break;
            bool save = action == Action::QuickSave;
            if (!save && !std::filesystem::exists(slots.StatePath(0), ec))
            {
                ui.Toast("The quick slot is empty");
                break;
            }
            quickBusy = true;
            auto done = [&, save](bool ok)
            {
                quickBusy = false;
                ui.Toast(save ? (ok ? "Saved to the quick slot" : "Couldn't save to the quick slot")
                              : (ok ? "Loaded the quick slot" : "Couldn't load the quick slot"));
            };
            save ? host.SaveSlot(0, done) : host.LoadSlot(0, done);
            break;
        }
        case Action::SwapScreens: windows.ToggleSwap(); redraw = true; break;
        case Action::DetachScreens: windows.SetDetached(!windows.IsDetached()); redraw = true; break;
        case Action::Fullscreen: windows.ToggleFullscreen(focusId); redraw = true; break;
        case Action::Screenshot:
        {
            if (!emu)
                break;
            fs::create_directories(exeDir + "screenshots", ec);
            char name[64];
            snprintf(name, sizeof(name), "screenshots/frame%06u.bmp", currentFrame.load());
            screenshot(exeDir + name);
            break;
        }
        case Action::Rewind:
            if (emu && !ui.IsOpen())
                ui.Toast("Rewind comes in a later version");
            break;
        default:
            // the DS buttons go through GameMask(), the touch tap through the cursor below
            break;
        }
        if (action >= Action::Pause)
            printf("hotkey: %s%s\n", Frontend::ActionKey(action), IsHeldAction(action) ? (down ? " down" : " up") : "");
        fflush(stdout);
        refreshTouchRect();
    };

    // one step of --ui-script, after each UI frame drawn
    auto scriptTick = [&]
    {
        if (script.empty() || scriptEnded)
            return;
        // on the library right away, with a game once its first frame is shown
        if (!scriptStarted)
        {
            if (emu && presentedThisGame == 0)
                return;
            scriptStarted = true;
        }
        if (scriptDelay > 0)
        {
            scriptDelay--;
            return;
        }
        if (std::chrono::steady_clock::now() < scriptSleepUntil)
            return;
        // a save or load from the menus finishes first (the menus close when it's done)
        if (ui.Busy)
            return;
        if (scriptRelease)
        {
            std::function<void()> release = std::move(scriptRelease);
            scriptRelease = nullptr;
            release();
            scriptDelay = 1;
            return;
        }
        if (scriptWaitScan)
        {
            if (library.Busy())
                return;
            scriptWaitScan = false;
            printf("script: the library's scan is done (%zu games)\n", library.Entries()->size());
            scriptDelay = 1;
            return;
        }
        if (scriptWaitSync)
        {
            // idle, with its art drawn, for a few passes in a row (a publish lands a frame before the art is asked for)
            bool quiet = !metadata.Busy() && !ui.ArtPending();
            scriptSyncQuiet = quiet ? scriptSyncQuiet + 1 : 0;
            if (scriptSyncQuiet < 4)
                return;
            scriptWaitSync = false;
            printf("script: the metadata sync is idle (requests: %s)\n", metadata.RequestCounts().c_str());
            scriptDelay = 1;
            return;
        }
        if (scriptWaitFrame)
        {
            if (emu && emuThread.HeldAt() != scriptFrame && !emuThread.IsFinished())
                return;
            scriptWaitFrame = false;
            printf("script: held at frame %u\n", scriptFrame);
        }
        if (scriptPos >= script.size())
        {
            scriptEnded = true;
            running = false;
            return;
        }
        const Frontend::UiStep& step = script[scriptPos++];
        printf("script %d: %s\n", step.Line, step.Text.c_str());
        fflush(stdout);
        using Kind = Frontend::UiStep::Kind;
        switch (step.Type)
        {
        case Kind::Action:
        {
            using Frontend::MenuAction;
            const std::string& a = step.Arg;
            MenuAction action = a == "up" ? MenuAction::Up : a == "down" ? MenuAction::Down : a == "left" ? MenuAction::Left
                : a == "right" ? MenuAction::Right : a == "confirm" ? MenuAction::Confirm : a == "back" ? MenuAction::Back
                : a == "tab-next" ? MenuAction::TabNext : a == "tab-prev" ? MenuAction::TabPrev : a == "clear" ? MenuAction::Clear : MenuAction::Menu;
            ui.Inject(action);
            scriptDelay = 1;
            break;
        }
        case Kind::Text:
            ui.InjectText(step.Arg);
            scriptDelay = 1;
            break;
        case Kind::Key:
        {
            SDL_Keycode key = SDL_GetKeyFromName(step.Arg.c_str());
            if (key == SDLK_UNKNOWN)
            {
                fprintf(stderr, "script %d: unknown key %s\n", step.Line, step.Arg.c_str());
                break;
            }
            SDL_Event event {};
            event.type = SDL_KEYDOWN;
            event.key.windowID = SDL_GetWindowID(window);
            event.key.state = SDL_PRESSED;
            event.key.keysym.sym = key;
            event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
            SDL_PushEvent(&event);
            event.type = SDL_KEYUP;
            event.key.state = SDL_RELEASED;
            scriptRelease = [event]() mutable { SDL_PushEvent(&event); };
            break;
        }
        case Kind::KeyDown:
        case Kind::KeyUp:
        case Kind::KeyRepeat:
        {
            SDL_Keycode key = SDL_GetKeyFromName(step.Arg.c_str());
            if (key == SDLK_UNKNOWN)
            {
                fprintf(stderr, "script %d: unknown key %s\n", step.Line, step.Arg.c_str());
                break;
            }
            SDL_Event event {};
            bool down = step.Type != Kind::KeyUp;
            event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.windowID = SDL_GetWindowID(window);
            event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
            event.key.keysym.sym = key;
            event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
            // key-repeat: the KEYDOWNs a held key sends after its first one
            event.key.repeat = step.Type == Kind::KeyRepeat ? 1 : 0;
            SDL_PushEvent(&event);
            scriptDelay = 1;
            break;
        }
        case Kind::Pad:
        case Kind::PadDown:
        case Kind::PadUp:
        {
            SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(step.Arg.c_str());
            if (button == SDL_CONTROLLER_BUTTON_INVALID)
            {
                fprintf(stderr, "script %d: unknown controller button %s\n", step.Line, step.Arg.c_str());
                break;
            }
            bool press = step.Type != Kind::PadUp, release = step.Type != Kind::PadDown;
            // the virtual pad's button, through SDL's events, or else an event as a pad with no id (-1) would send it
            auto set = [&input, button](bool down)
            {
                if (input.HasVirtualPad())
                {
                    input.SetVirtualButton(button, down);
                    return;
                }
                SDL_Event event {};
                event.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                event.cbutton.which = -1;
                event.cbutton.button = (Uint8)button;
                event.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
                SDL_PushEvent(&event);
            };
            if (press && release)
            {
                set(true);
                scriptRelease = [set] { set(false); };
            }
            else
            {
                set(press);
                scriptDelay = 1;
            }
            break;
        }
        case Kind::MouseDown:
        case Kind::MouseUp:
        {
            // the pointer moved there first: the menus (ImGui) take a click where the mouse last moved, and a hidden window has no
            // mouse position of its own
            SDL_Event motion {};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.x = step.Value;
            motion.motion.y = (int)step.Number;
            motion.motion.state = step.Type == Kind::MouseUp ? SDL_BUTTON_LMASK : 0;
            SDL_PushEvent(&motion);
            SDL_Event event {};
            bool down = step.Type == Kind::MouseDown;
            event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            event.button.windowID = SDL_GetWindowID(window);
            event.button.button = SDL_BUTTON_LEFT;
            event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
            event.button.clicks = 1;
            event.button.x = step.Value;
            event.button.y = (int)step.Number;
            SDL_PushEvent(&event);
            scriptDelay = 1;
            break;
        }
        case Kind::Axis:
        {
            SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(step.Arg.c_str());
            if (axis == SDL_CONTROLLER_AXIS_INVALID || !input.HasVirtualPad())
            {
                fprintf(stderr, "script %d: axis %s needs --virtual-pad and an SDL axis name\n", step.Line, step.Arg.c_str());
                break;
            }
            input.SetVirtualAxis(axis, step.Value);
            scriptDelay = 1;
            break;
        }
        case Kind::Wait:
            scriptDelay = (int)step.Number;
            break;
        case Kind::Sleep:
            scriptSleepUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(step.Number);
            break;
        case Kind::Frame:
            if (!emu)
                fprintf(stderr, "script %d: frame %u: no game is running\n", step.Line, step.Number);
            else if (ui.IsOpen())
                fprintf(stderr, "script %d: frame %u: a menu is open, close it first\n", step.Line, step.Number);
            else if (emuThread.HeldAt() == step.Number)
                break;
            else if (currentFrame > step.Number)
                fprintf(stderr, "script %d: frame %u: the game is past it (frame %u)\n", step.Line, step.Number, currentFrame.load());
            else
            {
                // held there, from wherever it is now (running, or held at an earlier frame)
                emuThread.HoldAt(step.Number);
                if (emuThread.IsPaused())
                {
                    paused = false;
                    emuThread.Resume();
                }
                scriptFrame = step.Number;
                scriptWaitFrame = true;
            }
            break;
        case Kind::Shot:
            windowShot(step.Arg);
            break;
        case Kind::Screenshot:
            screenshot(step.Arg);
            break;
        case Kind::WaitScan:
            scriptWaitScan = true;
            break;
        case Kind::WaitSync:
            scriptWaitSync = true;
            scriptSyncQuiet = 0;
            break;
        case Kind::Quit:
            scriptEnded = true;
            running = false;
            break;
        }
    };

    // the menus over a running game pause it and take its input; closing them resumes it
    bool menuOverGame = false;
    bool openFailed = false; // --open named a screen that isn't there
    auto lastPresent = std::chrono::steady_clock::now();
    Frontend::Input::Cursor lastCursor;
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            // "press a button" in the input setup takes every key, button and stick first
            // the device each press comes from, menus or game, for the input setup's first page
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat)
                input.NoteDevice(Frontend::Input::kKeyboard);
            else if (ev.type == SDL_CONTROLLERBUTTONDOWN)
                input.NoteDevice(ev.cbutton.which);
            else if (ev.type == SDL_CONTROLLERAXISMOTION && std::abs(ev.caxis.value) > Frontend::kStickDeadZone)
                input.NoteDevice(ev.caxis.which);
            if (input.Capturing() && input.CaptureEvent(ev))
                continue;
            // the menus next: what they take, the game doesn't see
            bool menus = ui.ProcessEvent(ev);
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
                if (menus)
                    break;
                // the keys work in whichever window has the focus: the input mapping's, then the fixed F-keys
                input.OnEvent(ev);
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat)
                    hotkey(ev.key.keysym.sym, ev.key.keysym.mod, ev.key.windowID);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                // touch comes from the window that shows the bottom screen
                if (!menus && ev.button.button == SDL_BUTTON_LEFT && ev.button.windowID == SDL_GetWindowID(windows.BottomWindow()))
                    touchAt(ev.button.x, ev.button.y);
                break;
            case SDL_MOUSEMOTION:
                if (!menus && touching && ev.motion.windowID == SDL_GetWindowID(windows.BottomWindow()))
                    touchAt(ev.motion.x, ev.motion.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT && touching)
                {
                    touching = false;
                    if (emu)
                        emu->releaseScreen();
                }
                break;
            case SDL_JOYDEVICEADDED:
            {
                // a pad SDL has no mapping for shows up as a joystick only: logged, and said, so it doesn't look ignored
                std::string name;
                if (!input.DescribeJoystick(ev.jdevice.which, name) && (!opts.Hidden || SDL_JoystickIsVirtual(ev.jdevice.which)))
                    ui.Toast(name + " has no controller mapping");
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                input.OpenController(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                input.CloseController(ev.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            case SDL_CONTROLLERAXISMOTION:
                if (!menus)
                    input.OnEvent(ev);
                break;
            default: break;
            }
        }
        // combos held long enough, the touch cursor's move; then the DS buttons for the next frame (none under a menu)
        input.Tick();
        keyMask = emu && !ui.IsOpen() ? input.GameMask() : 0;
        {
            // the touch cursor taps where it is, as long as the tap is held
            Frontend::Input::Cursor cursor = input.GetCursor();
            if (emu && !ui.IsOpen() && cursor.Pressed)
            {
                if (!padTouching)
                    printf("touch cursor: tap at DS %d,%d\n", (int)cursor.X, (int)cursor.Y);
                emu->touchScreen((u16)cursor.X, (u16)cursor.Y);
                padTouching = true;
            }
            else if (padTouching)
            {
                padTouching = false;
                if (emu && !touching)
                    emu->releaseScreen();
            }
            if (cursor.Visible != lastCursor.Visible || cursor.Pressed != lastCursor.Pressed
                || (cursor.Visible && (cursor.X != lastCursor.X || cursor.Y != lastCursor.Y)))
                redraw = true;
            lastCursor = cursor;
        }

        // the emulator's events (its stop), what the emulator thread waits for (--at shot, --at key), and the menus' queued actions (a
        // game booted or stopped)
        messenger->Drain();
        if (!running || (emu && emuThread.IsFinished()))
            break;

        // --open, once there's something to open it over
        if (!pendingOpen.empty() && (!emu || presentedThisGame > 0))
        {
            if (!ui.OpenPath(pendingOpen))
            {
                fprintf(stderr, "--open %s: no such screen%s\n", pendingOpen.c_str(), emu ? "" : " without a game");
                openFailed = true;
                running = false;
                break;
            }
            pendingOpen.clear();
        }

        // a menu opened over the game pauses it, with its held keys let go; closed, the game goes on
        bool overGame = emu && ui.IsOpen();
        if (overGame != menuOverGame)
        {
            menuOverGame = overGame;
            if (overGame)
            {
                // the menus take the input from here: nothing stays held for the game
                input.ReleaseAll();
                keyMask = 0;
                if (touching || padTouching)
                {
                    touching = padTouching = false;
                    emu->releaseScreen();
                }
                emuThread.Pause();
                audio.SetRunning(false);
            }
            else
            {
                paused = false;
                emuThread.Resume();
                audio.SetRunning(true);
            }
            updateFastForward();
        }

        // the newest frame, waiting a little for one when nothing new is queued, drawn once (or again when the window needs it); every
        // time while the menus are up
        bool uiActive = ui.Active();
        Frame* frame = emu ? AcquireFrame(*emu, std::chrono::steady_clock::now() + std::chrono::milliseconds(8)) : nullptr;
        bool newFrame = frame && frame->sequence != lastSequence;
        if (newFrame || redraw || uiActive)
        {
            // the menus at about 60 per second when nothing else paces the loop (no vsync, the game paused or not there)
            bool gameIdle = !emu || emuThread.IsParked();
            if (uiActive && gameIdle && !opts.Unthrottled && !settings.GetBool("desktop_vsync", false))
            {
                auto due = lastPresent + std::chrono::microseconds(16667);
                if (std::chrono::steady_clock::now() < due)
                    std::this_thread::sleep_until(due);
            }
            uiBuilt = uiActive;
            if (uiActive)
                ui.BuildFrame();
            present(frame);
            lastPresent = std::chrono::steady_clock::now();
            if (frame)
                lastSequence = frame->sequence;
            redraw = false;
            if (gameIdle)
            {
                uiFrames++;
                if (opts.Frames && currentFrame + uiFrames >= opts.Frames)
                    running = false;
            }
        }
        else if (!emu)
            SDL_Delay(5);
        // after each pass of the loop (a UI frame when the menus are up), also while the game is held with nothing drawn
        scriptTick();

        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - secondStart).count();
        if (secs >= 1.0)
        {
            std::lock_guard guard(titleStats.Lock);
            const std::vector<double>& work = titleStats.Second.WorkMs;
            double sum = 0;
            for (double ms : work)
                sum += ms;
            if (emu)
            {
                char title[256];
                snprintf(title, sizeof(title), " - %s %dx - %.1f fps - %.2f ms%s", RendererName(titleStats.Renderer),
                    titleStats.Scale, work.size() / secs, work.empty() ? 0.0 : sum / work.size(), paused || menuOverGame ? " (paused)" : "");
                windows.UpdateTitles(title);
            }
            if (opts.AudioLog)
            {
                // the game's pace over the same second, which the audio follows
                printf("audio: game at %.2f fps\n", work.size() / secs);
                audio.PrintStats();
            }
            titleStats.Second.WorkMs.clear();
            secondStart = now;
        }
    }

    // Out of the frame loop first, still serving what the emulator thread may wait for, so the last frame stays for the shots below
    if (emu)
    {
        emuThread.RequestFinish();
        while (!emuThread.IsFinished())
        {
            messenger->Drain();
            SDL_Delay(1);
        }
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();

    // --frames or the UI script ended the run: the window as it was last drawn, menus included
    bool reachedFrames = opts.Frames && currentFrame + uiFrames >= opts.Frames;
    if ((reachedFrames || scriptEnded) && !opts.WindowShotPath.empty())
        windowShot(opts.WindowShotPath);
    if ((reachedFrames || scriptEnded) && (!opts.DepthShotPath.empty() || !opts.DepthRawPath.empty()))
    {
        Frame* frame = emu ? AcquireFrame(*emu, std::nullopt) : nullptr;
        if (frame)
        {
            PrepareFrame(frame);
            DepthShot(*frame, opts.DepthShotPath, opts.DepthRawPath);
            ReleaseFrame(frame);
        }
        else
            fprintf(stderr, "--depth-shot: no frame\n");
    }

    stats.Print(statsLabel.c_str(), wall);
    printf("presented: %u frames in %.2f s (%.1f per second), %s\n", presented, wall, presented / wall,
        windows.IsDetached() ? "detached (two windows)" : "together");
    if (presentedAt1x)
        printf("presented at 1x: %u frames had a screen at 1x (GPU::Comp1x), the first at present %u\n", presentedAt1x, firstPresentedAt1x);
    else
        printf("presented at 1x: none\n");
    if (!opts.ScreenshotPath.empty() && !startFailed && emu)
        screenshot(opts.ScreenshotPath);

    // the instance goes on the emulator thread, with its context current; its saves are written as their SaveManagers go. The audio
    // callback lets go of it first
    audio.SetInstance(nullptr);
    input.StopRumble();
    emuThread.Stop();
    emu = nullptr;
    // what waited to be written (the library's last focused game)
    settings.SaveIfChanged();
    ui.Shutdown();
    // the sync stopped (its requests cancelled), then the library's worker (a scan stops between two files)
    metadata.Close();
    library.Close();
    MelonDSAndroid::frontendContext.eventMessenger = nullptr;
    presenter.Free();
    audio.Close();
    SDL_GL_DeleteContext(emuContext);
    SDL_DestroyWindow(emuWindow);
    // the window places saved, and the second window closed
    windows.Close();
    SDL_GL_DeleteContext(uiContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    CoUninitialize();
    timeEndPeriod(1);
    if (Frontend::LogFile)
        fclose(Frontend::LogFile);
    fflush(stdout);
    return startFailed || openFailed ? 1 : stateFailed ? 2 : 0;
}
