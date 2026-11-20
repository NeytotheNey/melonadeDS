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

which leaves `build-desktop/melonade-desktop.exe` with `SDL2.dll` and `gamecontrollerdb.txt` next to it
(`BUILD_DIR=dir` builds elsewhere, for example while that exe is running), and a ready-to-zip folder in
`build-desktop/release/melonadeDS-<version>/`: the exe, `SDL2.dll`, `gamecontrollerdb.txt` and `licenses/`. The exe is
a GUI program (no console window); the fonts, the icon and the version info are inside it (`src/resources.rc`; the
version is the app's `versionName` from `buildSrc/src/main/kotlin/AppConfig.kt`, the icon `src/app.ico`, made from the
app's launcher icon by `tools/desktop-icon/make_icon.py`). It needs only system DLLs and `SDL2.dll`.

`gl/pc_gl_loader.h/.cpp` are generated from `gl/khronos/GLES3/gl32.h` by `gl/gen_gl_loader.py`;
run it again only if that header changes.

## Running

    melonade-desktop [options] [game.nds]

Without a ROM it opens the start screen. It's a GUI program: started from bash or cmd it writes to their
console (test runs only: `--hidden`, `--frames`, `--ui-script`, `--verbose`, `--help`; a run to play lets go of the console, so
Ctrl+C there can't end the game), or to the files it's redirected to (`> out.txt 2> err.txt`, as the `logs/` scripts do).
Windows PowerShell treats a GUI program differently: it neither waits for it nor redirects `>` or `*>`
(the file stays empty), but it does both when the output is piped, so use
`& .\melonade-desktop.exe ... 2>&1 | Out-File out.txt`, or
`Start-Process -Wait -NoNewWindow -RedirectStandardOutput out.txt -RedirectStandardError err.txt`.

FreeBIOS and a generated firmware are used unless `--bios DIR` points at `bios9.bin`, `bios7.bin`
(and `firmware.bin`). The save goes next to the ROM as `.sav` unless `--sav` says otherwise (an empty
one is made if it's missing, as the app does, and the app's `SaveManager` writes it; `--sav` is for the command line's game
only, and a game opened from the menus in a test run, with its temporary profile, gets a copy of its save there), and the
Compute program cache and log (`melonade.log`) next to the exe. `--help` lists every option; the
main ones:

| Option | |
|---|---|
| `--renderer soft\|gl\|compute` | 3D renderer, Compute by default |
| `--scale N` | internal resolution (default 4) |
| `--wide W` | WideMelon top screen width (256 = off) |
| `--wide-margin M` | with `--wide`, M more 3D rows above and below the top screen (camera stage 2, 0-128) |
| `--dither`, `--hires-capture`, `--filter2d`, `--smooth30`, `--better-polygons`, `--precise-depth` | the app's renderer settings (the ones the settings screens have are overrides of their settings) |
| `--set Name=Value` | a core debug switch on `GPU`, e.g. `--set LateBlit=0` (`--list-sets`) |
| `--frames N` | quit after N frames and print frame time stats: the game's frames, plus the UI's while no game frame runs (the start screen, a menu over the paused game) |
| `--screenshot F.bmp` / `--window-shot F.bmp` | save the last frame (internal resolution) / the window, menus included, when `--frames` or `--ui-script` ends the run; detached, the windows as `F_top.bmp` and `F_bottom.bmp` |
| `--open SCREEN` | open a menu screen at the start: `start`, `pause`, `pause/save`, `pause/load`, `settings`, `settings/video` (or `display`, `emulation`, `audio`, `controls`), `input`, `keyboard`; with a game, once its first frame is shown |
| `--ui-script F` | drive the menus from a file, see [UI scripts](#ui-scripts) |
| `--state-dir DIR` | the command line's game's savestate slots (`.ml0`-`.ml8`) in DIR instead of next to the ROM, for tests (a game opened from the menus in a test run keeps them in the temporary profile) |
| `--unthrottled`, `--hidden`, `--no-audio`, `--gpu-sync` | for benchmarks and scripted runs |
| `--virtual-pad xbox\|switch\|ps` | an SDL virtual controller of that model, the one controller a hidden run takes; `--ui-script`'s `pad` and `axis` steps drive it through SDL's own events, and its rumble is printed |
| `--audio-log`, `--audio-wav F.wav`, `--audio-trace F.csv` | the sound: the ring's fill and ratio once a second, what the callback plays (before the volume), every callback's and frame's fill |
| `--console ds\|dsi`, `--dsi-bios DIR` | the console for this run (`console_type`, `dsi_bios_dir`); DSi needs `--bios` too |
| `--sharper-rotated-2d`, `--gl-seams`, `--no-edge-marking` | the app's renderer settings, like `--filter2d` |
| `--gba rumble` | the Rumble Pak in the GBA slot (`desktop_rumble_pak` for every game) |
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

Keys and buttons are the input mapping's ([Input](#input)); by default the keyboard has arrows = D-pad, X = A, Z = B, S = X,
A = Y, Q = L, W = R, Enter = Start, Right Shift (or Backspace) = Select, Esc = the pause menu, Tab (hold) or \` (on/off) =
fast forward, Ctrl+S / Ctrl+L = quick save / load (slot 0), F1 = swap screens, F9 = screens together/detached, F11 (or
Alt+Enter) = fullscreen of the focused window, F12 = screenshot, and the mouse on the bottom screen touches. These stay fixed:
P = pause, Ctrl+R = reset, F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up, F2 = layout, F8 = linear filtering,
F10 = fit of the focused detached window. Keys and controllers work whichever window has the focus.

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
| `video_hires_capture`, `video_filter_2d`, `video_sharper_rotated_2d`, `video_edge_marking`, `video_precise_depth`, `video_gl_seams` | `true`, `false` | the renderer settings, applied to a running game at once (desktop defaults: edge marking on, the rest off) |
| `enable_jit` | `true`, `false` | from the next game on (desktop default off) |
| `console_type` | `ds`, `dsi` | from the next game on; DSi needs the custom BIOS and both folders below |
| `use_custom_bios`, `bios_dir`, `dsi_bios_dir`, `show_bios` | | the custom BIOS and firmware (off: FreeBIOS) and the boot screen, from the next game on |
| `desktop_rumble_pak` | `true`, `false` | the Rumble Pak in the GBA slot, from the next game on |
| `fast_forward_speed_multiplier` | `"-1"` (unlimited), `"1.5"`, `"2"`, `"3"`, `"4"`, `"8"` | |
| `sound_enabled` | `true`, `false` | |
| `volume` | 0-256 | |
| `audio_latency` | `low`, `medium`, `high` | the callback's size and the buffered sound, see [Sound](#sound) |
| `mute_fast_forward_audio` | `true`, `false` | (default on, as in the app) |
| `desktop_rumble` | `true`, `false` | controllers rumble when the game does |
| `desktop_vsync` | `true`, `false` | the main window waits for vsync (`--vsync`) |
| `desktop_layout` | `vertical`, `horizontal`, `top`, `bottom` | the screens together, F2 |
| `desktop_screens` | `together`, `detached` | F9 |
| `desktop_swap_screens` | `true`, `false` | F1 |
| `desktop_fit_top`, `desktop_fit_bottom` | `aspect`, `integer`, `stretch` | each screen's fit when detached, F10 |
| `desktop_fullscreen_mode` | `borderless`, `exclusive` | what F11 does to the main window |
| `desktop_bottom_no_title`, `desktop_bottom_on_top` | `true`, `false` | the second window without a title bar, always on top |
| `desktop_theme` | `system`, `light`, `dark` | the menus' theme; `system` follows Windows' app mode |
| (`input.json`) | | the input mapping, in a file of its own next to `settings.json`, see [Input](#input) |
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
- **Input**: keyboard (arrows, Enter, Esc or Backspace = back, Delete = clear, Q/W or Page Up/Down = tabs), mouse, and
  controllers (D-pad or left stick, A = choose, B = back, X = clear, L/R = tabs, Start or Guide = back to the game), A and B
  where each pad's face layout puts them.
  While a menu is open the game is paused and gets no input; keys and buttons held when it closes stay
  away from the game until they're released. The F-key hotkeys still work in the menus.
- **Start screen** (no ROM): Open ROM through the Windows file dialog (`.nds` and `.dsi`; zip and 7z
  aren't read by the shared ROM loader, so they're not offered), the recent games, Settings, Quit.
- **Pause menu** (the Pause binding: Esc, Guide, or Select + Start held; over the last frame, dimmed): Resume, Save state, Load state,
  Detached screens, Swap screens, Settings, Reset, Quit to start screen, Quit.
- **Save state / Load state**: the app's nine slots next to the ROM (`<rom>.ml0`, the quick slot, to
  `.ml8`, so states move between phone and PC), saved and loaded through `MelonInstance` on the
  emulator thread. Loading a state takes its save into memory only (`SaveManager::SyncFromState`), so
  the `.sav` on disk isn't touched until the game saves again. Each slot shows a screenshot (a 256x384
  PNG, top over bottom, as the app's), kept in the profile's `ss_screenshots` folder like the app keeps
  them in its own files; they don't travel with the states. The screenshot is read from the frame the
  game is paused on, since the app's `ScreenshotRenderer` only takes one as the next frame runs. Saving over a used slot asks
  first, and X (Delete) on a used slot deletes it, once asked, to the recycle bin with its screenshot.
- **Settings**: five tabs (L / R): Video (renderer, internal resolution, the renderer settings, JIT), Display (screens together or
  detached, layout, swap, each detached screen's fit, the bottom window's title bar and on-top, fullscreen mode, vsync, filter,
  theme), Emulation (console, custom BIOS and its folders through the Windows folder picker, boot screen, Rumble Pak, fast-forward
  speed), Audio (on/off, volume, latency, mute while fast forwarding) and Controls (the input setup, rumble). A change is saved at
  once and applied to a running game where the core allows it; the rows that wait for the next game say so, and a toast says it
  when a game runs. `--open settings/audio` opens a tab.
- **Buttons and hotkeys** (Settings > Controls, `--open input`): one device at a time (L / R: the keyboard, the controllers plugged
  in, the ones `input.json` knows), each DS button and hotkey with a primary and an alternate binding. A waits for the next input of
  that device for 5 s (several held together make a combo, taken when one is let go; Esc or B on the other kind of device cancels),
  X clears, and the top rows reset the device to its defaults (asked twice) and set a controller's face layout.
- **On-screen keyboard**: a grid for text fields with a controller alone (the physical keyboard types
  too); `--open keyboard` shows a test field.

### UI scripts

`--ui-script F` drives the menus from a file, one step per line (blank lines and `#` comments skipped):

| Step | |
|---|---|
| `up`, `down`, `left`, `right`, `confirm`, `back`, `tab-next`, `tab-prev`, `menu`, `clear` | the menus' input, as a controller sends it (`menu` = Start, `clear` = X) |
| `text STRING` | typed into the text field being edited |
| `key NAME` | a key pressed and released, as typed (`key escape`, `key return`, `key f9`) |
| `pad NAME` | a controller button pressed and released, as SDL names it (`pad start`, `pad guide`, `pad b`); with `--virtual-pad`, on that pad |
| `pad-down NAME`, `pad-up NAME`, `key-down NAME`, `key-up NAME` | a button or key held over the next steps, or let go (combos, holds); `key-repeat NAME` sends a repeated KEYDOWN, as a held key does |
| `mouse-down X Y`, `mouse-up X Y` | the left mouse button at X,Y of the main window |
| `axis NAME VALUE` | a stick or trigger of the `--virtual-pad` (`axis rightx 20000`, `axis righttrigger 32767`) |
| `wait N` | N passes of the frame loop (UI frames while a menu is up) |
| `sleep MS` | MS milliseconds of real time, the game running on unless it's held |
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

### Input

Every DS button and hotkey has a primary and an alternate binding per device, like the app's `InputConfig`: the keyboard has one
profile, each controller model (its SDL GUID) one of its own, made from the defaults the first time it's plugged in. A binding is a key,
a controller button, a stick or trigger pushed one way, or a combo of up to three of them. Controller combos fire once all their parts
were held for half a second, so a game's own combos (Select + Start, Pokemon's L + R + Start + Select reset) still reach it; keyboard
combos fire at once. While a combo is on, its parts don't reach the game. They live in `input.json` in the profile folder, with the app's
`Input` names (`A`, `START`, `PAUSE`, `FAST_FORWARD_HOLD`, `QUICK_SAVE`, ...) and the desktop's own (`TOUCH_TAP`, `DETACH_SCREENS`,
`SCREENSHOT`, `FULLSCREEN`); `Input.cpp` has the default tables.

Controllers are SDL GameControllers, with the mappings in `gamecontrollerdb.txt` next to the exe (and one in the profile folder, if
there is one, on top), opened as they're plugged in. Their face buttons are read by position, and each pad's face layout says where
the DS's A is: Nintendo (east = A, south = B, north = X, west = Y; the default for Switch pads) or positional (south = A, east = B, west =
X, north = Y, as an Xbox pad's labels say; the default for Xbox, PlayStation and other pads). The menus follow it too.

| | Keyboard | Controller |
|---|---|---|
| D-pad, A, B, X, Y, L, R | arrows, X, Z, S, A, Q, W | D-pad (or the left stick), the face buttons by the layout, LB, RB |
| Start, Select | Enter, Right Shift (or Backspace) | Start, Back |
| Tap the touch screen | (the mouse) | right trigger (or the right stick's click) |
| Pause menu | Esc | Guide, or Select + Start held |
| Fast forward hold / on-off | Tab / \` | left trigger / none |
| Quick save / load (slot 0) | Ctrl+S / Ctrl+L | Select + RB held / Select + LB held |
| Swap screens | F1 | the left stick's click |
| Detach screens, screenshot, fullscreen | F9, F12, F11 (or Alt+Enter) | none |
| Rewind | (comes later; it shows a toast) | |

The right stick moves a touch cursor over the bottom screen, drawn in whichever window shows it, and the Touch binding taps where it
is; it stays a few seconds after it stops. A pad whose profile binds the right stick to something else doesn't move it. Rumble (a game
with the Rumble Pak in the GBA slot) goes to the pad that played last, through `SDL_GameControllerRumble`; `desktop_rumble` turns it off.

### Sound

The game's sound plays through an SDL audio callback, ported from the app's Oboe output: the callback steers the buffered sound
towards a target by nudging the SPU's resampling ratio by up to 0.5% (the app's drift control), primes before it plays, fades in and out
of gaps, and trims what fast forward leaves. Since a PC runs a frame in a few milliseconds and its frames come in bunches when one is
slow, the emulator thread moves each frame's sound into a ring of its own (8192 frames) instead of the callback reading the SPU's
(2048), with targets of 1500, 2000 and 2800 frames (31, 42 and 58 ms) for low, medium and high latency (256, 512 and 1024 frames a
callback). Fast forward is the app's: silent while muted (the default), sped up and pitched up at its speed when it isn't, and silent
when it's unlimited. The emulator runs at the DS's 59.83 Hz, which the display's refresh rate doesn't change. `--audio-log` prints the
fill, the filtered fill, the ratio, underruns and overflows once a second.

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
- A hidden run (`--hidden`) takes no input from real controllers, only `--ui-script`'s and its `--virtual-pad`'s, and never
  opens the file or folder dialog.
- The sound goes through a deeper buffer than the app's, with larger targets (see [Sound](#sound)).
- The process opts out of Windows 11's power throttling, which otherwise cuts the frame rate to a
  third when the window isn't in front.
