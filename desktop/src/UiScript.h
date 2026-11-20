// --ui-script FILE: drives the menus from a text file, one step per line, for menu checks in hidden runs. Blank lines and # comments are
// skipped. The steps:
//   up, down, left, right, confirm, back, tab-next, tab-prev, menu   the menus' own input, as a controller sends it (menu = Start)
//   text STRING        typed into the text field being edited (the on-screen keyboard)
//   key NAME           a key pressed and released in the main window, through the same path as typing it (key escape, key return, key f9)
//   pad NAME           a controller button pressed and released, as SDL names it (pad start, pad guide, pad a, pad dpdown)
//   wait N             N passes of the frame loop (UI frames while a menu is up)
//   frame N            the game run to its frame N and held there (paused, as before frame N runs), then the script goes on
//   shot FILE.bmp      the window as drawn now, menus included (as --window-shot; detached: FILE_top.bmp and FILE_bottom.bmp)
//   screenshot FILE.bmp the game's last frame at the internal resolution (as --screenshot)
//   quit               the run ends here (it also ends after the last line)
// Each step takes two passes of the frame loop (a press and its release; a pass is a UI frame while a menu is up), so the menus react to
// one before the next. The script starts on the start screen, or with a game once its first frame is shown
#pragma once

#include <string>
#include <vector>

namespace Frontend
{

struct UiStep
{
    enum class Kind { Action, Text, Key, Pad, Wait, Frame, Shot, Screenshot, Quit } Type;
    std::string Arg;  // the action's name, the text, the key's or button's name, the file
    std::string Text; // the line as written
    unsigned Number = 0;
    int Line = 0;
};

// false (with the line's error on stderr) when a line isn't a step
bool LoadUiScript(const std::string& path, std::vector<UiStep>& steps);

}
