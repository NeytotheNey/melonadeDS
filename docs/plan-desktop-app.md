# Plan: melonadeDS for Windows (a real desktop app)

## Status (2026-11-18)

Plan only, nothing built. Decided: Dear ImGui menus on the existing SDL2 + GLES 3.2 window, full controller navigation, and every feature the Android app has, built in phases. The current `desktop/` test frontend becomes this app; its scripted-run options keep working.

## Goal

A Windows `.exe` you can double-click and play from the couch: a game library, a pause menu, settings, save states, controller mapping, and then everything else the app has (layouts and filters, Lua mods, RetroAchievements, rewind, cheats, DSi). Every screen works with a controller alone, with keyboard and mouse as well.

## Where we start

- `desktop/` (about 2.2k lines) runs the core with soft, OpenGL and Compute on one thread, 4 fixed layouts, hardcoded keys, SDL controller with a fixed Nintendo layout, raw savestates, `SDL_QueueAudio`, and a 59.83 Hz timer. It compiles none of the app's C++.
- The app's own C++ in `app/src/main/cpp/` (about 13k lines) is mostly portable. About 9k lines can be shared as is: `MelonInstance`, `Configuration.h`, `EmulatorArgsBuilder`, `SDCardArgsBuilder`, `CartLoader`, `DSiSupport`, `SaveManager`, `RewindManager`, `renderer/FramePacing`, `FrameQueue`, `ScreenshotRenderer`, `retroachievements/`, `scripting/` (minus `LuaScriptingJNI.cpp`), `RomIconBuilder`, `debug/`.
- Android ties in that code are few: EGL sync fences in `MelonInstance.cpp` and `FrameQueue.cpp`, the `MelonDSAndroid::` globals (`internalFilesDir`, `eventMessenger`), and `pid_t`/`gettid`.
- Everything else the user sees is Kotlin (about 63k lines) and gets rewritten: presentation (`DSRenderer.kt`, `VideoFilterPipeline.kt`, shaders in `ShaderProgramSource.kt`), layouts, the library, settings, input mapping, cheats storage, the RetroAchievements network layer, the Lua folder walk and header parser, `WideCamera.kt`.

## Architecture

**One exe, two modes.** `melonade.exe` with no arguments opens the library. With a ROM it boots straight into the game. The test options (`--frames`, `--press`, `--set`, `--screenshot`, `--hidden`, `--unthrottled`, ...) still work and skip the UI, so scripted renderer checks don't change. The app links as a GUI program (no console window); test mode attaches to the parent console for output.

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
- An app icon and version info in a `.rc` resource; `build.sh` makes a ready-to-zip folder.
- Done when: you can play a game start to finish with only a controller, never touching the keyboard.

### Phase 2: game library (about 3k lines)

- ROM folders in settings, scanning (`.nds`, `.dsi`, zip and 7z), header parse, the RA hash (port of `RomProcessor.kt`) and banner icons (`RomIconBuilder`).
- List, Grid and Mosaic views with sort, filter and search, following `docs/plan-launcher.md`; game details sheet with Play and per-game settings (`RomConfig`: console type, GBA slot, widescreen, mic, mods).
- RetroAchievements metadata and art through the Web API key, with the app's caching and back-off rules (`RomMetadataSync*`), art cached on disk.
- Done when: the library opens with art on first launch after a sync, and controller focus works across all three views.

### Phase 3: presentation parity (about 4k lines)

- Port `DSRenderer` and `VideoFilterPipeline` to C++ with the shaders lifted from `ShaderProgramSource.kt` verbatim (all 12 filters, deband, tilt-shift, shadows, backgrounds, the Lua overlay pass). A small script checks the desktop copies still match the Kotlin strings, so they can't drift silently.
- Layouts: the `UILayout`/`LayoutConfiguration` model and JSON format from `impl/dtos/layout`, the default layouts, and an ImGui layout editor (drag screens with mouse or controller, snap, resize). Same JSON as the app, so layouts can be copied over.
- Backgrounds with nanosvg for the built-in SVGs.
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
- Controller testing with real pads: at least an Xbox and a Switch Pro or DualSense controller (Nintendo vs Xbox face buttons).

## Open questions

- Is the PC build just for you, or will it be shared? That decides an installer, auto-update, and how much first-run help it needs.
- Exe and folder name: `melonade.exe` and `melonadeDS` assumed here.
- Shaders: copy them now (with the drift check) and move both platforms to shared `.glsl` files later, or move them in phase 3 at the cost of an Android change and a phone run.
