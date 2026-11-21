// The screens, one file each
#pragma once

#include <memory>
#include <string>

#include "Library.h"
#include "Screen.h"

namespace Ui
{

// the game library, the home screen when no game runs; [openDetails]: the first game's details once the scan is done (--open
// library/details)
std::unique_ptr<Screen> MakeLibraryScreen(bool openDetails = false);
// a game's details, with Play
std::unique_ptr<Screen> MakeGameDetailsScreen(const Frontend::LibraryEntry& entry);
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
