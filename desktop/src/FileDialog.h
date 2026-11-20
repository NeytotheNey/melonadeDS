// The Windows file dialog (IFileOpenDialog) for Open ROM. UI thread; it's modal and blocks until it closes
#pragma once

#include <optional>
#include <string>

#include <SDL.h>

namespace Frontend
{

// the ROM the user picked (UTF-8), starting in [startDir] when it's given; nothing when the dialog was cancelled or couldn't open. Only
// .nds and .dsi are offered: the shared ROM loader reads plain ROM files, not zip or 7z archives
std::optional<std::string> OpenRomDialog(SDL_Window* parent, const std::string& startDir);

}
