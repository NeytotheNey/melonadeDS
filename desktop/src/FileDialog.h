// The Windows file dialog (IFileOpenDialog) for Open ROM, the folder settings and a game's GBA ROM. UI thread; it's modal and blocks until it closes
#pragma once

#include <optional>
#include <string>

#include <SDL.h>

namespace Frontend
{

// the ROM the user picked (UTF-8), starting in [startDir] when it's given; nothing when the dialog was cancelled or couldn't open. .nds,
// .dsi, and .zip and .7z archives with one in them (unpacked before they boot, as the library does)
std::optional<std::string> OpenRomDialog(SDL_Window* parent, const std::string& startDir);
// a folder the user picked (the BIOS folders), the same way
std::optional<std::string> OpenFolderDialog(SDL_Window* parent, const std::string& startDir);
// a GBA ROM (.gba), for a game's GBA slot
std::optional<std::string> OpenGbaRomDialog(SDL_Window* parent, const std::string& startDir);

}
