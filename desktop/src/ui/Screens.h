// The screens, one file each
#pragma once

#include <memory>
#include <string>

#include "Screen.h"

namespace Ui
{

std::unique_ptr<Screen> MakeStartScreen();
std::unique_ptr<Screen> MakePauseScreen();
// the savestate slots, on the Save tab or the Load tab
std::unique_ptr<Screen> MakeStatesScreen(bool save);
// the settings, on one of its tabs: video, display, emulation, audio, controls
std::unique_ptr<Screen> MakeSettingsScreen(const std::string& tab = "");
// the bindings of each device (input setup)
std::unique_ptr<Screen> MakeInputScreen();
// a text field and the on-screen keyboard, for tests (--open keyboard)
std::unique_ptr<Screen> MakeKeyboardTestScreen();

}
