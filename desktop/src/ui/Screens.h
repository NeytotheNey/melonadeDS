// The screens, one file each
#pragma once

#include <memory>

#include "Screen.h"

namespace Ui
{

std::unique_ptr<Screen> MakeStartScreen();
std::unique_ptr<Screen> MakePauseScreen();
// the savestate slots, on the Save tab or the Load tab
std::unique_ptr<Screen> MakeStatesScreen(bool save);
std::unique_ptr<Screen> MakeSettingsScreen();
// a text field and the on-screen keyboard, for tests (--open keyboard)
std::unique_ptr<Screen> MakeKeyboardTestScreen();

}
