// The screens, one file each
#pragma once

#include <memory>
#include <string>

#include "Library.h"
#include "Screen.h"

namespace Ui
{

// what the library opens by itself once its scan is done (--open library/details, library/settings): the first game's details or settings
enum class LibraryOpen { None, Details, Settings };
// the game library, the home screen when no game runs
std::unique_ptr<Screen> MakeLibraryScreen(LibraryOpen open = LibraryOpen::None);
// a game's details, with Play and its settings
std::unique_ptr<Screen> MakeGameDetailsScreen(const Frontend::LibraryEntry& entry);
// a game's own settings (RomConfig: its display name, console and GBA slot), kept in the library by its path
std::unique_ptr<Screen> MakeGameSettingsScreen(const Frontend::LibraryEntry& entry);
std::unique_ptr<Screen> MakePauseScreen();
// the savestate slots, on the Save tab or the Load tab
std::unique_ptr<Screen> MakeStatesScreen(bool save);
// the settings, on one of its tabs: video, display, emulation, audio, controls, library
std::unique_ptr<Screen> MakeSettingsScreen(const std::string& tab = "");
// the bindings of each device (input setup)
std::unique_ptr<Screen> MakeInputScreen();
// a text field and the on-screen keyboard, for tests (--open keyboard)
std::unique_ptr<Screen> MakeKeyboardTestScreen();

}
