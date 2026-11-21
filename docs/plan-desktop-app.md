# Plan: melonadeDS for Windows (a real desktop app)

## Status (2026-11-21)

Phase 0 on `feature/desktop-shared-native` (11-20, merged 11-20 in #22): `melonade-shared` (`app/src/main/cpp/SharedNative.cmake`) without `debug/`, which stays Android-only with a desktop stand-in; the desktop runs `MelonInstance` and `FrameQueue` on two threads, with the same screenshots as before in every scripted comparison. The Fold and Pixel 8 regression runs came first (`perf_logs/2026-11-20-shared-native*`). Decided: Dear ImGui menus on the existing SDL2 + GLES 3.2 window, full controller navigation, and every feature the Android app has, built in phases. The current `desktop/` test frontend becomes this app; its scripted-run options keep working. Added 11-20: [detached screens](#detached-screens), each DS screen in its own window, sized and placed on its own (phase 1, layouts per window in phase 3).

Phase 1 on `feature/desktop-app-shell` (merged 11-20 in #23). Step A (11-20): `settings.json` in a profile folder (`--profile`, a temporary one for `--hidden` runs), windowed, borderless and exclusive fullscreen with remembered places, per-monitor DPI, and detached screens (F9) with their command line; the visible two-monitor check is still open. Step B (11-20, same branch): Dear ImGui 1.93 menus in the app's Material 3 look (start screen with Open ROM and recent games, pause menu, nine save state slots with screenshots, a placeholder settings screen, on-screen keyboard), controller, keyboard and mouse navigation, a GUI exe, and `--open` / `--ui-script` for hidden menu checks; a visible check with a real controller is still open. Step C (11-20, same branch): settings screens on five tabs with the app's keys, the input mapping (per-device primary and alternate bindings, hotkeys with controller combos, Nintendo or positional face layout per pad, `gamecontrollerdb.txt`, hot-plug, the right stick's touch cursor, rumble) in `input.json`, sound through an SDL callback with the app's drift control over a deeper ring, slot delete and overwrite questions, the app icon and version info, and a ready-to-zip release folder from `build.sh`; checks with real controllers and speakers are still open, which ends phase 1 once they pass.

Phase 2 step A on `feature/desktop-library` (11-20, merged 11-21 in #24): the game library as the home screen, List and Grid views with the app's sort (title, recently played), a filter (All, Played, Not played) and search through the on-screen keyboard, controller focus across tiles and up to the controls, virtualized with icons made as they're drawn, and a game details screen with Play (the press that opened it can't press Play). ROM folders (`rom_search_dirs`) on a Library settings tab, scanned on a worker for `.nds`, `.dsi`, `.zip` and `.7z` (vendored miniz and the LZMA SDK's 7z decoder): header, banner title in Windows' language with English as the fallback, icon (`RomIconBuilder`) and the RetroAchievements hash (`rc_hash`, built from the app's rcheevos); unchanged files aren't read again, missing ones drop out, online-only OneDrive files aren't downloaded. `library.db` (vendored SQLite, schema version 1 with a migration list for step C) in the profile, with the last-played times from the library and the command line. Archives unpack into the profile's `rom-cache` before they boot, from the menus or the command line. `--rom-folder`, `--open library` / `library/details` / `settings/library` and the `wait-scan` script step check it all hidden; checks with a real controller and a big real library are still open. Step B (Mosaic view, per-game settings) and step C (metadata and art sync) followed.

Phase 2 step B on `feature/desktop-library` (11-20, merged 11-21 in #24): the Mosaic view, a port of the app's `MosaicLayout.kt` (the two most recently played games as 2x2 featured tiles with Continue and a menu button, 2x1 tiles for games with title art picked one in six by the RetroAchievements hash as the app does, dense packing, columns by width, clipped by rows), with down and up into and out of a featured tile's pill and menu button; List, Grid and Mosaic cycle on L / R. Per-game settings (`RomConfig`) in a `rom_config` table (schema version 2), by the ROM's RetroAchievements hash (the path without one) so they survive a move or rename, with the app's field names: display name, boot system (locked to DSi for DSiWare) and GBA slot (None by default as in the app, GBA ROM, Rumble Pak, memory expansion; the global Rumble Pak setting is gone) apply when the game boots from the library or the command line (`--rom-config`, `--console` and `--gba` beat them for a run); mic, layout, widescreen and mods are listed as "Coming later". They open from the details screen, a featured tile's menu button, or holding confirm or the mouse on a tile. Start / Guide opens the library's menu, and the last focused game and the view carry over to the next run. A drive's root as a ROM folder was checked with a `subst` drive, unplugged between runs too. Wide tiles had no candidates until step C's art (`--pretend-title-art` stood in); checks with a real controller and mouse are still open.

Phase 2 step C on `feature/desktop-library` (11-21, merged 11-21 in #24; a real Web API key and cover art downloads checked by you): the metadata and art sync, a port of the app's `RomMetadataSync*` rules on a worker thread with a small WinHTTP client. Each ROM's CRC32, MD5 and SHA1 once per file; RetroAchievements' hash library for DS and DSi at most once a day; with a Web API key, `API_GetGame` once per game and its art (box art, else title, else icon, plus the title screen), two downloads at a time in random order, with the app's back-off, image-failure, control-call and refresh rules; the libretro thumbnails as the fallback with no account, by the No-Intro DAT's CRC32 or the game code and region for trimmed ROMs, from the libretro-thumbnails repository on GitHub (thumbnails.libretro.com answered 404 for DS on 11-21), with missing images remembered only once a run has saved one. Schema version 3 (`ra_hash_library`, `ra_game_metadata`, `libretro_art`, `sync_state`); art in the profile's `media` folder, read with vendored stb_image. Art in List, Grid and Mosaic (real wide tiles from title screens), the details screen's RetroAchievements facts and the app's credit line, and on the Library settings tab the key (DPAPI-encrypted in `settings.json`, typed as dots), a Test button, the libretro switch, Refresh metadata, Clear downloaded art and a status line. No sync while a game runs. `--no-sync`, `--sync-now`, `--ra-web-api-key` and the test hosts (`--metadata-base-url`, `--thumbnails-base-url`, `--dat-url`) check it all hidden against a local server; a real Web API key and a big real library are still open.

Phase 3 step A on `feature/desktop-presentation` (11-21, not merged): the screens are drawn by a C++ port of DSRenderer's screen drawing and `VideoFilterPipeline` (`desktop/src/render/`): all 12 filters, deband (folded into the screen shader under the app's rules), DS screen colors, screen shadow, halves-at-1x and the tilt-shift passes. The shaders aren't copied by hand: `DesktopShaderExportTest` (app unit tests) writes every `ShaderProgramSource` and the filter table to `desktop/shaders/` with `-Pmelonade.exportShaders`, and otherwise fails when those files differ byte for byte; CMake embeds them. The one desktop change is a loader swap of the app's `GL_FRAGMENT_PRECISION_HIGH` block for `precision highp float`, because NVIDIA's GLES driver doesn't define the macro and 2xBR came out as no filter at mediump. Video tab "Image" settings with the app's keys (`video_filtering`, `video_deband`, `video_ds_screen_colors`, `video_screen_shadow`) and run-only `--filter`, `--deband`, `--ds-colors`, `--screen-shadow`, `--tilt-shift` and `--output-cap`; shader license notices in the release's `licenses/shaders.txt`. Checked hidden: every filter with deband on and off, gl and soft, 1x and 2x, detached, with `--screenshot` byte-identical to before. Open: a side-by-side with a phone (LCD at exactly 2x looks green, likely the shader's math); whether the output scale need (sent only with `--output-cap`, since it caps frames to the window and changes scripted screenshots) becomes the default; the Dave Hoskins hash credit has no full MIT text. Steps B (layouts, backgrounds, editor, per-window layouts) and C (pacing, WideMelon camera) follow.

UI mockup (seven screens: library, game details, pause menu, save states, video settings, input setup, in game): https://claude.ai/artifact/Pq7RMiy7XqRLTVKk7NNU4n. Made-up games and art; private, so it has to be shared before anyone else can open it.

## Goal

A Windows `.exe` you can double-click and play from the couch: a game library, a pause menu, settings, save states, controller mapping, and then everything else the app has (layouts and filters, Lua mods, RetroAchievements, rewind, cheats, DSi). Every screen works with a controller alone, with keyboard and mouse as well.

## Where we start

- `desktop/` (about 2.2k lines) runs the core with soft, OpenGL and Compute on one thread, 4 fixed layouts, hardcoded keys, SDL controller with a fixed Nintendo layout, raw savestates, `SDL_QueueAudio`, and a 59.83 Hz timer. It compiles none of the app's C++.
- The app's own C++ in `app/src/main/cpp/` (about 13k lines) is mostly portable. About 9k lines can be shared as is: `MelonInstance`, `Configuration.h`, `EmulatorArgsBuilder`, `SDCardArgsBuilder`, `CartLoader`, `DSiSupport`, `SaveManager`, `RewindManager`, `renderer/FramePacing`, `FrameQueue`, `ScreenshotRenderer`, `retroachievements/`, `scripting/` (minus `LuaScriptingJNI.cpp`), `RomIconBuilder`, `debug/`.
- Android ties in that code are few: EGL sync fences in `MelonInstance.cpp` and `FrameQueue.cpp`, the `MelonDSAndroid::` globals (`internalFilesDir`, `eventMessenger`), and `pid_t`/`gettid`.
- Everything else the user sees is Kotlin (about 63k lines) and gets rewritten: presentation (`DSRenderer.kt`, `VideoFilterPipeline.kt`, shaders in `ShaderProgramSource.kt`), layouts, the library, settings, input mapping, cheats storage, the RetroAchievements network layer, the Lua folder walk and header parser, `WideCamera.kt`.

## Architecture

**One exe, two modes.** `melonade.exe` with no arguments opens the library. With a ROM it boots straight into the game. The test options (`--frames`, `--press`, `--set`, `--screenshot`, `--hidden`, `--unthrottled`, ...) still work and skip the UI, so scripted renderer checks don't change. The app links as a GUI program (no console window); test mode attaches to the parent console for output. See [Command line](#command-line) for the rules.

**Shared native layer.** The portable app C++ builds into a static library used by both the Android app and the desktop, so the desktop runs the same `MelonInstance`, saves, rewind, RA runtime and Lua engine as the phone instead of its own copy. Android changes are kept small:
- a `GpuFence` wrapper (EGL fence on Android, `glFenceSync`/`glWaitSync` on desktop),
- the `MelonDSAndroid::` globals moved behind a small frontend-context struct each side fills in,
- `pid_t`/`gettid` behind `#ifdef`.
The Android build must stay behavior-identical; that gets a phone run before it merges.

**Desktop side of the native layer** (new, replaces what `MelonDS.cpp`, `PlatformAndroid.cpp` and the JNI emulator loop do on Android):
- `DesktopPlatform.cpp`: threads, files, save hooks (grows out of today's `Platform.cpp`).
- `EmulatorThread.cpp`: port of `emulate()` from `MelonDSAndroidJNI.cpp` (pause/resume, fast forward, fps, pacing), without JNI.
- `DesktopEventMessenger`: implements `MelonEventMessenger` (rumble, RA events, Lua requests) by posting to the UI thread.

**Threads.** Emulator thread and UI/present thread, each with its own GLES context shared through SDL (`SDL_GL_SHARE_WITH_CURRENT_CONTEXT`), handing frames over through `FrameQueue` exactly like the app. Menus stay smooth while the game runs, and the pause menu can draw over a live or frozen frame.

**UI.** Dear ImGui with the SDL2 and OpenGL3 backends (GLES 3 shader path), `ImGuiConfigFlags_NavEnableGamepad` and `NavEnableKeyboard`.
- Full-window screens, not floating windows: Library, Game details, Settings, Input setup, and an in-game overlay menu. Built for a TV at a distance first, so large type and large focus targets, and it still looks right on a monitor.
- Styled from the app's Material 3 palette and fonts, with Material Symbols as an icon font. Light and dark.
- Controller: D-pad/left stick moves focus, A confirms, B goes back, L/R switch tabs, Start/Guide opens the pause menu in game. An on-screen keyboard for text fields (search, RA login, layout names), since ImGui has none.
- UI code is split into screens (`ui/LibraryScreen.cpp`, `ui/SettingsScreen.cpp`, ...) with a small navigation stack, so each phase adds screens without touching the others.

**Storage.** Everything under `%APPDATA%\melonadeDS\`, or next to the exe when a `portable.txt` sits there.
- Settings: one JSON file using the Android preference key names, so an Android settings backup (`SettingsBackupFormat`) can be imported later.
- Library, ROM metadata, cheats and RA data: SQLite (vendored amalgamation).
- Saves and states next to the ROM by default, like the app, with the same `.ml0`–`.ml8` slot names so state files move between phone and PC.

**Other libraries.** SDL2 (already used), Dear ImGui, SQLite, nanosvg (backgrounds), tinyxml2 (cheats.xml), stb_image (art), WinHTTP for network (no extra DLL). All vendored or static, so the shipped folder is the exe, `SDL2.dll` and an `assets/` folder.

## Command line

Anything you can reach through the menus for a test, you can also start from one command, so a test never needs someone to click through screens.

**Straight into a game, with the full app.** A ROM on the command line skips the library and boots it; the pause menu and everything else still work. The options from today's test frontend keep their names and meaning:

    melonade.exe --sav C:\tmp\test.sav --state C:\tmp\battle.ml0 game.nds

- `--sav F`: use this save file instead of the one next to the ROM. Since loading a state rewrites the save, point it at a copy.
- `--state F`: load this savestate right after boot. `--slot N` loads the game's own slot N instead.
- `--bios DIR`, `--renderer`, `--scale`, `--wide`, `--jit`, `--set Name=Value` and the renderer toggles (`--dither`, `--hires-capture`, `--precise-depth`, ...) as today.

**Overrides don't stick.** Settings given on the command line apply to that run only and are never written to the settings file, so a test can't change your normal setup. Each phase adds flags for what it builds, for example `--layout NAME`, `--filter NAME`, `--mod FILE` (with `--mod-option key=value`), `--cheats on|off`, `--no-ra` / `--ra-hardcore`, `--rewind on|off`.

**Separate profile for tests.** `--profile DIR` uses DIR for settings, library database, RA login and art instead of `%APPDATA%\melonadeDS`, so agents and scripts run against a throwaway profile and never touch your library or saves. A test run with no `--profile` and a `--hidden` flag gets a fresh temporary one.

**Headless and scripted.** `--hidden`, `--frames N`, `--unthrottled`, `--no-audio`, `--press F[-L]:KEYS`, `--screenshot F.bmp` and `--window-shot F.bmp` work as now, with or without the UI. `--open SCREEN` (for example `settings/video`, `pause`, `mods`, `achievements`) opens a menu screen directly, and `--ui-script F` drives the menus from a file (see [Testing](#testing)), so menu checks start from one command too.

`--help` lists every option. Each phase's "done when" includes that its new features can be started and checked this way.

## Detached screens

Something the phone can't do: the top and bottom screens can each get their own window, moved and resized on their own. For example the top screen fullscreen on the TV and the bottom screen in a small window on a touch monitor, or both side by side at different sizes on one monitor.

**Modes.** A "Screens" setting: *together* (one window, the usual layouts) or *detached* (two windows). A hotkey and a pause-menu entry switch between them while a game runs. Swap screens still works when detached: the windows keep their place and size and swap what they show.

**Each window on its own.**
- Its own size, position and monitor, remembered separately, and its own fullscreen (borderless on the monitor it's on), so one can be fullscreen and the other not.
- Its own fit: keep aspect (black bars), integer scale, or stretch. With WideMelon the top screen's aspect follows the wide width.
- Options for the bottom-screen window: no title bar, and always on top, so it can sit as a small touch pad over another window.
- Window titles say which screen they show ("melonadeDS: top", "melonadeDS: bottom").

**Which window does what.** The main window (the one the library opened in) keeps the menus, the pause menu, toasts and the perf overlay; ImGui draws only there (no multi-viewport). Touch comes from the mouse or a touch screen in whichever window shows the bottom screen, and the right-stick touch cursor draws there too. Keyboard and controller input work whichever window has focus. Closing the second window goes back to *together*; closing the main one quits or goes back to the library as now.

**Drawing.** The UI/present thread draws both windows from the same `FrameQueue` frame with one GLES context, `SDL_GL_MakeCurrent` on each window in turn (they share a pixel format, so that works on Windows). Only the main window waits for vsync (swap interval 1); the second swaps with interval 0, otherwise two vsync waits per frame would halve the frame rate. On two monitors with different refresh rates, pacing follows the main window's monitor and the second one may tear or repeat frames; that's accepted.

**Layouts and filters (phase 3).** When detached, each window gets a one-screen layout from the layout system: its own background, screen position and size inside the window, and the same filter as together mode unless a per-window filter is set. The layout editor edits the focused window's layout. Layout JSON gets a `detached` pair so the app can ignore it.

**Command line.** `--detach` starts detached; `--window W,H[,X,Y]` and `--window2 W,H[,X,Y]` set each window's size and place for the run; `--window-shot F.bmp` saves both windows as `F_top.bmp` and `F_bottom.bmp` when detached. Like other overrides, they never change the saved window places.

## Game metadata

**ScreenScraper is shelved (2026-11-18).** There are no developer credentials, and ScreenScraper only allows its API in free apps that are distributed, which melonadeDS isn't yet. Until that changes, phase 2 uses RetroAchievements (as the app does) with libretro thumbnails as the fallback. The hashing, libretro and cache parts below apply to that as well; the ScreenScraper parts are kept for when the app goes public.

The plan for then: ScreenScraper becomes the main source, the same one Cocoon credits for its metadata (Cocoon's platform list, `D:\Dev\CocoonFE\android\nds.yaml`, maps DS to ScreenScraper system 15). Researched 2026-11-18 from ScreenScraper's API doc (https://www.screenscraper.fr/webapi2.php, French, marked beta and subject to change) and the Skyscraper, ES-DE and RomM clients; anything marked unverified needs a real call to confirm.

**Sources, in order:**

| Source | Needs | Gives |
|---|---|---|
| ScreenScraper API v2 | our developer credentials; the user's ScreenScraper login is optional and raises their quota | titles per region, synopsis per language, publisher, developer, genres, release dates, players, rating; box 2D/3D, screenshots, title screens, logos (`wheel`), fan art, video, manuals |
| libretro thumbnails | nothing | box art, a screenshot and a title screen as PNGs, by No-Intro name. Fallback art when ScreenScraper has none, is down, or the user's quota ran out |
| RetroAchievements | an RA login (achievements), a Web API key (art, as in the app) | achievement counts and the RA art; its header-based hash also matches trimmed ROMs, so it can name a game the other two miss |

**Developer credentials.** ScreenScraper gives a `devid`/`devpassword` to an app's developer after a request on its developer forum (https://www.screenscraper.fr/forumsujets.php?frub=12); it won't give them to end users. The API may only be built into free apps that are distributed, unless they authorize otherwise. Skyscraper and ES-DE build theirs into the binary scrambled, so we do the same: the values sit in a git-ignored file that `build.sh` turns into a header, never in the repo, and `softname` is `melonadeDS-<version>`. Breaking the rules gets the app blacklisted (HTTP 426), so never borrow another app's credentials. Registering is your step.

**Finding the game.**
- One pass over the decompressed `.nds` (zip and 7z are unpacked first) computes CRC32, MD5 and SHA1, plus the size, the file name and the header's game code and region. Saved in the library database so it runs once per file.
- ScreenScraper `jeuInfos.php`: `systemeid=15`, `romtype=rom`, all three hashes, `romtaille`, `romnom` (file name only; a path gives HTTP 400), `output=json`. A 404 means not found. The response doesn't say whether it matched by hash or by name, so compare the hashes it returns with ours and count a mismatch as a name match.
- Trimmed ROMs will almost always miss on hash, because the reference dumps are full size. They fall back to the file name, then to the RA hash, then to the manual match. Whether `serialnum` takes the header game code, and in what form, is unverified; test it with a known game before relying on it. Whether DSi has its own system id is unverified too; DSiWare starts on 15.
- libretro: the No-Intro DS DAT from libretro-database maps CRC to name (its entries carry full sizes and the 4-letter serial, so a trimmed ROM can match by serial, preferring the entry for its region). It's downloaded on the first sync, not bundled, since no license is stated. Image URL: `https://thumbnails.libretro.com/Nintendo - Nintendo DS/Named_Boxarts/<name>.png`, URL-encoded, with each of `&*/:`<>?\|"` in the name replaced by `_`.
- "Fix match" from the launcher plan (section 4) uses ScreenScraper's `jeuRecherche.php?recherche=<name>`, which returns up to 30 likely games.

**Picking region and language.** Settings for a region order and a language. Names, dates and media try the user's region, then `wor`, `us`, `eu`, `jp`, then whatever exists, like ES-DE; video and fan art have no region. The synopsis uses the user's language, then English.

**Media.** By default box 2D (tiles and details), title screen (mosaic wide tiles), screenshots (details gallery) and the logo. Box 3D, video and manuals are off by default and can be turned on in settings. Images are requested through `mediaJeu.php` with `maxwidth` (512 for boxes, 640 for screenshots) to save bandwidth. A refresh sends the hash of the file we already have, and ScreenScraper answers `CRCOK`/`MD5OK`/`SHA1OK` instead of sending it again.

**Quotas and errors.** ScreenScraper requires clients to handle its quotas, and the limits differ by account, so they're read from the `ssuser` block that comes back with every response (`maxthreads`, `maxrequestspermin`, `maxrequestsperday`, `requeststoday`, `requestskotoday`, `maxrequestskoperday`), never hardcoded. A basic registered account was seen at 1 thread, 20,000 requests a day and 2,000 not-found a day. We run at most `maxthreads` requests at once, no faster than `maxrequestspermin` and no closer than 1.2 s apart, as Skyscraper does.

| HTTP | Meaning | What we do |
|---|---|---|
| 400 | bad parameters | log it, skip the game (a bug on our side) |
| 401 | closed to guests while the server is busy | pause the sync a few minutes, suggest a ScreenScraper login |
| 403 | bad developer credentials | stop, report it |
| 404 | not found | mark the game not found; no retry until a manual refresh |
| 423 | API closed | stop for this session |
| 426 | app blacklisted or too old | stop, tell the user to update |
| 429 | thread or per-minute limit | back off a few seconds, retry |
| 430 / 431 | daily quota / too many not-found today | stop until tomorrow, libretro art still fills in |

The body is checked as well as the status, as Skyscraper does. Not-found counts against its own daily limit, so a game that missed is never asked about again on its own.

**Cache.** Results in the library database keyed by SHA1 (ScreenScraper game id, the chosen fields, fetch time and status). Media in `<profile>\media\<source>\<game id>\<type>_<region>.<ext>`, kept until the user clears it. No sync while a game is running, like the app.

**Terms.** ScreenScraper's site content is CC BY-NC-SA 4.0, and nothing says caching for display in a free app is a problem. Downloaded media is never redistributed (no pre-scraped packs). The details screen and About credit "Data and media: ScreenScraper.fr". libretro art belongs to its publishers and gets a credit line too. Worth confirming on the forum when registering.

**Settings.** ScreenScraper login (optional, stored like a password, with a test button that shows the account's quota), region order, language, media types, "Scrape now", "Refresh metadata", "Clear downloaded media", and the RetroAchievements Web API key as in the app.

The Android app could move to the same source later, using the same developer credentials, but that's a separate decision.

## Phases

Each phase ends with something usable and is a separate `feature/desktop-*` branch. Sizes are rough lines of new or ported code.

### Phase 0: shared native layer (about 1.5k lines, some in Android code)

- Static library target for the portable app C++, used by `app/CMakeLists.txt` and `desktop/CMakeLists.txt`.
- `GpuFence`, the frontend-context struct, the `#ifdef`s.
- `DesktopPlatform`, `EmulatorThread`, `DesktopEventMessenger`; the desktop switches from its own `Emulator` class to `MelonInstance` and `FrameQueue`, on two threads.
- Done when: the desktop plays as today on the shared code, the scripted renderer comparisons give the same screenshots as before, and the Android app passes a Fold run (GL, Compute, saves, states, Lua overlay) with no change in behavior or fps.

### Phase 1: playable app shell (about 4k lines)

- ImGui set up with the theme, fonts and on-screen keyboard.
- Open ROM (file dialog or a simple folder list), pause menu: Resume, Save/Load state (9 slots with screenshots), Reset, Settings, Quit to menu.
- Settings screens for what maps onto `EmulatorConfiguration`: renderer, internal resolution, JIT, the renderer toggles (hi-res capture, filter 2D, sharper rotated 2D, edge marking, precise depth, seams), fast forward speed, audio, BIOS/firmware paths, console type.
- Input setup: per-device bindings for keyboard and each controller (SDL GameController with `gamecontrollerdb.txt`), primary and alternate binding like the app's `InputConfig`, hotkeys (fast forward hold/toggle, quick save/load, pause, swap screens, screenshot, fullscreen, rewind later) including button combos so a controller alone can reach them. Touch: mouse, or the right stick as a cursor with a button to tap, for controller-only play. Rumble through `SDL_GameControllerRumble`.
- Audio: SDL audio callback with the ring buffer and skew resampling ported from `MelonDSAudio.cpp`.
- Window: windowed, borderless fullscreen, exclusive fullscreen; remembers size and position; per-monitor DPI.
- [Detached screens](#detached-screens): top and bottom in two windows, each with its own size, place, fullscreen and fit; touch in the bottom-screen window; switch with a hotkey or the pause menu (about 500 lines).
- An app icon and version info in a `.rc` resource; `build.sh` makes a ready-to-zip folder.
- Done when: you can play a game start to finish with only a controller, never touching the keyboard.

### Phase 2: game library (about 3k lines)

- ROM folders in settings, scanning (`.nds`, `.dsi`, zip and 7z), header parse, the RA hash (port of `RomProcessor.kt`) and banner icons (`RomIconBuilder`).
- List, Grid and Mosaic views with sort, filter and search, following `docs/plan-launcher.md`; game details sheet with Play and per-game settings (`RomConfig`: console type, GBA slot, widescreen, mic, mods).
- Metadata and art from RetroAchievements through the Web API key, with the app's caching and back-off rules (`RomMetadataSync*`), and libretro thumbnails as the fallback that needs no account; art cached on disk. ScreenScraper is shelved, see [Game metadata](#game-metadata).
- Done when: the library opens with art on first launch after a sync, and controller focus works across all three views.

### Phase 3: presentation parity (about 4k lines)

- Port `DSRenderer` and `VideoFilterPipeline` to C++ with the shaders exported from `ShaderProgramSource.kt` by a unit test (all 12 filters, deband, tilt-shift, shadows, backgrounds, the Lua overlay pass). The same test fails when the desktop copies differ from the Kotlin, so they can't drift silently (step A; backgrounds and the overlay pass come with step B and phase 4).
- Layouts: the `UILayout`/`LayoutConfiguration` model and JSON format from `impl/dtos/layout`, the default layouts, and an ImGui layout editor (drag screens with mouse or controller, snap, resize). Same JSON as the app, so layouts can be copied over.
- Backgrounds with nanosvg for the built-in SVGs.
- Detached screens get a one-screen layout per window, editable in the layout editor (see [Detached screens](#detached-screens)).
- Frame pacing: `FramePacing` fed by a desktop vsync source (DXGI `WaitForVBlank` on the window's output), Smooth 30 FPS, frame buffer setting.
- WideMelon and its camera (port of `WideCamera.kt`).
- Done when: the same game, layout and filter look the same on PC and phone side by side, and pacing on a 60 Hz and a 120/144 Hz monitor is smooth.

### Phase 4: Lua mods (about 1.5k lines)

- Ports of `LuaScriptHeaderParser.kt` (exact rules from `MODS.md`), `LuaScriptFolderContent.kt` and the folder sync; script storage.
- Mods tab on the game details screen and a Mods entry in the pause menu with per-script options.
- Overlay compositing through `FrameQueue::updateOverlay` as on the phone; script requests (`melon.screen.*`, layout, tilt-shift, wide camera, fast forward) handled by the desktop.
- Done when: the Pokemon battle info mod runs on PC exactly as on the phone.

### Phase 5: RetroAchievements (about 2k lines)

- `rc_client` from the vendored rcheevos, with WinHTTP as its HTTP callback. It does login, sets, unlocks, leaderboards, hashing and offline retries itself, which replaces the Kotlin network layer instead of porting it. The phone keeps its current path.
- Login screen, toast popups for unlocks and leaderboards, achievement list in the pause menu, hardcore mode (blocks states, cheats, rewind, slow motion as on the phone), rich presence.
- Done when: a hardcore unlock on PC shows on the RetroAchievements site.

### Phase 6: the rest (about 3k lines)

- Rewind (shared `RewindManager`) with the rewind window UI.
- Cheats: import of `cheats.xml` (tinyxml2) and R4 `usrcheat.dat` (the core's `ARDatabaseDAT`), cheat list per game, enable/disable in the pause menu.
- DSi: NAND setup and the DSiWare manager (port of `MelonDSNandJNI.cpp` logic); DSi camera from a still image.
- Firmware settings (nickname, birthday, language, MAC), mic (none, blow, a capture device), GBA slot (ROM, Rumble Pak, memory expansion, Motion Pak through SDL sensors where the controller has a gyro), Wi-Fi through `Net_Slirp`.
- Perf overlay and the debug switches screen, reusing `cpp/debug`.
- Settings backup import from Android.

### Phase 7: release polish

- Zip release from `build.sh`, optional installer later.
- First-run setup (ROM folder, controller check), crash log, "about" screen.

### Not planned

Android home-screen shortcuts, the external display presentation and PerformanceHint have no PC equivalent. The in-app updater can come later if the PC build is shared.

## Testing

- Scripted runs stay the main check for renderer and core logic, hidden, never on the user's desktop.
- A UI test hook: `--ui-script F` feeds navigation events (up, down, confirm, back, wait N frames) and `--window-shot` captures the result, so menus can be checked by agents in a hidden window.
- Phase 0 and any shared-code change need an Android phone run before merging. Renderer results on the PC are logic checks only; Adreno and Mali still decide.
- Detached screens: hidden runs with `--detach --window-shot` check both windows; a visible check on two monitors (different refresh rates) for pacing, once, by you.
- Controller testing with real pads: at least an Xbox and a Switch Pro or DualSense controller (Nintendo vs Xbox face buttons).

## Open questions

- Is the PC build just for you, or will it be shared? That decides an installer, auto-update, and how much first-run help it needs.
- Exe and folder name: `melonade.exe` and `melonadeDS` assumed here.
- ScreenScraper (shelved): needs developer credentials from its forum and a public, free release first.
