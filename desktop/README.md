# melonadeDS for PC

The Windows app, growing out of the desktop test frontend (`docs/plan-desktop-app.md`): it runs the
same core and the same OpenGL and Compute renderers as the Android app (`melonDS-android-lib`), in an
SDL2 window, and the app's own native code around them: `MelonInstance` with its saves, savestates,
frame queue, game state reads and the rest of `melonade-shared` (`app/src/main/cpp/SharedNative.cmake`).
Its menus ([below](#menus)) are Dear ImGui screens made for a TV and a controller first, starting on the [game library](#game-library).

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
app's launcher icon by `tools/desktop-icon/make_icon.py`), and so are the app's shaders ([Screen filters](#screen-filters)). It needs
only system DLLs and `SDL2.dll`.

`gl/pc_gl_loader.h/.cpp` are generated from `gl/khronos/GLES3/gl32.h` by `gl/gen_gl_loader.py`;
run it again only if that header changes.

## Running

    melonade-desktop [options] [game.nds|.dsi|.zip|.7z]

Without a ROM it opens the game library. A zip or 7z is unpacked into the profile's `rom-cache` folder first (its save and slots stay
next to the archive). It's a GUI program: started from bash or cmd it writes to their
console (test runs only: `--hidden`, `--frames`, `--ui-script`, `--verbose`, `--help`; a run to play lets go of the console, so
Ctrl+C there can't end the game), or to the files it's redirected to (`> out.txt 2> err.txt`, as the `logs/` scripts do).
Windows PowerShell treats a GUI program differently: it neither waits for it nor redirects `>` or `*>`
(the file stays empty), but it does both when the output is piped, so use
`& .\melonade-desktop.exe ... 2>&1 | Out-File out.txt`, or
`Start-Process -Wait -NoNewWindow -RedirectStandardOutput out.txt -RedirectStandardError err.txt`.

FreeBIOS and a generated firmware are used unless `--bios DIR` points at `bios9.bin`, `bios7.bin`
(and `firmware.bin`). The save goes next to the ROM as `.sav` unless `--sav` says otherwise (an empty
one is made if it's missing, as the app does, and the app's `SaveManager` writes it; `--sav` is for the command line's game
only, and in a test run, with its temporary profile, a game without one gets a copy of its save there), and the
Compute program cache and log (`melonade.log`) next to the exe. `--help` lists every option; the
main ones:

| Option | |
|---|---|
| `--renderer soft\|gl\|compute` | 3D renderer, Compute by default |
| `--scale N` | internal resolution (default 4) |
| `--wide W` | WideMelon top screen width (256 = off) |
| `--wide-margin M` | with `--wide`, M more 3D rows above and below the top screen (camera stage 2, 0-128) |
| `--dither`, `--hires-capture`, `--filter2d`, `--smooth30`, `--better-polygons`, `--precise-depth` | the app's renderer settings (the ones the settings screens have are overrides of their settings) |
| `--filter NAME` | the [screen filter](#screen-filters) for the run (`video_filtering`): `none`, `linear`, `xbr2`, `hq2x`, `hq4x`, `quilez`, `lcd`, `scanlines`, `scale2x`, `fxaa`, `sharp`, `mmpx`, the app's `VideoFiltering` names in any case; `--linear` is `--filter linear` |
| `--deband on\|off`, `--ds-colors on\|off`, `--screen-shadow on\|off` | smooth gradients (`video_deband`), the DS screen colors (`video_ds_screen_colors`) and the screen shadow (`video_screen_shadow`) for the run |
| `--tilt-shift TOP,BOTTOM,CENTER,WIDTH,RADIUS[,SATURATION,CONTRAST[,COLOR]]` | the Lua tilt-shift's passes over the top screen, for tests until scripts drive them (phase 4): the blur's strength above and below the sharp band (0-1), the band's center and width (fractions of the screen's height), the radius in DS pixels (up to 64), and the color boost with what it follows (the app's `TiltShift`) |
| `--bloom STRENGTH[,THRESHOLD,RADIUS[,VIGNETTE,SIZE]]` | the Lua bloom's and vignette's passes over the top screen, for tests until scripts drive them, clamped as `melon.screen.bloom` and `vignette` clamp them: the glow's strength (0-2), the luma it starts at (0-1, 0.7 left out) and its radius in DS pixels (0-8, 3), then the vignette's strength (0-1) and where it starts as a fraction of the half-diagonal (0-1, 0.6); both strengths at 0 is none (the app's `Bloom`). Only `--window-shot` shows them |
| `--dof STRENGTH,RADIUS[,RANGE,FALLOFF[,FOCUS_X,FOCUS_Y[,FOCUS_DEPTH]]]` | the Lua depth of field's passes over the top screen, for tests, clamped as `melon.screen.depth_of_field` clamps them: its strength (0-1) and radius in DS pixels (0-12), the sharp range around the focus and the falloff past it in raw depth (0-1, 0.0002 and 0.0015, tuned for Pokémon White's field), and the focus point in DS pixels of the top screen (128,96) or a focus depth (0-1) instead. It asks the core for each frame's depth (`MelonInstance::setDepthWanted`), which only `--renderer gl` hands out. Only `--window-shot` shows it |
| `--output-cap` | the windows' screen size goes to the core after each present (GPU::OutputCap, as the app's surfaces send it), so the frame is drawn no bigger than the screens are shown; without it the frame stays at the internal resolution |
| `--window-shot-linear` | the window shot drawn with the linear filter, whatever the run's filter |
| `--set Name=Value` | a core debug switch on `GPU`, e.g. `--set LateBlit=0` (`--list-sets`) |
| `--frames N` | quit after N frames and print frame time stats: the game's frames, plus the UI's while no game frame runs (the start screen, a menu over the paused game), and how many presented frames had a screen at 1x (GPU::Comp1x) |
| `--screenshot F.bmp` / `--window-shot F.bmp` | save the last frame (internal resolution) / the window, menus included, when `--frames` or `--ui-script` ends the run; detached, the windows as `F_top.bmp` and `F_bottom.bmp` |
| `--open SCREEN` | open a menu screen at the start: `library` (or `start`), `library/details` or `library/settings` (the first game's details or game settings once the scan is done), `pause`, `pause/save`, `pause/load`, `settings`, `settings/video` (or `display`, `emulation`, `audio`, `controls`, `library`), `input`, `keyboard`; with a game, once its first frame is shown |
| `--rom-folder DIR` | a folder for the game library, repeated for more; for the run only, in place of the saved `rom_search_dirs`, never written |
| `--rom-config KEY=VALUE` | over the booted game's own settings for this run, never stored: `console=default\|ds\|dsi`, `gba-slot=default\|none\|gba-rom\|rumble-pak\|memory-expansion`, `gba-rom=FILE.gba`, `name=TEXT`; repeat it for more |
| `--pretend-title-art`, `--pretend-wide-tiles N` | every game counts as having title-screen art, so the Mosaic view makes one in six of them wide by ROM hash, as the app does; `--pretend-wide-tiles N` makes one in N of the games with title art (downloaded, or pretended) wide by path instead, for a test library of copies of a few ROMs |
| `--no-sync` | no network at all for the run: the [metadata sync](#metadata-and-art) makes no requests (the art on disk still shows, and the ROMs' hashes are still computed) |
| `--sync-now` | scan the library and sync at the start, without the library screen (with `--open settings/library`, say), with RetroAchievements' hash library fetched whatever its age |
| `--ra-web-api-key KEY` | a RetroAchievements Web API key for this run only, never stored |
| `--metadata-base-url URL`, `--thumbnails-base-url URL`, `--dat-url URL` | the sync's hosts for this run (a local test server): `URL/dorequest.php`, `URL/API/API_GetGame.php` and `URL/Images/...` in place of retroachievements.org and media.retroachievements.org; `URL/Named_Boxarts/<name>.png` and `URL/Named_Titles/<name>.png` for the libretro thumbnails; the DAT's address |
| `--sync-backoff S` | a sync that stopped early tries again after S seconds, then twice that (default 900, the app's 15 minutes), for tests |
| `--ui-script F` | drive the menus from a file, see [UI scripts](#ui-scripts) |
| `--state-dir DIR` | the command line's game's savestate slots (`.ml0`-`.ml8`) in DIR instead of next to the ROM, for tests (without it, a test run keeps them in its temporary profile) |
| `--unthrottled`, `--hidden`, `--no-audio`, `--gpu-sync` | for benchmarks and scripted runs |
| `--virtual-pad xbox\|switch\|ps\|generic` | an SDL virtual controller of that model (generic: a joystick SDL has no mapping for), the one controller a hidden run takes; `--ui-script`'s `pad` and `axis` steps drive it through SDL's own events, and its rumble is printed |
| `--audio-log`, `--audio-wav F.wav`, `--audio-trace F.csv` | the sound: the ring's fill and ratio once a second, what the callback plays (before the volume), every callback's and frame's fill |
| `--console ds\|dsi`, `--dsi-bios DIR` | the console for this run (`console_type`, `dsi_bios_dir`); DSi needs `--bios` too |
| `--sharper-rotated-2d`, `--gl-seams`, `--no-edge-marking` | the app's renderer settings, like `--filter2d` |
| `--gba rumble` | the Rumble Pak in the GBA slot for the command line's game, over its game settings (a game's own GBA slot setting does it from the menus) |
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
P = pause, Ctrl+R = reset, F5/F6/F7 = software/OpenGL/Compute, F3/F4 = scale down/up, F2 = layout, F8 = the linear filter on and off,
F10 = fit of the focused detached window. Keys and controllers work whichever window has the focus.

### Settings

Settings live in `settings.json` in the profile folder: `%APPDATA%\melonadeDS\` by default, the exe's
folder when a `portable.txt` sits next to the exe, or `--profile DIR`. A scripted run (`--hidden` or
`--frames`) without `--profile` gets a fresh temporary profile that's removed when it ends, so test
runs never read or change the real one, visible or not. Keys the app has use its preference names and value types (list settings are strings);
desktop-only ones start with `desktop_`. Keys the desktop doesn't know are kept.

Options that set a setting (`--renderer`, `--scale`, `--filter`, `--linear`, `--deband`, `--ds-colors`, `--screen-shadow`, `--vsync`,
`--no-audio`, `--layout`, `--detach`, `--rom-folder`, `--setting`) apply to that run only and are never written; a hotkey that changes one of
them during that run changes it for the run only too. Hotkeys change and save the others. `--layout`
also means the screens together for that run, unless `--detach` is given too.

| Key | Values | |
|---|---|---|
| `video_renderer` | `software`, `opengl`, `compute` | F5-F7 (desktop default `compute`) |
| `video_internal_resolution` | `"1"`-`"16"` | F3/F4 (default `"4"`) |
| `video_filtering` | `none`, `linear`, `xbr2`, `hq2x`, `hq4x`, `quilez`, `lcd`, `scanlines`, `scale2x`, `fxaa`, `sharp`, `mmpx` | the [screen filter](#screen-filters); F8 turns linear on and off |
| `video_deband` | `true`, `false` | smooth gradients (default off, as in the app); on Compute it also turns on the color dithering, as in the app |
| `video_ds_screen_colors` | `true`, `false` | the DS screen colors (default off; the app took its switch out and reads it as off whatever is stored) |
| `video_screen_shadow` | `true`, `false` | a soft shadow around the screens (default off, as in the app) |
| `video_hires_capture`, `video_filter_2d`, `video_sharper_rotated_2d`, `video_edge_marking`, `video_precise_depth`, `video_gl_seams` | `true`, `false` | the renderer settings, applied to a running game at once (desktop defaults: edge marking on, the rest off) |
| `enable_jit` | `true`, `false` | from the next game on (desktop default off) |
| `console_type` | `ds`, `dsi` | from the next game on; DSi needs the custom BIOS and both folders below |
| `use_custom_bios`, `bios_dir`, `dsi_bios_dir`, `show_bios` | | the custom BIOS and firmware (off: FreeBIOS) and the boot screen, from the next game on |
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
| `rom_search_dirs` | a list of folders | the game library's ROM folders (the app's key; its values are content URIs, these are paths) |
| `rom_list_view_mode` | `list`, `grid` | the library's view (the app's key; desktop default `grid`, the app's `list`) |
| `rom_sorting_mode`, `rom_sorting_order` | `alphabetically`, `recently_played`; `ascending`, `descending` | the library's sort, as the app stores it |
| `desktop_recent_roms` | a list of paths | the games opened last, newest first (10): where the Open ROM dialog starts |
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
  controllers (D-pad or left stick, A = choose, B = back, X = clear, L/R = tabs, Start or Guide = back to the game, or with no game
  the library's menu), A and B where each pad's face layout puts them.
  While a menu is open the game is paused and gets no input; keys and buttons held when it closes stay
  away from the game until they're released. The F-key hotkeys still work in the menus.
- **Library** (no game running): see [Game library](#game-library).
- **Game details** (a game chosen in the library): its icon, title, developer, file, game code, region, size, when it was last played,
  its RetroAchievements hash and its game settings that aren't the default, with Play focused (a zip or 7z says it's unpacking
  meanwhile) and Game settings beside it. Back goes back to the game's tile. Play and Game settings only take a press that began on
  this screen, so the press that opened it can't start the game (the app's `CarriedOverKeyFilter`).
- **Game settings** (`src/ui/GameSettingsScreen.cpp`; from the details, a featured mosaic tile's menu button, or holding confirm or
  the mouse on a tile for half a second, the app's long press): the app's ROM settings for what the desktop applies when the game
  boots. Display name (the on-screen keyboard; X goes back to the banner's title), Boot system (Default, which follows Settings >
  Emulation > Console, DS or DSi; locked to DSi for a DSiWare title, as the app does) and GBA slot (None by default as in the app, GBA
  ROM with its file through the Windows file dialog and its save next to it as `--gba` does, Rumble Pak, Memory expansion; the file is
  kept while another slot is chosen), then Reset to defaults. They're kept by the ROM's RetroAchievements hash, so they survive the
  file being moved or renamed and every copy of the game shares them (by path for a ROM rc_hash refuses). The app's
  microphone source, controller layout, widescreen and mods are listed as "Coming later" (phases 6, 3 and 4), and its Motion Paks are
  left out until the desktop reads a controller's gyro. Changes are stored at once and apply the next time the game starts, from the
  library or the command line. The command line beats them: `--rom-config` for the run, then `--console` and `--gba`.
  `--open library/settings` opens the first game's.
- **Pause menu** (the Pause binding: Esc, Guide, or Select + Start held; over the last frame, dimmed): Resume, Save state, Load state,
  Detached screens, Swap screens, Settings, Reset, Quit to library, Quit.
- **Save state / Load state**: the app's nine slots next to the ROM (`<rom>.ml0`, the quick slot, to
  `.ml8`, so states move between phone and PC), saved and loaded through `MelonInstance` on the
  emulator thread. Loading a state takes its save into memory only (`SaveManager::SyncFromState`), so
  the `.sav` on disk isn't touched until the game saves again. Each slot shows a screenshot (a 256x384
  PNG, top over bottom, as the app's), kept in the profile's `ss_screenshots` folder like the app keeps
  them in its own files; they don't travel with the states. The screenshot is read from the frame the
  game is paused on, since the app's `ScreenshotRenderer` only takes one as the next frame runs. Saving over a used slot asks
  first, and X (Delete) on a used slot deletes it, once asked, to the recycle bin with its screenshot.
- **Settings**: six tabs (L / R): Video (renderer, internal resolution, the renderer settings, the screen filter in the app's list and
  order, smooth gradients, DS screen colors, screen shadow, JIT), Display (screens together or detached, layout, swap, each detached
  screen's fit, the bottom window's title bar and on-top, fullscreen mode, vsync, theme), Emulation (console, custom BIOS and its folders through the Windows folder picker, boot screen, fast-forward
  speed), Audio (on/off, volume, latency, mute while fast forwarding), Controls (the input setup, rumble) and Library (the ROM folders:
  A changes one through the folder picker, X removes it, then Add a ROM folder and Rescan the library). A change is saved at
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
| `mouse-down X Y`, `mouse-up X Y` | the pointer moved to X,Y of the main window and the left button pressed there or let go (a `wait` between the two holds it: a long press) |
| `axis NAME VALUE` | a stick or trigger of the `--virtual-pad` (`axis rightx 20000`, `axis righttrigger 32767`) |
| `wait N` | N passes of the frame loop (UI frames while a menu is up) |
| `sleep MS` | MS milliseconds of real time, the game running on unless it's held |
| `frame N` | the game run to its frame N and held there (paused before frame N runs) |
| `shot F.bmp` | the window as drawn now, menus included (detached: `F_top.bmp`, `F_bottom.bmp`) |
| `screenshot F.bmp` | the game's last frame at the internal resolution |
| `wait-scan` | until the library's scan, and a zip or 7z being unpacked to play, are done |
| `wait-sync` | until the metadata sync has nothing to do (while a game runs, it waits for the game) and the art it published is drawn |
| `quit` | the end of the run (the run also ends after the last step) |

It starts on the library, or once the game's first frame is shown. Each step gets a press and a
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

### Game library

The home screen (`src/ui/LibraryScreen.cpp`, the app's launcher from `docs/plan-launcher.md`): the games in the ROM folders as a
**List** (banner icon, title, developer and file name, when it was last played), a **Grid** (9:10 cards with the banner icon on a card
tinted with its colors, the title on two lines, region and developer tags) or a **Mosaic**, cycled with L / R or the List / Grid / Mosaic
buttons (`rom_list_view_mode`: `list`, `grid` or `mosaic`, the app's values). Above them: Sort (Title A-Z and Z-A, Recently played and
Least recently played, as the app sorts), Filter (All, Played, Not played; not saved, as in the app), Search (the title and the file
name, typed on the on-screen keyboard; X clears it) and a menu (Open ROM, ROM folders, Rescan the library, Settings, Quit), which Start
or Guide opens too. The D-pad moves between games with the focus ring, up from the first row reaches the controls, left and right stop
at the edges, and the view scrolls with the focus. A press on a game opens its details when it's let go; held for half a second (the
mouse too) it opens the game's settings instead. The game the focus was on (`desktop_library_focus`, its path) and the view are kept
for the next run. Every view draws only the rows on screen (`ImGuiListClipper`) and makes each icon's texture the first time it's
drawn, so a library of hundreds of games scrolls as one of six. A game without a banner title shows its file name and an italic "No
metadata"; one with a display name of its own (its game settings) shows that. An empty library says why: no folders yet (Add a ROM
folder, Open ROM), nothing found in them (Add a ROM folder, Rescan), or nothing matching the search or filter (Show all games).

The **Mosaic** (`src/ui/MosaicLayout.cpp`, a port of the app's `MosaicLayout.kt`) packs square-ish cells (at least 150 wide and 150
tall at 720p, so seven columns there, never fewer than four) like CSS's dense grid, under a label for the order ("A TO Z", "RECENTLY
PLAYED"). The two most recently played games lead as 2x2 featured tiles whatever the sort, with a Continue or Recent label, when they
were played and a menu button for the game's settings; the first one also has a Continue pill that starts it at once. Down on a
featured tile goes into its pill (or menu button), up comes back out, and left and right move between the two. A game with title-screen
art on disk (`LibraryEntry::HasTitleArt`) gets a 2x1 tile if its RetroAchievements hash, hashed as Kotlin's `String.hashCode`, is a
multiple of six, so the same games are wide on the phone and the PC and a filter or sort doesn't reshuffle them (`--pretend-title-art`
and `--pretend-wide-tiles N` widen the choice for tests). A tile with art has it filling the tile, cut to fit, with the title on a scrim
(a wide tile shows the title screen, its top screen when the image has both); one without shows the banner icon on its tinted card
with the title under it. The packing is redone when the tiles' sizes or the number of columns change (art arriving that changes no
tile's size moves nothing), and the clipper draws the bands (rows no tile crosses) that are on screen.

**Scanning** (`src/Library.cpp`) runs on a worker thread when the library is first shown (not for a game started from the command
line), when the folders change, and on Rescan. It walks the folders and their subfolders for `.nds`, `.dsi`, `.zip` and `.7z` files,
and reads each new or changed one (its size or last write time differs) as the app's ROM processors do (`src/RomFile.cpp`): the
header's game code, region and DSiWare flag (a header too short or ARM9/ARM7 code over 0x3BFE00 bytes is not a ROM), the banner's
title in Windows' display language with English to fall back on (the app reads English only; `MELONADE_BANNER_LANGUAGE=0`-`7` picks
one for a test), its developer line, its icon (the shared `RomIconBuilder`; one of a single color counts as none) and the
RetroAchievements hash (rcheevos' `rc_hash` Nintendo DS method, the one the app's `RomProcessor` copies). An archive's ROM is the first
`.nds`, `.dsi` or `.ids` file in it; a zip is only decompressed as far as those reads go, a 7z is decoded whole (the LZMA SDK decodes
whole blocks). Unchanged files aren't opened again, files that are gone drop out (unless their folder isn't there at all, a drive
unplugged), and online-only files (OneDrive's cloud files) are listed with no metadata instead of being downloaded, then read once
they're on the disk. A file that can't be read (a damaged archive, one too big for memory, a locked file) is listed without details
and read again at the next scan; an archive whose ROM claims more than 1 GB isn't decompressed at all. While `--rom-folder` sets the
folders for a run, the saved folders' rows stay in the database, only hidden. A zip's names without the UTF-8 flag are read as code page
437, and a file whose name UTF-8 can't hold (a lone surrogate) is skipped. The header shows the progress ("Scanning 12 of 40"). Quitting
stops a scan or an unpack at the next block it reads, a 7z decode included. Japanese, Korean and Chinese titles draw with Windows' own
fonts, merged into Roboto.

**library.db** (SQLite, in the profile folder, so a hidden run's is temporary) holds a `roms` table (path, the archive's entry, the
file's size and time, what was read, the icon as 32x32 RGBA, the whole ROM's `crc32`/`md5`/`sha1`, which the metadata sync fills), a
`play_history` table (last played, by path, so it outlives rescans and covers games started from the command line) and, since schema
version 2, a `rom_config` table: each game's own settings by its `game` key, the ROM's RetroAchievements hash (or its path when rc_hash
refuses it), one row per field that differs from the default under the app's `RomConfigDto` names and values (`runtimeConsoleType` =
`DS`/`DSi`, `customName`, `gbaSlotConfig.type` = `GbaRom`/`RumblePak`/`MemoryExpansion`, `gbaSlotConfig.gbaRomPath`; rows with other
keys are left alone). A game booted from outside the library is read for its hash before it boots.
Schema version 3 adds the metadata sync's tables (see below). The version is `PRAGMA user_version`; a new version is a new entry in
`LibraryDb.cpp`'s migrations, run once when the file opens, so an older `library.db` gets the new tables the first time this build opens it.

**Playing** a game from the library, the Open ROM dialog or the command line records when it was played, and applies its game
settings (the console and the GBA slot; the log says `game settings: ... (by <hash>); boots as DS (<why>)`). A zip or 7z is unpacked into
the profile's `rom-cache` folder first (on the worker, so the menus keep drawing; the command line's on the spot), named for the
archive, its entry, size and time so a changed archive is unpacked again; the cache keeps the last three. The save and the slots go
next to the archive, as for a plain ROM.

### Metadata and art

The library's details and art (`src/MetadataSync.cpp`) come from RetroAchievements as the app gets them (`RomMetadataSyncWorker`,
`RomMetadataSyncPlanner`, `docs/plan-launcher.md` sections 2 and 3), with the libretro thumbnails as the fallback that needs no
account. A sync runs on a worker thread after each library scan, when a Web API key is saved, on Refresh metadata and with `--sync-now`,
never while a game runs: a game starting stops it (its requests are cancelled, a key test's too) and the library coming back asks for it
again. A scan that finishes while a sync runs, or while one waits to try again, leaves it as it is (the app's KEEP). Removing the key
stops only the RetroAchievements part of a running sync, and turning the libretro thumbnails off only theirs. In order:

1. **ROM hashes**: each ROM's CRC32, MD5 and SHA1, once per file (one pass over the decompressed ROM; online-only files are skipped),
   into `roms`.
2. **Hash library**: RetroAchievements' connect API `hashlibrary` for DS (18) and DSi (78), replaced at most once a day, needs no key.
   It says which game id each ROM's RetroAchievements hash is, and which hashes RetroAchievements doesn't know.
3. **RetroAchievements**, with a Web API key: `API_GetGame` once per distinct game for the life of the install (rows with
   `fetch_status` 0 ok or 1 not found aren't asked again; 2 failed is retried every sync), half a second apart, in batches of 20 with the
   art after each batch: box art, else the title screen, else the icon, plus the title screen for the mosaic's wide tiles, from
   `media.retroachievements.org`, two downloads at a time in random order. The app's rules: a 429 stops the run and it tries again 15
   minutes later (then 30, 60, ...; five runs in all, as the app's `MAX_RUN_ATTEMPTS`), counting only while no game runs; a 5xx, a 404, a redirect, no connection or an answer that isn't game data marks that game
   failed and stops the same way; another 4xx or a malformed answer marks the game failed and goes on; 401/403 (the key) stops for good.
   Three "not found" in a row trigger one control call for game 1: an answer means they stand, anything else counts them as failed and
   stops. An image that fails for good (404, too large, not an image) has its path cleared, which moves the game to its next kind; three
   image failures in a row (or a 429) stop the art for the run, so a blocked or broken media host can't clear the library's paths.
4. **libretro thumbnails** (Settings > Library, on by default), for games RetroAchievements has no box art or title screen for: the
   No-Intro DS DAT from libretro-database (downloaded the first sync, kept in the profile's `libretro` folder, again after Refresh
   metadata once it's two weeks old), each ROM's name by its CRC32, else by its game code (a trimmed ROM's CRC32 never matches the
   full-size dump) with its region's and the retail release preferred, then `Named_Boxarts/<name>.png` and `Named_Titles/<name>.png` with
   each of ``&*/:`<>?\|"`` in the name made `_`. They come from the libretro-thumbnails repository on GitHub
   (`raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Nintendo_DS/master`): `thumbnails.libretro.com` answered 404 for the DS
   folder on 2026-11-21. Many games have no title screen there, so a missing image doesn't count as a failure; it's remembered
   (`libretro_art`, until a refresh two weeks later) only once an image of the same run was saved, and ten missing in a row with none
   saved stop the run, so a blanket 404 can't mark the whole library. Three other failures in a row, or a 429, stop it as above.

Art goes in `<profile>\media\ra\<game id>\<box|title|icon>.png` and `<profile>\media\libretro\<name> ~<hash>\<box|title>.png`, and
stays until Clear downloaded art. The libretro folder is the thumbnail's name made safe for Windows (control characters, leading spaces,
trailing dots and spaces dropped, so `.` and `..` have none and are skipped; a reserved device name such as `CON` or `COM1.txt` gets a
`_` in front; at most 120 bytes) with the name's hash after it, so no two names share one, and the final path is checked to be in
`media\libretro`. An image over 4096 pixels a side or 4 megapixels in all doesn't count as an image (its size is read before it's
decoded). WebP, which RetroAchievements allows and stb_image can't read, is decoded with Windows' WIC when Windows has a WebP decoder
(Windows 11, and 10 with the WebP Image Extensions); without one, a WebP is skipped without counting as a failure or clearing its path.
Requests have a two-minute deadline in all (ten for the DAT), so a server sending a byte at a time can't hold the sync. The menus key each image's texture to the file's save stamp, so art replaced on disk (a game whose image changed, or art cleared and
downloaded again) shows the new image at once. A tile prefers RetroAchievements' box art, then its title screen, then libretro's box art and title screen,
and the RetroAchievements icon last. The menus read images with stb_image on a worker thread, shrunk to at most 512 pixels a side, and
make their textures as they're first drawn (the ones not drawn for a while go once there are more than 160). The game's title falls
back on RetroAchievements' when the banner has none, as the app's `displayTitle`.

The **details screen** lists RetroAchievements' publisher, developer, genre and release date (by its granularity: "Aug 22, 2005", "Nov
2005" or "2007"), and two credit lines: where the details came from, as the app's sheet tells them apart ("Metadata from
RetroAchievements · cached 3 days ago", "· due for a refresh", "Not found on RetroAchievements · checked today", "No metadata from
RetroAchievements yet · last tried today", "Hash not found on RetroAchievements", "Can't be looked up on RetroAchievements", "No
RetroAchievements Web API key set", "RetroAchievements metadata not downloaded yet"), and where the art came from with the credits.

**Settings > Library** has the RetroAchievements Web API key (from retroachievements.org, under Settings; typed on the on-screen keyboard
as dots, or pasted with Ctrl+V; X removes it), stored in `settings.json`'s `desktop_ra_web_api_key` encrypted with Windows' DPAPI for
the Windows user, so the file alone gives it to no one; Test the key (one `API_GetGame` call for game 1); Use libretro thumbnails
(`desktop_libretro_thumbnails`); Refresh metadata (rows fetched over two weeks ago, or whose image failed, fetched again, and libretro
images found missing that long ago asked for again); Clear downloaded art (the sync stops and the media folder is deleted; the next
sync downloads the art again with no API calls); and a status line ("Art: 5 of 7 games, last synced Nov 21, 01:01 AM", and what it's
doing). Requests carry the User-Agent `melonadeDS-desktop/<version>` and nothing about the user; the key is never logged.

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
X, north = Y, as an Xbox pad's labels say; the default for Xbox, PlayStation and other pads). The menus follow it too. Every joystick SDL sees is logged
(`joystick N:` with its name, GUID, USB ids and whether it's a game controller, after a `joystick hints:` line), and one SDL
has no mapping for gets a toast ("... has no controller mapping"), since it can't be bound. The input setup opens on the page of
the device used last.

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

### Screen filters

The screens are drawn the way the app's `DSRenderer` draws them, through ports of its code in `src/render/`: `ScreenRenderer` (the
screen quads, the screen shadow, deband in the screen shader where that's cheaper than the pre-pass, GPU::Comp1x's 1x shader and the
output cap's need; one per window, as the app has one per surface), `VideoFilterPipeline` (the filter's pre-passes in order at their
scale, deband as a pre-pass, the 1x expand pass, the Lua bloom, vignette and tilt-shift passes) and `Shader` (the app's `Shader` and `ShaderFactory`). The
filters are the app's twelve (`VideoFiltering`; the settings list leaves out Scanlines as the app's does), with smooth gradients
(deband), the DS screen colors (Pokefan531's color profile) and the screen shadow. `Presenter` still places the screens with the four
layouts and the fits; the app's layouts, backgrounds and layout editor come next. `--window-shot` shows the filtered screens,
`--screenshot` the raw frame as before.

The shaders aren't copied by hand. `shaders/` holds what `ShaderProgramSource.kt` and `VideoFilterShaderProvider.kt` give: a `.vert`
and a `.frag` for every shader (the tilt-shift and bloom ones built on each `get()` too), and `shaders.json` with each shader's texture filtering
and the filter table (each filter's screen shader, its pre-passes with their scale and `nativeResolutionOnly`, and whether it can fold
deband in). The build embeds the folder in the exe (`EmbedShaders.cmake` makes `generated/shader_files.inc`). The app's JVM unit test
`DesktopShaderExportTest` writes them, from PowerShell at the repo root:

    .\gradlew.bat :app:testGitHubProdDebugUnitTest "-Pandroid.injected.build.abi=x86_64" "-Pmelonade.exportShaders" --tests "me.magnum.melonds.common.opengl.DesktopShaderExportTest"

and every unit test run without `-Pmelonade.exportShaders` fails when the files here differ from the Kotlin, or one is left over from a
shader that's gone, so they can't drift. After changing a shader or the filter table in the app, export them and commit `shaders/`
with the change. Their license notices (2xBR and HQx are GPL 2 or later, MMPX MIT) are in `licenses/shaders.txt`, which `build.sh`
copies into the release's `licenses/`. The shaders compile on the desktop's GLES 3.2 context as they are (GLSL ES 1.00 and 3.00). The loader adds the app's
defines (`COLOR_CORRECTION`, `DEBAND`) on the line after `#version`, as `ShaderFactory` does, and makes one change of its own
(`ForDesktop` in `src/render/Shader.cpp`): the app's precision block (`#ifdef GL_FRAGMENT_PRECISION_HIGH` highp, else mediump) becomes
plain `precision highp float;`, the branch phones take. NVIDIA's ES driver doesn't define that macro, so without it those shaders ran
at 16-bit mediump there, and 2xBR, whose color compares need highp, drew as no filter at all. Deband folds into the screen shader
whenever the app's would (its `debug.melonade.debandfold` default); the desktop has no switch for it.

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
- The screens are drawn with the app's filters and deband ([Screen filters](#screen-filters)), but not yet with its layouts,
  backgrounds or frame pacing to the display's vsync. Lua scripts, rewind and
  RetroAchievements are compiled in (shared code) but not set up yet. Pacing is the app's timer
  limiter at the DS's 59.83 Hz instead of the app's 60.
- DS mode only (no DSi NAND), no multiplayer, Wi-Fi, mic or camera.
- The library reads banner titles in Windows' display language (English when the banner has none in it); the app reads English.
- No metadata or art sync yet (phase 2 step C), so the Mosaic has no art and no wide tiles outside tests. The game settings have no
  microphone, layout, widescreen or mods yet; the app's long press is holding confirm or the mouse for half a second here. They're
  kept by RetroAchievements hash, where the app keeps them with each ROM file.
- A hidden run (`--hidden`) takes no input from real controllers, only `--ui-script`'s and its `--virtual-pad`'s, and never
  opens the file or folder dialog.
- The sound goes through a deeper buffer than the app's, with larger targets (see [Sound](#sound)).
- The process opts out of Windows 11's power throttling, which otherwise cuts the frame rate to a
  third when the window isn't in front.
