// The library's Mosaic view packing, a port of the app's ui/romlist/MosaicLayout.kt (docs/plan-launcher.md section 1): the two most
// recently played games lead as 2x2 featured tiles whatever the sort, games with title-screen art are 2x1 (about one in six of them, by
// the ROM hash, so a filter or sort doesn't reshuffle them), the rest single cells, packed like CSS's dense grid and cut into bands that
// no tile crosses. Pure logic on indexes into the caller's list
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Ui
{

enum class MosaicSize { Single, Wide, Featured };

inline int ColumnSpan(MosaicSize s) { return s == MosaicSize::Single ? 1 : 2; }
inline int RowSpan(MosaicSize s) { return s == MosaicSize::Featured ? 2 : 1; }

struct MosaicEntry
{
    int Item;
    MosaicSize Size;
};

// where a tile goes in its band
struct MosaicPlacement
{
    int Item;
    MosaicSize Size;
    int Column, Row; // Row: inside the band
};

struct MosaicBand
{
    int FirstRow;  // the band's first row in the whole mosaic
    int RowCount;
    std::vector<MosaicPlacement> Tiles;
};

// whether a game with title-screen art gets a wide tile: [identity] (the ROM's RetroAchievements hash) hashed as Kotlin's
// String.hashCode, mod 6, as the app's isWideMosaicGame, so the same games are wide on the phone and the PC
bool IsWideMosaicGame(const std::string& identity);

// the sizes: the [maxFeatured] most recently played items first, in play order (newest first), then the rest in their order, wide when
// they have title art. [lastPlayed] 0 means never
std::vector<MosaicEntry> ChooseMosaicSizes(int count, const std::function<int64_t(int)>& lastPlayed, const std::function<bool(int)>& hasTitleArt,
    int maxFeatured = 2);

// [entries] on a grid of [columns] (at least 2) columns: the first free cell always gets the first tile that fits there, so a later
// single tile back-fills the gap a wide one left, and when nothing left fits, the first of them shrinks to a single cell rather than
// leave a hole
std::vector<MosaicBand> PackMosaic(const std::vector<MosaicEntry>& entries, int columns);

// whether anything in [band] sits beside [tile] on its right, in one of the rows it covers
bool HasTileAtEnd(const MosaicBand& band, const MosaicPlacement& tile);

// how many columns of at least [minCell] fit in [width] with [gutter] between them, at least 4 (the app's mosaicColumnsFor)
int MosaicColumnsFor(float width, float gutter, float minCell);

}
