// --ui-script FILE: drives the menus from a text file, one step per line, for menu checks in hidden runs. Blank lines and # comments are
// skipped. The steps:
//   up, down, left, right, confirm, back, tab-next, tab-prev, menu, clear   the menus' own input, as a controller sends it (menu = Start,
//                      clear = X, which clears a binding)
//   text STRING        typed into the text field being edited (the on-screen keyboard)
//   key NAME           a key pressed and released in the main window, through the same path as typing it (key escape, key return, key f9)
//   pad NAME           a controller button pressed and released, as SDL names it (pad start, pad guide, pad a, pad dpdown); with
//                      --virtual-pad, on that pad through SDL's own events
//   pad-down NAME, pad-up NAME   a controller button pressed (held over the next steps) or let go
//   key-down NAME, key-up NAME   the same for a key; key-repeat NAME: a repeated KEYDOWN, as a held key sends them
//   mouse-down X Y, mouse-up X Y   the pointer moved to X,Y of the main window (its points) and the left button pressed or let go there
//   mouse-move X Y     the pointer moved to X,Y of the main window, the left button still held after a mouse-down (a drag)
//   wheel N            the mouse wheel turned N notches (positive: away from you) where the pointer is
//   axis NAME VALUE    a stick or trigger of the --virtual-pad at VALUE (-32768 to 32767; triggers 0 to 32767): axis rightx 20000
//   wait N             N passes of the frame loop (UI frames while a menu is up)
//   sleep MS           MS milliseconds of real time (the game runs on meanwhile, unless it's held)
//   frame N            the game run to its frame N and held there (paused, as before frame N runs), then the script goes on
//   shot FILE.bmp      the window as drawn now, menus included (as --window-shot; detached: FILE_top.bmp and FILE_bottom.bmp)
//   screenshot FILE.bmp the game's last frame at the internal resolution (as --screenshot)
//   wait-scan          until the library's scan (and a game being unpacked) is done
//   wait-sync          until the metadata sync has nothing to do (while a game runs it waits for the game) and the art it published
//                      is on screen
//   quit               the run ends here (it also ends after the last line)
// Each step takes two passes of the frame loop (a press and its release; a pass is a UI frame while a menu is up), so the menus react to
// one before the next. The script starts on the library, or with a game once its first frame is shown
#pragma once

#include <string>
#include <vector>

namespace Frontend
{

struct UiStep
{
    enum class Kind { Action, Text, Key, Pad, PadDown, PadUp, KeyDown, KeyUp, KeyRepeat, MouseDown, MouseUp, MouseMove, Wheel, Axis, Wait, Sleep, Frame, Shot, Screenshot, WaitScan, WaitSync, Quit } Type;
    std::string Arg;  // the action's name, the text, the key's or button's name, the file
    std::string Text; // the line as written
    unsigned Number = 0;
    int Value = 0;    // axis
    int Line = 0;
};

// false (with the line's error on stderr) when a line isn't a step
bool LoadUiScript(const std::string& path, std::vector<UiStep>& steps);

}
