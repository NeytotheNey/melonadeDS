// The fixed controls, in one place: which keyboard key and which controller button is which DS key in game, and what each one does in the
// menus. Step C's input mapping replaces these tables; nothing else in the frontend knows the layout. The controller layout is Nintendo's:
// the east face button is A, the south one B, in the game and in the menus (A confirms, B goes back)
#pragma once

#include <SDL.h>

#include "types.h"

namespace Frontend
{

// the DS key bit (MelonInstance::pressKey's numbering) for a keyboard key in game, or 0
melonDS::u32 GameKeyBit(SDL_Keycode key);
// the DS key bit for a controller button in game, or 0
melonDS::u32 GameButtonBit(int button);

// what a key or a button does in the menus
enum class MenuAction { None, Up, Down, Left, Right, Confirm, Back, TabPrev, TabNext, Menu };

// a controller button in the menus; Menu (Start, Guide) also opens the pause menu in game
MenuAction MenuButtonAction(int button);
// a key in the menus, for the keys the frontend handles itself (Esc and Backspace go back, Esc opens the pause menu in game); arrows,
// Enter and Space go to the menus' own keyboard navigation and aren't listed
MenuAction MenuKeyAction(SDL_Keycode key);

// the left stick's dead zone, for the D-pad it stands in for in game and the focus it moves in the menus
constexpr int kStickDeadZone = 16000;

}
