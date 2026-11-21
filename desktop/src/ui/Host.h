// What the menus ask of the rest of the frontend (main.cpp fills it in): the game, its savestates, the windows and the settings. All of it
// is called on the UI thread, from a screen's Draw; what starts or stops a game is queued and runs after the frame
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "picojson/picojson.h"
#include "SaveSlots.h"

namespace Frontend { class Settings; class Input; class Library; }

namespace Ui
{

struct Host
{
    Frontend::Settings* Settings = nullptr;
    // the input mapping: the menus' controller buttons by each pad's face layout, and the bindings the setup screen changes
    Frontend::Input* Input = nullptr;
    // the game library: its list, its scan and its folders (rom_search_dirs, changed through ChangeSetting)
    Frontend::Library* Library = nullptr;
    // a hidden (scripted) run: no file dialog
    bool Hidden = false;

    std::function<bool()> GameRunning;
    // the game's name (its ROM's file name without the extension) and "Compute 4x"
    std::function<std::string()> GameTitle;
    std::function<std::string()> RendererStatus;

    std::function<void()> Reset;
    // the savestate slots of the game; a save or load runs on the emulator thread and [done] comes back on the UI thread
    std::function<std::vector<Frontend::SaveSlots::Slot>()> ListSlots;
    std::function<std::string()> SlotFileNames;
    std::function<void(int slot, std::function<void(bool ok)> done)> SaveSlot;
    std::function<void(int slot, std::function<void(bool ok)> done)> LoadSlot;
    // the slot's state and screenshot deleted (to the recycle bin)
    std::function<bool(int slot)> DeleteSlot;

    // queued: the game stopped and the library shown / the app closed / the file dialog, then the ROM booted / a ROM booted (a zip or 7z
    // unpacked first, on the library's worker; the library's Progress says so meanwhile), and its last-played time set. BootRom is
    // false, with a toast, while another game is still starting (being unpacked)
    std::function<void()> QuitToStart;
    std::function<void()> Quit;
    std::function<void()> OpenRom;
    std::function<bool(const std::string& path)> BootRom;

    std::function<bool()> IsDetached;
    std::function<void(bool)> SetDetached;
    std::function<bool()> IsSwapped;
    std::function<void()> ToggleSwap;

    // a setting the settings screens changed: stored, and applied at once where the running game allows it. False when it waits for the
    // next game that starts (JIT, the console, the BIOS)
    std::function<bool(const std::string& key, const picojson::value& value)> ChangeSetting;
    // the Windows folder picker (UTF-8), starting in [start]; nothing when it was cancelled, or in a hidden run
    std::function<std::optional<std::string>(const std::string& start)> PickFolder;
    // the file dialog for a GBA ROM (a game's GBA slot), the same way
    std::function<std::optional<std::string>(const std::string& start)> PickGbaRom;
};

}
