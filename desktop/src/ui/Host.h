// What the menus ask of the rest of the frontend (main.cpp fills it in): the game, its savestates, the windows and the settings. All of it
// is called on the UI thread, from a screen's Draw; what starts or stops a game is queued and runs after the frame
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "SaveSlots.h"

namespace Frontend { class Settings; }

namespace Ui
{

struct Host
{
    Frontend::Settings* Settings = nullptr;
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

    // queued: the game stopped and the start screen shown / the app closed / the file dialog, then the ROM booted / a ROM booted
    std::function<void()> QuitToStart;
    std::function<void()> Quit;
    std::function<void()> OpenRom;
    std::function<void(const std::string& path)> BootRom;
    std::function<std::vector<std::string>()> RecentRoms;

    std::function<bool()> IsDetached;
    std::function<void(bool)> SetDetached;
    std::function<bool()> IsSwapped;
    std::function<void()> ToggleSwap;

    // the settings the placeholder settings screen changes, saved and applied (to the running game too)
    std::function<void(const std::string& renderer)> SetRenderer; // software, opengl, compute
    std::function<void(int scale)> SetScale;
    std::function<void(bool vsync)> SetVSync;
};

}
