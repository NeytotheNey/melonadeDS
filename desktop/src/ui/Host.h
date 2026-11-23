// What the menus ask of the rest of the frontend (main.cpp fills it in): the game, its savestates, the windows and the settings. All of it
// is called on the UI thread, from a screen's Draw; what starts or stops a game is queued and runs after the frame
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "picojson/picojson.h"
#include "SaveSlots.h"
#include "layout/LayoutEditing.h"

namespace Frontend { class Settings; class Input; class Library; class MetadataSync; class Layouts; class Backgrounds; }

namespace Ui
{

struct Host
{
    Frontend::Settings* Settings = nullptr;
    // the input mapping: the menus' controller buttons by each pad's face layout, and the bindings the setup screen changes
    Frontend::Input* Input = nullptr;
    // the game library: its list, its scan and its folders (rom_search_dirs, changed through ChangeSetting)
    Frontend::Library* Library = nullptr;
    // the metadata and art sync (its status, the key test, Refresh metadata, Clear downloaded art)
    Frontend::MetadataSync* Metadata = nullptr;
    // the RetroAchievements Web API key saved (encrypted in settings.json) and used, or removed
    // (false when it couldn't be stored: it's used for the run only, and a toast says so)
    std::function<bool(const std::string& key)> SaveRaKey;
    std::function<void()> RemoveRaKey;
    // where the key in use came from: "" none, "saved", "command line" (--ra-web-api-key, for the run only)
    std::function<std::string()> RaKeySource;
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

    // the layouts to pick from (Settings > Display, a game's settings, the pause menu), the one the windows show, where it comes from now
    // ("run": --layout, "game": the game's own, "global": the settings'), and a pick while a game runs, stored where that one came from
    Frontend::Layouts* Layouts = nullptr;
    Frontend::Backgrounds* Backgrounds = nullptr;
    std::function<std::string()> LayoutInUse;
    std::function<std::string()> LayoutSource;
    std::function<void(const std::string& id)> PickLayoutInGame;
    // The layout editor's: the window it edits ([surface] 0 or 1 when detached, -1 for the focused one; together always the main window),
    // the layout shown there while it edits (none: the layout in use again), what the layout in use shows in a window as a layout to start
    // a new one from (none for a custom layout), and, after layouts.json changed (a save, a copy, a delete), the windows put on the layout
    // in use again: called right after Layouts::Put or Remove, before anything asks for the layout in use (the list was made again)
    std::function<Frontend::LayoutEditTarget(int surface)> LayoutEditTarget;
    std::function<void(const std::optional<Frontend::LayoutPreview>&)> SetLayoutPreview;
    std::function<std::optional<Frontend::UILayout>(int surface)> ShownAsLayout;
    std::function<void()> LayoutsChanged;
    // an image or SVG for a background through the Windows file dialog, queued to after the frame as OpenRom: [done] gets the file (UTF-8),
    // or nothing when it was cancelled (at once in a hidden run)
    std::function<void(std::function<void(const std::optional<std::string>& file)> done)> PickBackgroundFile;

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
