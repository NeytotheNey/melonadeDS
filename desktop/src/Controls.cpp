#include "Controls.h"

using namespace melonDS;

namespace Frontend
{

u32 GameKeyBit(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_x: return 1 << 0;      // A
    case SDLK_z: return 1 << 1;      // B
    case SDLK_RSHIFT: return 1 << 2; // Select
    case SDLK_BACKSPACE: return 1 << 2;
    case SDLK_RETURN: return 1 << 3; // Start
    case SDLK_RIGHT: return 1 << 4;
    case SDLK_LEFT: return 1 << 5;
    case SDLK_UP: return 1 << 6;
    case SDLK_DOWN: return 1 << 7;
    case SDLK_w: return 1 << 8;      // R
    case SDLK_q: return 1 << 9;      // L
    case SDLK_s: return 1 << 10;     // X
    case SDLK_a: return 1 << 11;     // Y
    default: return 0;
    }
}

u32 GameButtonBit(int button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_B: return 1 << 0;  // east = A
    case SDL_CONTROLLER_BUTTON_A: return 1 << 1;  // south = B
    case SDL_CONTROLLER_BUTTON_BACK: return 1 << 2;
    // Start and Guide open the pause menu, so the DS's Start is on the right stick's click until the input mapping comes
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return 1 << 3;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return 1 << 4;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return 1 << 5;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return 1 << 6;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return 1 << 7;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return 1 << 8;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return 1 << 9;
    case SDL_CONTROLLER_BUTTON_Y: return 1 << 10; // north = X
    case SDL_CONTROLLER_BUTTON_X: return 1 << 11; // west = Y
    default: return 0;
    }
}

MenuAction MenuButtonAction(int button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return MenuAction::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return MenuAction::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return MenuAction::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return MenuAction::Right;
    case SDL_CONTROLLER_BUTTON_B: return MenuAction::Confirm; // east = A
    case SDL_CONTROLLER_BUTTON_A: return MenuAction::Back;    // south = B
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return MenuAction::TabPrev;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return MenuAction::TabNext;
    case SDL_CONTROLLER_BUTTON_START:
    case SDL_CONTROLLER_BUTTON_GUIDE: return MenuAction::Menu;
    default: return MenuAction::None;
    }
}

MenuAction MenuKeyAction(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_ESCAPE: return MenuAction::Back;
    case SDLK_BACKSPACE: return MenuAction::Back;
    // the DS's L and R keys, and Page Up / Page Down
    case SDLK_q:
    case SDLK_PAGEUP: return MenuAction::TabPrev;
    case SDLK_w:
    case SDLK_PAGEDOWN: return MenuAction::TabNext;
    default: return MenuAction::None;
    }
}

}
