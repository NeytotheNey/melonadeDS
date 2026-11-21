// What the library screen and the game details screen draw alike: a game's art (its downloaded art when it's on disk, else the banner icon
// on a card tinted with its colors, as the app's tiles without downloaded art show it) and the facts' wording
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"
#include "Library.h"

namespace Ui
{

class App;

// the game's downloaded art (ArtFile) whole on its tinted card over [min, max] (rounded [rounding]), or the banner icon on it at the biggest
// whole multiple of 32 pixels that fits [fraction] of the card's width; the game's mark when it has no icon, a cloud for a file not read yet
// True when it was the downloaded art
bool GameArt(App& app, ImDrawList* draw, const Frontend::LibraryEntry& entry, ImVec2 min, ImVec2 max, float rounding, float fraction);
// the same with the icon centered in [iconMin, iconMax] instead of the card's middle (a mosaic tile, with its text under it), sized by
// that area
bool GameArtIn(App& app, ImDrawList* draw, const Frontend::LibraryEntry& entry, ImVec2 min, ImVec2 max, float rounding, ImVec2 iconMin,
    ImVec2 iconMax, float fraction);

// [file]'s image (downloaded art) over [min, max], rounded: whole and centered ([cover] false), or filling it, cut to fit; [topScreen]: a
// title screen of both screens shows its top one. False when it isn't loaded (yet), and then nothing is drawn
bool ArtImage(App& app, ImDrawList* draw, const Frontend::ArtRef& file, bool topScreen, ImVec2 min, ImVec2 max, float rounding, bool cover);
// a title screen's file (title.png), which shows its top screen when it fills a tile
bool IsTitleArt(const Frontend::ArtRef& file);
// a darkening along the bottom of [min, max] from [top] down, under text drawn over art
void Scrim(App& app, ImDrawList* draw, ImVec2 min, ImVec2 max, float top, float rounding);

// "Played 3 days ago", "Played yesterday", ... ("" when never); [now] in ms since 1970 (0: the clock's)
std::string PlayedText(int64_t lastPlayedMs, int64_t now = 0);
// the date and time it was played, "Nov 18, 2026, 9:41 PM" ("Never" when never)
std::string PlayedDate(int64_t lastPlayedMs);
// the region as a short tag ("USA", "EUR", "JPN", ...)
std::string RegionShort(const std::string& gameCode);
// "128 MB", "512 KB"
std::string SizeText(uint64_t bytes);
// "today", "yesterday", "3 days ago" for a time in ms since 1970
std::string DaysAgo(int64_t ms);

// Where the game's details came from, as the app's sheet credit tells them apart: cached from RetroAchievements (and when), not found
// there, its hash not in the hash library, no Web API key, not downloaded yet; empty for a file that wasn't read
std::string MetadataCredit(const Frontend::LibraryEntry& entry, bool hasKey);
// where its art came from (RetroAchievements or the libretro thumbnails, with its No-Intro name), or why there's none
std::string ArtCredit(const Frontend::LibraryEntry& entry, bool libretro);

}
