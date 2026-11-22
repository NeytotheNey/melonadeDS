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
    // an overlay that leaves the game as it is, not dimmed (the layout editor)
    virtual bool Dims() const { return true; }
    // Start or Guide go to the screen's MenuButton even while a game runs, instead of back to the game
    virtual bool OwnsMenuButton() const { return false; }
    // the screen's contents, inside its full-window ImGui window (cursor at the top left, no padding)
    virtual void Draw(App& app) = 0;
    // Back (B, Esc, Backspace): true when the screen handled it, otherwise it's closed
    virtual bool Back(App&) { return false; }
    // L / R (Q / W, Page Up / Page Down)
    virtual void Tab(App&, int /*direction*/) {}
    // X (Delete): clears what has the focus, where a screen has something to clear
    virtual void Clear(App&) {}
    // the Delete key, which is Clear unless a screen gives it a meaning of its own (the layout editor: remove the selected screen)
    virtual void DeleteKey(App& app) { Clear(app); }
    // Start or Guide while no game runs (with one, they go back to it): the library opens its menu
    virtual void MenuButton(App&) {}

    // App's: which item has the focus, to give it back when the screen is on top again, and whether it has to be set
    ImGuiID FocusId = 0;
    // the item focused that way, held there for a few more frames: ImGui's own first-item focus of a window that just appeared comes a
    // frame later and would take it back
    ImGuiID RefocusId = 0;
    int RefocusFrames = 0;
    bool WantFocus = true;
};

}
