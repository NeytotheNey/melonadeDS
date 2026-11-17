# Plan: PC test runner for Lua mods

Plan only. Nothing is built, and the user chose not to build it yet (11-05). The battle info mod (`mods/pokemon-battle-info/`) is done, so the natural time to start is when adding more games means a lot of repeat testing.

## Goal

One command on Windows that runs a DS game with the real core and the real Lua engine, runs a mod through a battle or any other scene, and leaves screenshots and logs. No Android and no UI.

This is not a PC port of melonadeDS. The app's UI is Android-only Kotlin, and desktop melonDS already covers playing on PC.

Why it's worth having: testing a mod today means an Android emulator running the app's x86_64 build. Two of those pinned the PC at 100% CPU, instances can't easily be shared between sessions, and each test goes through the app's UI. The runner also gives a clean check on bugs like the emulator thread hangs seen on 2026-11-05: if one shows up with no Android layer, Android isn't the cause.

## What it won't cover

- OpenGL or Compute output, WideMelon, and performance: those stay on the phone.
- The app's screens: the Mods tab, the pause menu, layouts. Those stay on the Android emulator.
- Requests a script sends to the UI (`melon.screen.*`, `melon.emu.fast_forward`). They'd be accepted and do nothing.
- Images from a script's folder (`melon.image.load`) in the first version. It should fail with a clear error rather than draw nothing. No planned mod needs them yet.

## What already exists

**`tools/swi-harness` in the core repo** (on `melonade`) already does the hard part. It runs the core headless on Windows with MSYS2 MinGW and has:
- all the `Platform::` stubs (threads, files, save callbacks, audio and camera no-ops), in about the first 420 lines of its `main.cpp`
- ROM loading, DS and DSi mode, official or FreeBIOS, an x86-64 JIT switch, a fixed RTC
- `--frames`, `--tap KEY@FRAME`, `--shot-every` (BMP files)
- `--sav` (copied into the cart at boot, never written back) and `--state` (only read)

It builds the core with `MELONADE_SWI_LOG`, which the runner shouldn't have.

**The Lua engine is nearly free of Android already.** In `app/src/main/cpp/scripting/`, only `LuaScriptingJNI.cpp` uses JNI, and the runner leaves it out. What's left:
- `MelonLog.h` includes `android/log.h`. A small shim header in the runner's include path can send those calls to stderr, with no app code changed.
- `debug/DebugLog.h` pulls in `LogQueue.h` and `MelonLog.h`. `melon.log` only prints while the debug log is on, so the runner turns it on. Whether `DebugLog.cpp` builds as is on PC hasn't been checked.
- `LuaEngine::start` takes a `MelonEventMessenger`, and `LuaEngine.cpp` checks it for null before each use, so the runner can pass null.
- `LuaSavestateHost` is a small interface the runner implements, for `melon.state`.

**Not reusable:** the script folder walk and the header parser are Kotlin (`impl/LuaScriptFolderContent.kt`, `impl/LuaScriptHeaderParser.kt`), so the runner needs its own short C++ versions following the rules in [MODS.md](../MODS.md).

## How it would work

```
mod-runner <rom.nds> --bios free --frames 3000 --out logs/mod-runner/run1
           --scripts mods/pokemon-battle-info --enable battle_info.lua --option show_tags=false
           --sav <copy> --state <file> --console ds --shot-every 60
```

- **Location:** `tools/mod-runner/` in the app repo, because the Lua engine lives there. It builds the core from the submodule without `MELONADE_SWI_LOG`.
- **Frame loop:** the same order as `MelonInstance`: `runFrame` (`on_frame`), `nds->RunFrame()`, `endFrame` (`on_frame_end` and rasterizing the overlay).
- **Driving the game:** a second Lua script, using the normal `melon.input` calls, the same way the test probe did. That avoids a new input format, and `--tap` stays for simple cases. Don't add test-only calls to `melon.*`, so a script behaves the same as on a phone.
- **Output:** PNGs of both screens, doubled with nearest neighbor to match the 2× overlay, with the overlay blended on top. PNG rather than BMP so they can be viewed directly; an uncompressed PNG writer needs no library. Script logs go to the console and a log file.
- **Exit code:** non-zero if a script errored or hit a limit, so a test run can fail by itself. Scripts report anything finer through the log.
- **Files:** the save and the state are read only. Save writes are dropped, as the harness does now, so the user's originals can't be touched even when passed directly. Pass a copy anyway.
- **Repeatable runs:** with a fixed RTC and no threads, two runs should give the same frames. If so, screenshots can be compared against a saved good copy. That needs checking.

## Steps

Sizes are rough new or moved lines.

1. **Share the stubs (about 390 moved, core repo).** Move the `Platform::` stubs out of `swi-harness/main.cpp` into their own file. Check by running the harness and confirming its SWI log is unchanged.
2. **Build it (about 150).** CMake for the core, Lua (including `patched/lstrlib.c`, without `LUA_USE_POSIX` on Windows) and the scripting files without the JNI one, plus the Android shim. Check: it compiles and links.
3. **Run a script (about 300).** ROM, BIOS and console options, the frame loop, `LuaSavestateHost`, save and state loading. Check: a small script reads memory and logs across frames, and an error sets the exit code. The NAS `lua-test-scripts` set is a ready-made test.
4. **Folder and options (about 250).** Top-level scripts, modules with the same caps as the app, the header parser, and `--option` values with the same fallback rules. Check: the same scripts get the same values as in the app.
5. **Screenshots (about 100).** The overlay blend and the PNG writer. Check: the draw self-test script's picture against the same script on the Android emulator.
6. **Prove it on a real mod.** Run the battle info mod on White 2 through a battle from a save state, and compare with the same battle on the Android emulator. Record the frame rate of both. Add a short "how to use it" to the Testing section of MODS.md.

## Risks

- **DSi mode states.** The user's default console type is DSi, and White 2 is DSi-enhanced. A state has to be loaded in the mode it was made in, so `--console dsi` needs the DSi dumps. If that's a problem, make states in DS mode for testing. Whether the app adds anything around the core's state data also needs checking.
- **Drifting from the app.** The frame loop and the header and folder rules get written a second time. Keep the copies short and point at the originals in comments. If they start to drift, move `MelonInstance::runLuaFrame`'s logic somewhere both can share, which would be an app change.
- **Different CPU path.** The phone runs the ARM64 JIT, and the runner would default to the interpreter or the x86-64 JIT. Lua reads go through the bus either way, so mods shouldn't care, but a timing-sensitive bug might only show on one.
- **Clean-room rule.** The harness can load the user's BIOS dumps. The runner only passes them to the core, and nothing ever reads or disassembles them. See [FreeBIOS's upgrade plan](../melonDS-android-lib/freebios/UPGRADE_PLAN.md) on the core's `bios-upgrade` branch.

## Done means

The battle info mod runs on White 2 from one command, its screenshots match the Android emulator's for the same battle, the speed comparison is written down, and the swi-harness output hasn't changed. The app's own code stays untouched.
