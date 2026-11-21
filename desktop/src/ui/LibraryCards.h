// What the library screen and the game details screen draw alike: a game's art (the banner icon on a card tinted with its colors, as
// the app's tiles without downloaded art show it) and the facts' wording
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"
#include "Library.h"

namespace Ui
{

class App;

// the banner icon on its tinted card over [min, max] (rounded [rounding]), at the biggest whole multiple of 32 pixels that fits
// [fraction] of the card's width; the game's mark when it has no icon, a cloud for a file not read yet
void GameArt(App& app, ImDrawList* draw, const Frontend::LibraryEntry& entry, ImVec2 min, ImVec2 max, float rounding, float fraction);

// "Played 3 days ago", "Played yesterday", ... ("" when never); [now] in ms since 1970 (0: the clock's)
std::string PlayedText(int64_t lastPlayedMs, int64_t now = 0);
// the date and time it was played, "Nov 18, 2026, 9:41 PM" ("Never" when never)
std::string PlayedDate(int64_t lastPlayedMs);
// the region as a short tag ("USA", "EUR", "JPN", ...)
std::string RegionShort(const std::string& gameCode);
// "128 MB", "512 KB"
std::string SizeText(uint64_t bytes);

}
