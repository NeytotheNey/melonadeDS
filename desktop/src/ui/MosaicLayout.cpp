#include "MosaicLayout.h"

#include <algorithm>

namespace Ui
{

namespace
{

constexpr int kWideOneIn = 6;
constexpr int kMinColumns = 4;

}

bool IsWideMosaicGame(const std::string& identity)
{
    // Java's String.hashCode over UTF-16 units; the hashes are ASCII hex, where a byte is a unit
    int32_t h = 0;
    for (unsigned char c : identity)
        h = (int32_t)((uint32_t)h * 31u + c);
    int m = h % kWideOneIn;
    return (m < 0 ? m + kWideOneIn : m) == 0;
}

std::vector<MosaicEntry> ChooseMosaicSizes(int count, const std::function<int64_t(int)>& lastPlayed, const std::function<bool(int)>& hasTitleArt,
    int maxFeatured)
{
    std::vector<int> played;
    for (int i = 0; i < count; i++)
    {
        if (lastPlayed(i) > 0)
            played.push_back(i);
    }
    std::stable_sort(played.begin(), played.end(), [&](int a, int b) { return lastPlayed(a) > lastPlayed(b); });
    if ((int)played.size() > maxFeatured)
        played.resize(maxFeatured);
    std::vector<bool> featured(count, false);
    std::vector<MosaicEntry> entries;
    entries.reserve(count);
    for (int i : played)
    {
        featured[i] = true;
        entries.push_back({i, MosaicSize::Featured});
    }
    for (int i = 0; i < count; i++)
    {
        if (!featured[i])
            entries.push_back({i, hasTitleArt(i) ? MosaicSize::Wide : MosaicSize::Single});
    }
    return entries;
}

std::vector<MosaicBand> PackMosaic(const std::vector<MosaicEntry>& entries, int columns)
{
    columns = std::max(columns, 2);
    std::vector<std::vector<bool>> occupied;
    auto isFree = [&](int row, int column) { return row >= (int)occupied.size() || !occupied[row][column]; };
    auto fits = [&](MosaicSize size, int row, int column)
    {
        if (column + ColumnSpan(size) > columns)
            return false;
        for (int r = row; r < row + RowSpan(size); r++)
        {
            for (int c = column; c < column + ColumnSpan(size); c++)
            {
                if (!isFree(r, c))
                    return false;
            }
        }
        return true;
    };

    size_t n = entries.size();
    std::vector<bool> placed(n, false);
    struct Placed { int Item; MosaicSize Size; int Column, Row; };
    std::vector<Placed> placements;
    placements.reserve(n);
    size_t firstPending = 0, nextSingle = 0;
    int row = 0, column = 0;
    for (size_t step = 0; step < n; step++)
    {
        while (!isFree(row, column))
        {
            if (++column == columns)
            {
                column = 0;
                row++;
            }
        }
        while (placed[firstPending])
            firstPending++;

        long index = -1;
        if (column + 1 == columns || !isFree(row, column + 1))
        {
            // only a single tile goes here; a position of its own keeps a library of wide tiles from looking through all of them each row
            while (nextSingle < n && (placed[nextSingle] || entries[nextSingle].Size != MosaicSize::Single))
                nextSingle++;
            if (nextSingle < n)
                index = (long)nextSingle;
        }
        else
        {
            for (size_t i = firstPending; i < n; i++)
            {
                if (!placed[i] && fits(entries[i].Size, row, column))
                {
                    index = (long)i;
                    break;
                }
            }
        }
        size_t at = index >= 0 ? (size_t)index : firstPending;
        MosaicSize size = index >= 0 ? entries[at].Size : MosaicSize::Single;
        placed[at] = true;
        placements.push_back({entries[at].Item, size, column, row});
        for (int r = row; r < row + RowSpan(size); r++)
        {
            while ((int)occupied.size() <= r)
                occupied.emplace_back(columns, false);
            for (int c = column; c < column + ColumnSpan(size); c++)
                occupied[r][c] = true;
        }
    }

    // cut into bands: a new one starts with a tile below every row the tiles so far cover
    std::vector<MosaicBand> bands;
    int firstRow = 0, lastRow = -1;
    MosaicBand band {0, 0, {}};
    for (const Placed& p : placements)
    {
        if (p.Row > lastRow && !band.Tiles.empty())
        {
            band.RowCount = lastRow - firstRow + 1;
            bands.push_back(std::move(band));
            band = MosaicBand {p.Row, 0, {}};
            firstRow = p.Row;
        }
        band.Tiles.push_back({p.Item, p.Size, p.Column, p.Row - firstRow});
        lastRow = std::max(lastRow, p.Row + RowSpan(p.Size) - 1);
    }
    if (!band.Tiles.empty())
    {
        band.RowCount = lastRow - firstRow + 1;
        bands.push_back(std::move(band));
    }
    return bands;
}

bool HasTileAtEnd(const MosaicBand& band, const MosaicPlacement& tile)
{
    int end = tile.Column + ColumnSpan(tile.Size);
    for (const MosaicPlacement& other : band.Tiles)
    {
        if (other.Column < end)
            continue;
        if (other.Row < tile.Row + RowSpan(tile.Size) && tile.Row < other.Row + RowSpan(other.Size))
            return true;
    }
    return false;
}

int MosaicColumnsFor(float width, float gutter, float minCell)
{
    int fitting = (int)((width + gutter) / (minCell + gutter));
    return std::max(fitting, kMinColumns);
}

}
