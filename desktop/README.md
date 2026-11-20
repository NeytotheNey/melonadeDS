# melonadeDS for PC

A desktop frontend for testing: it runs the same core and the same OpenGL and Compute renderers
as the Android app (`melonDS-android-lib`), in an SDL2 window on Windows, and the app's own native
code around them: `MelonInstance` with its saves, savestates, frame queue, game state reads and the
rest of `melonade-shared` (`app/src/main/cpp/SharedNative.cmake`).

Like the app it runs on two threads, each with its own GLES context (shared through SDL): the
emulator thread (`src/EmulatorThread.cpp`, a port of the app's `emulate()`) runs the frames and
hands them over through the app's `FrameQueue`, and the UI thread presents them. The desktop's
side of the shared code is `src/DesktopPlatform.cpp` (the core's Platform layer, with
`src/PlatformBase.cpp`, which `tools/jitverify` shares), `src/DesktopEventMessenger.cpp` and
`src/DesktopLog.cpp`.

The renderers are written for GLES 3.2 (`#version 320 es` shaders, GLES-only extensions), so the
window gets a real **OpenGL ES 3.2 context** through the GPU driver's WGL ES profile (NVIDIA, AMD
and Intel all have one) instead of a desktop GL one. The core is compiled against the same Khronos
GLES 3.2 headers as on Android (`gl/khronos`), with the functions loaded at run time by
`gl/pc_gl_loader.cpp`, since Windows has no GLES library to link. Nothing in the renderers changes.

## Building

Needs MSYS2 with the MinGW-w64 toolchain and SDL2, plus CMake and Ninja:

    pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2

Then, from Git Bash or an MSYS2 shell:

    desktop/build.sh

which leaves `build-desktop/melonade-desktop.exe` with `SDL2.dll` next to it.

`gl/pc_gl_loader.h/.cpp` are generated from `gl/khronos/GLES3/gl32.h` by `gl/gen_gl_loader.py`;
run it again only if that header changes.

## Running

    melonade-desktop [options] game.nds

FreeBIOS and a generated firmware are used unless `--bios DIR` points at `bios9.bin`, `bios7.bin`
(and `firmware.bin`). The save goes next to the ROM as `.sav` unless `--sav` says otherwise (an empty
one is made if it's missing, as the app does, and the app's `SaveManager` writes it), and the
Compute program cache and log (`melonade.log`) next to the exe. `--help` lists every option; the
main ones:

| Option | |
|---|---|
| `--renderer soft\|gl\|compute` | 3D renderer, Compute by default |
| `--scale N` | internal resolution (default 4) |
| `--wide W` | WideMelon top screen width (256 = off) |
| `--wide-margin M` | with `--wide`, M more 3D rows above and below the top screen (camera stage 2, 0-128) |
| `--dither`, `--hires-capture`, `--filter2d`, `--smooth30`, `--better-polygons`, `--precise-depth` | the app's renderer settings |
| `--set Name=Value` | a core debug switch on `GPU`, e.g. `--set LateBlit=0` (`--list-sets`) |
| `--frames N` | quit after N frames and print frame time stats |
| `--screenshot F.bmp` / `--window-shot F.bmp` | save the last frame (internal resolution) / the window when quitting |
| `--unthrottled`, `--hidden`, `--no-audio`, `--gpu-sync` | for benchmarks and scripted runs |
| `--state F` | load a savestate after the reset (DS-mode states only; point `--sav` at a copy, the state replaces the save). States from the app and from `--at F:save=` load the app's way (`MelonInstance::loadState`, which also sets the RTC again); older desktop states without the RetroAchievements section load as the core's part |
| `--press F[-L]:KEYS` | hold keys (`a`, `start`, `up`, ... joined by `+`) over frames F..L, for scripted runs |
| `--game-state` | print the app's Pokemon game state scene (`app/src/main/cpp/gamestate`, the camera's auto zoom) whenever it changes, and the GPU 2D shadow's mode, which a Gen 4 game's battles turn on and off |
| `--no-wide-battles` | with `--wide`, Gen 4 Pokemon battles stop at the edge (by default they're drawn wide on gl and compute, as in the app) |

Keys: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Right Shift = Select,
mouse on the bottom screen = touch, Tab (hold) = fast forward, P = pause, Ctrl+R = reset,
F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up, F2 = layout, F8 = linear filtering,
F11 = fullscreen, F12 = screenshot. Game controllers work too, Nintendo layout.

A scripted check, e.g. comparing renderers on the same frame:

    for r in soft gl compute; do
        build-desktop/melonade-desktop.exe --renderer $r --frames 1200 --unthrottled --hidden --no-audio \
            --sav /tmp/test.sav --screenshot $r.bmp game.nds
    done

## Differences from the app

- **No JIT by default.** In this core the x86-64 JIT runs slower than the interpreter (Pokemon
  Platinum's intro, 2400 frames: interpreter 342 fps, JIT with fast memory 175, JIT without 66), so
  the PC build interprets unless `--jit` / `--fastmem` ask for it. The app runs the ARM64 JIT,
  which this doesn't test.
- The window shows the frame texture as it comes out of `MelonInstance`: none of the app's screen
  filters, deband, layouts or frame pacing to the display's vsync. Lua scripts, rewind and
  RetroAchievements are compiled in (shared code) but not set up yet. Pacing is the app's timer
  limiter at the DS's 59.83 Hz instead of the app's 60.
- DS mode only (no DSi NAND), no multiplayer, Wi-Fi, mic or camera.
- The process opts out of Windows 11's power throttling, which otherwise cuts the frame rate to a
  third when the window isn't in front.
