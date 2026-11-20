// A menu screen. The open screens are a stack in Ui::App: the top one is drawn and gets the input, Back pops it. Each screen is its own file
// in ui/ and is made through ui/Screens.h, so a new screen never touches the others
#pragma once

#include "imgui.h"

namespace Ui
{

class App;

class Screen
{
public:
    virtual ~Screen() = default;

    // the ImGui window's name, and the screen's name for --open
    virtual const char* Id() const = 0;
    // drawn over the game, which shows dimmed behind it; otherwise the screen fills the window
    virtual bool Overlay() const { return false; }
    // the screen's contents, inside its full-window ImGui window (cursor at the top left, no padding)
    virtual void Draw(App& app) = 0;
    // Back (B, Esc, Backspace): true when the screen handled it, otherwise it's closed
    virtual bool Back(App&) { return false; }
    // L / R (Q / W, Page Up / Page Down)
    virtual void Tab(App&, int /*direction*/) {}

    // App's: which item has the focus, to give it back when the screen is on top again, and whether it has to be set
    ImGuiID FocusId = 0;
    bool WantFocus = true;
};

}
