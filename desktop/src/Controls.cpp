#include "Controls.h"

using namespace melonDS;

namespace Frontend
{

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
    case SDL_CONTROLLER_BUTTON_Y: return MenuAction::Clear;   // north = X
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
    case SDLK_DELETE: return MenuAction::Clear;
    // the DS's L and R keys, and Page Up / Page Down
    case SDLK_q:
    case SDLK_PAGEUP: return MenuAction::TabPrev;
    case SDLK_w:
    case SDLK_PAGEDOWN: return MenuAction::TabNext;
    default: return MenuAction::None;
    }
}

}
