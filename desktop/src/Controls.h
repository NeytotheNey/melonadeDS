// The menus' fixed controls: what each key and each controller button does in the menus. In game, the input mapping (Input.h) decides.
// The controller table is for a pad with Nintendo's face layout (east = A chooses, south = B goes back, north = X clears);
// Input::MenuButton turns it around for a pad with the positional one (south = A, east = B, west = X)
#pragma once

#include <SDL.h>

#include "types.h"

namespace Frontend
{

// what a key or a button does in the menus
enum class MenuAction { None, Up, Down, Left, Right, Confirm, Back, TabPrev, TabNext, Menu, Clear };

// a controller button in the menus (Nintendo's face layout); Menu (Start, Guide) goes back to the game
MenuAction MenuButtonAction(int button);
// a key in the menus, for the keys the frontend handles itself (Esc and Backspace go back, Delete clears); arrows, Enter and Space go to
// the menus' own keyboard navigation and aren't listed
MenuAction MenuKeyAction(SDL_Keycode key);

// the left stick's dead zone, for the focus it moves in the menus
constexpr int kStickDeadZone = 16000;

}
