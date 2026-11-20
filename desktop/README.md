# melonadeDS for PC

The Windows app, growing out of the desktop test frontend (`docs/plan-desktop-app.md`): it runs the
same core and the same OpenGL and Compute renderers as the Android app (`melonDS-android-lib`), in an
SDL2 window, and the app's own native code around them: `MelonInstance` with its saves, savestates,
frame queue, game state reads and the rest of `melonade-shared` (`app/src/main/cpp/SharedNative.cmake`).
Its menus ([below](#menus)) are Dear ImGui screens made for a TV and a controller first.

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

which leaves `build-desktop/melonade-desktop.exe` with `SDL2.dll` next to it. The exe is a GUI
program (no console window); the fonts are inside it (`src/resources.rc`). It needs only system DLLs
and `SDL2.dll`.

`gl/pc_gl_loader.h/.cpp` are generated from `gl/khronos/GLES3/gl32.h` by `gl/gen_gl_loader.py`;
run it again only if that header changes.

## Running

    melonade-desktop [options] [game.nds]

Without a ROM it opens the start screen. Started from a console (PowerShell, cmd, bash) it writes its
output there, or to the files it's redirected to (`> out.txt 2> err.txt`). PowerShell doesn't wait for
a GUI program unless its output is redirected or piped, so for a run whose end you wait for, redirect it
or use `Start-Process -Wait -NoNewWindow`.

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
| `--frames N` | quit after N frames and print frame time stats: the game's frames, plus the UI's while no game frame runs (the start screen, a menu over the paused game) |
| `--screenshot F.bmp` / `--window-shot F.bmp` | save the last frame (internal resolution) / the window, menus included, when `--frames` or `--ui-script` ends the run; detached, the windows as `F_top.bmp` and `F_bottom.bmp` |
| `--open SCREEN` | open a menu screen at the start: `start`, `pause`, `pause/save`, `pause/load`, `settings`, `keyboard`; with a game, once its first frame is shown |
| `--ui-script F` | drive the menus from a file, see [UI scripts](#ui-scripts) |
| `--state-dir DIR` | the savestate slots (`.ml0`-`.ml8`) in DIR instead of next to the ROM, for tests |
| `--unthrottled`, `--hidden`, `--no-audio`, `--gpu-sync` | for benchmarks and scripted runs |
| `--profile DIR` | the profile folder (see [Settings](#settings)); a scripted run (`--hidden` or `--frames`) without one gets a fresh temporary profile |
| `--setting KEY=VALUE` | a setting for this run only, e.g. `--setting desktop_fit_bottom=integer` |
| `--detach` | start with [detached screens](#windows-and-detached-screens) |
| `--window W,H[,X,Y]`, `--window2 W,H[,X,Y]` | the main / second window's size in pixels (and desktop position) for this run |
| `--state F` | load a savestate after the reset (DS-mode states only; point `--sav` at a copy, the state replaces the save). States from the app and from `--at F:save=` load the app's way (`MelonInstance::loadState`, which also sets the RTC again); older desktop states without the RetroAchievements section load as the core's part |
| `--press F[-L]:KEYS` | hold keys (`a`, `start`, `up`, ... joined by `+`) over frames F..L, for scripted runs |
| `--touch F[-L]:X,Y` | touch at X,Y in the drawable pixels of the window that shows the bottom screen over frames F..L, through the same mapping as the mouse |
| `--at F:key=KEY` | a hotkey before frame F, as if typed in the main window (`key=F9`, `key=ctrl+r`); what it changes is saved as when typed |
| `--game-state` | print the app's Pokemon game state scene (`app/src/main/cpp/gamestate`, the camera's auto zoom) whenever it changes, and the GPU 2D shadow's mode, which a Gen 4 game's battles turn on and off |
| `--no-wide-battles` | with `--wide`, Gen 4 Pokemon battles stop at the edge (by default they're drawn wide on gl and compute, as in the app) |

Keys: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Right Shift = Select,
mouse on the bottom screen = touch, Tab (hold) = fast forward, P = pause, Ctrl+R = reset,
F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up, F2 = layout, F8 = linear filtering,
F9 = screens together/detached, F1 = swap screens, F10 = fit of the focused detached window,
F11 = fullscreen of the focused window, F12 = screenshot, Esc = the pause menu. Game controllers work
too, Nintendo layout (east = A, south = B); Start or Guide opens the pause menu, so the DS's Start is
on the right stick's click until the input mapping comes. Keys and controllers work whichever window
has the focus. The fixed layouts are all in `src/Controls.cpp`.

### Settings

Settings live in `settings.json` in the profile folder: `%APPDATA%\melonadeDS\` by default, the exe's
folder when a `portable.txt` sits next to the exe, or `--profile DIR`. A scripted run (`--hidden` or
`--frames`) without `--profile` gets a fresh temporary profile that's removed when it ends, so test
runs never read or change the real one, visible or not. Keys the app has use its preference names and value types (list settings are strings);
desktop-only ones start with `desktop_`. Keys the desktop doesn't know are kept.

Options that set a setting (`--renderer`, `--scale`, `--linear`, `--vsync`, `--no-audio`, `--layout`,
`--detach`, `--setting`) apply to that run only and are never written; a hotkey that changes one of
them during that run changes it for the run only too. Hotkeys change and save the others. `--layout`
also means the screens together for that run, unless `--detach` is given too.

| Key | Values | |
|---|---|---|
| `video_renderer` | `software`, `opengl`, `compute` | F5-F7 (desktop default `compute`) |
| `video_internal_resolution` | `"1"`-`"16"` | F3/F4 (default `"4"`) |
| `video_filtering` | `none`, `linear` | F8 |
| `sound_enabled` | `true`, `false` | |
| `desktop_vsync` | `true`, `false` | the main window waits for vsync (`--vsync`) |
| `desktop_layout` | `vertical`, `horizontal`, `top`, `bottom` | the screens together, F2 |
| `desktop_screens` | `together`, `detached` | F9 |
| `desktop_swap_screens` | `true`, `false` | F1 |
| `desktop_fit_top`, `desktop_fit_bottom` | `aspect`, `integer`, `stretch` | each screen's fit when detached, F10 |
| `desktop_fullscreen_mode` | `borderless`, `exclusive` | what F11 does to the main window |
| `desktop_bottom_no_title`, `desktop_bottom_on_top` | `true`, `false` | the second window without a title bar, always on top |
| `desktop_theme` | `system`, `light`, `dark` | the menus' theme; `system` follows Windows' app mode |
| `desktop_recent_roms` | a list of paths | the start screen's recent games, newest first (10) |
| `desktop_window`, `desktop_window_detached`, `desktop_window2` | `{w, h, x, y, display, display_index, maximized, fullscreen}` | the window places, saved on exit once they came from the file or you moved, resized, maximized or fullscreened the window (a default size, shaped by `--layout`, `--wide` or `--window-scale`, isn't saved) |

### Menus

Dear ImGui (`third_party/imgui`) with its SDL2 and OpenGL3 backends, drawn into the main window after
the game frame; with detached screens the second window shows only its screen. Nothing is drawn while
no menu, toast or keyboard is up, so the window shows the game exactly as before. The screens are in
`src/ui/`, one file each, on a small stack (`ui/Ui.cpp`): Back closes the top one.

- **Look**: the app's Material 3 colors (`app/src/main/res/values*/colors.xml`), light and dark
  (`desktop_theme`), Roboto (the app has no font of its own, so Android's) and Material Symbols
  Outlined icons. Laid out for 1280x720 and scaled with the window (a 1080p TV is 1.5x), never smaller
  than the monitor's own scale; a narrow window gets fewer columns.
- **Input**: keyboard (arrows, Enter, Esc or Backspace = back, Q/W or Page Up/Down = tabs), mouse, and
  controllers (D-pad or left stick, A = choose, B = back, L/R = tabs, Start or Guide = back to the game).
  While a menu is open the game is paused and gets no input; keys and buttons held when it closes stay
  away from the game until they're released. The F-key hotkeys still work in the menus.
- **Start screen** (no ROM): Open ROM through the Windows file dialog (`.nds` and `.dsi`; zip and 7z
  aren't read by the shared ROM loader, so they're not offered), the recent games, Settings, Quit.
- **Pause menu** (Esc, Start, Guide; over the last frame, dimmed): Resume, Save state, Load state,
  Detached screens, Swap screens, Settings, Reset, Quit to start screen, Quit.
- **Save state / Load state**: the app's nine slots next to the ROM (`<rom>.ml0`, the quick slot, to
  `.ml8`, so states move between phone and PC), saved and loaded through `MelonInstance` on the
  emulator thread. Loading a state takes its save into memory only (`SaveManager::SyncFromState`), so
  the `.sav` on disk isn't touched until the game saves again. Each slot shows a screenshot (a 256x384
  PNG, top over bottom, as the app's), kept in the profile's `ss_screenshots` folder like the app keeps
  them in its own files; they don't travel with the states. The screenshot is read from the frame the
  game is paused on, since the app's `ScreenshotRenderer` only takes one as the next frame runs.
- **Settings**: renderer, internal resolution, vsync and theme for now, saved and applied at once.
- **On-screen keyboard**: a grid for text fields with a controller alone (the physical keyboard types
  too); `--open keyboard` shows a test field.

### UI scripts

`--ui-script F` drives the menus from a file, one step per line (blank lines and `#` comments skipped):

| Step | |
|---|---|
| `up`, `down`, `left`, `right`, `confirm`, `back`, `tab-next`, `tab-prev`, `menu` | the menus' input, as a controller sends it (`menu` = Start) |
| `text STRING` | typed into the text field being edited |
| `key NAME` | a key pressed and released, as typed (`key escape`, `key return`, `key f9`) |
| `pad NAME` | a controller button pressed and released, as SDL names it (`pad start`, `pad guide`, `pad b`) |
| `wait N` | N passes of the frame loop (UI frames while a menu is up) |
| `frame N` | the game run to its frame N and held there (paused before frame N runs) |
| `shot F.bmp` | the window as drawn now, menus included (detached: `F_top.bmp`, `F_bottom.bmp`) |
| `screenshot F.bmp` | the game's last frame at the internal resolution |
| `quit` | the end of the run (the run also ends after the last step) |

It starts on the start screen, or once the game's first frame is shown. Each step gets a press and a
release, two passes of the loop. `--window-shot` is taken when the script ends the run. With a ROM,
point `--sav` at a copy and `--state-dir` at a test folder, so slot saves never touch the real ones:

    # save to slot 3 at frame 1100, load it again at frame 1400
    frame 1100
    key escape
    down
    confirm
    right
    right
    down
    confirm
    frame 1400
    pad start
    down
    down
    confirm
    ...

### Windows and detached screens

The process is per-monitor DPI aware: window sizes are real pixels on every monitor, and Windows
doesn't blur-scale them on a high-DPI one. A first run opens the windows at `--window-scale` (2) times
the monitor's scale, so at 150% a 2x window is 3x in pixels, the size it had before; `--hidden` runs
keep the plain window scale. The main window remembers its size, position and monitor
(found again by name). A place is fitted to its monitor's usable area with its title bar and borders, and centered when its monitor is gone or its title bar would be off every monitor. F11 toggles
fullscreen on the focused window: the main window goes borderless or exclusive
(`desktop_fullscreen_mode`, exclusive at the monitor's own mode), the second one always borderless on
its own monitor.

Detached (F9, `--detach`), the top and bottom screens each get a window, "melonadeDS: top" (the main
window) and "melonadeDS: bottom". Each has its own size, place, monitor and fullscreen, remembered
separately (the main window has one place for together and one for detached), and each screen its own
fit: keep aspect (black bars), integer scale (the biggest whole multiple, bars all around), or stretch.
With `--wide` the top screen's aspect follows the wide width. Swap screens (F1) keeps the windows
where they are and swaps what they show; the fit goes with the screen. Touch comes from the mouse in
whichever window shows the bottom screen. Closing the second window goes back to together; closing
the main one quits. The second window can have no title bar (`desktop_bottom_no_title`; drag it by
the black bars or with Alt held, resize it at its edges) and stay on top (`desktop_bottom_on_top`).

Both windows draw the same frame with one GL context, made current on each in turn. Only the main
window waits for vsync; the second swaps with interval 0, so detaching doesn't halve the frame rate.
WGL sets the swap interval per window on NVIDIA, which the frontend checks when it detaches (it
prints which); on a driver that keeps it per context it's set before each swap instead.
`--hidden` runs never go fullscreen and save no window places unless `--save-window-places` asks.

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
- A hidden run (`--hidden`) takes no input from real controllers, only `--ui-script`'s, and never opens
  the file dialog.
- The process opts out of Windows 11's power throttling, which otherwise cuts the frame rate to a
  third when the window isn't in front.
