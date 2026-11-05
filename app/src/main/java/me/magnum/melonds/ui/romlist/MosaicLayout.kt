package me.magnum.melonds.ui.romlist

import kotlin.math.max

enum class MosaicTileSize(val columnSpan: Int, val rowSpan: Int) {
    SINGLE(1, 1),
    WIDE(2, 1),
    FEATURED(2, 2),
}

data class MosaicEntry<T>(val item: T, val size: MosaicTileSize)

/**
 * Where a tile goes inside its band.
 */
data class MosaicPlacement<T>(val item: T, val size: MosaicTileSize, val column: Int, val row: Int)

/**
 * Rows that no tile crosses the edge of. A lazy list can lay these out one by one, which a lazy grid can't do with tiles that span rows.
 */
data class MosaicBand<T>(val rowCount: Int, val tiles: List<MosaicPlacement<T>>)

/**
 * Which games get a wide tile, of those with title-screen art: about one in [MOSAIC_WIDE_ONE_IN], as a grid of nothing but wide tiles wouldn't be a mosaic.
 * This goes by the game itself so that a game keeps its size when the filter or the sort changes.
 *
 * @param identity What identifies the game, like its ROM hash
 */
fun isWideMosaicGame(identity: String): Boolean {
    return identity.hashCode().mod(MOSAIC_WIDE_ONE_IN) == 0
}

/**
 * Whether anything in the band sits beside [tile] on the end side, in one of the rows it covers.
 */
fun <T> hasTileAtEnd(band: MosaicBand<T>, tile: MosaicPlacement<T>): Boolean {
    val tileColumnEnd = tile.column + tile.size.columnSpan
    val tileRows = tile.row until tile.row + tile.size.rowSpan
    return band.tiles.any { other ->
        other.column >= tileColumnEnd && (other.row until other.row + other.size.rowSpan).any { it in tileRows }
    }
}

/**
 * Decides how big each tile is: the [maxFeatured] most recently played items lead as featured tiles, in play order. Everything else keeps its order, and
 * is wide only if it has title-screen art to fill the width with.
 */
fun <T> chooseMosaicSizes(
    items: List<T>,
    maxFeatured: Int = DEFAULT_MAX_FEATURED,
    lastPlayedAt: (T) -> Long?,
    hasTitleArt: (T) -> Boolean,
): List<MosaicEntry<T>> {
    val featuredIndices = items.indices
        .filter { lastPlayedAt(items[it]) != null }
        .sortedByDescending { lastPlayedAt(items[it]) }
        .take(maxFeatured)
    val featuredSet = featuredIndices.toSet()

    return buildList(items.size) {
        featuredIndices.forEach { add(MosaicEntry(items[it], MosaicTileSize.FEATURED)) }
        items.forEachIndexed { index, item ->
            if (index !in featuredSet) {
                add(MosaicEntry(item, if (hasTitleArt(item)) MosaicTileSize.WIDE else MosaicTileSize.SINGLE))
            }
        }
    }
}

/**
 * Packs [entries] onto a grid of [columns] columns the way CSS's dense grid does, then cuts the result into bands.
 *
 * The first free cell always gets the first tile that fits there, so a later single tile can back-fill the gap a wide one left at the end of a row. When
 * nothing that's left fits, the first of them shrinks to a single tile rather than leaving a hole, so free cells can only come after the last tile.
 */
fun <T> packMosaic(entries: List<MosaicEntry<T>>, columns: Int): List<MosaicBand<T>> {
    require(columns >= MosaicTileSize.FEATURED.columnSpan) { "A mosaic needs at least ${MosaicTileSize.FEATURED.columnSpan} columns" }

    val occupied = ArrayList<BooleanArray>()
    fun isFree(row: Int, column: Int) = row >= occupied.size || !occupied[row][column]
    fun fits(size: MosaicTileSize, row: Int, column: Int): Boolean {
        return column + size.columnSpan <= columns && (row until row + size.rowSpan).all { r ->
            (column until column + size.columnSpan).all { c -> isFree(r, c) }
        }
    }

    val isPlaced = BooleanArray(entries.size)
    val placements = ArrayList<MosaicPlacement<T>>(entries.size)
    var firstPending = 0
    var nextSingle = 0
    var row = 0
    var column = 0

    repeat(entries.size) {
        while (!isFree(row, column)) {
            column++
            if (column == columns) {
                column = 0
                row++
            }
        }
        while (isPlaced[firstPending]) {
            firstPending++
        }

        val index = if (column + 1 == columns || !isFree(row, column + 1)) {
            // Only a single tile can go here. A separate position keeps libraries of nothing but wide tiles from looking through all of them every row
            while (nextSingle < entries.size && (isPlaced[nextSingle] || entries[nextSingle].size != MosaicTileSize.SINGLE)) {
                nextSingle++
            }
            nextSingle.takeIf { it < entries.size }
        } else {
            (firstPending until entries.size).firstOrNull { !isPlaced[it] && fits(entries[it].size, row, column) }
        }

        val placedIndex = index ?: firstPending
        val size = if (index != null) entries[placedIndex].size else MosaicTileSize.SINGLE
        isPlaced[placedIndex] = true
        placements.add(MosaicPlacement(entries[placedIndex].item, size, column, row))

        for (r in row until row + size.rowSpan) {
            while (occupied.size <= r) {
                occupied.add(BooleanArray(columns))
            }
            for (c in column until column + size.columnSpan) {
                occupied[r][c] = true
            }
        }
    }

    return cutIntoBands(placements)
}

private fun <T> cutIntoBands(placements: List<MosaicPlacement<T>>): List<MosaicBand<T>> {
    val bands = ArrayList<MosaicBand<T>>()
    var tiles = ArrayList<MosaicPlacement<T>>()
    var firstRow = 0
    var lastRow = -1

    placements.forEach { placement ->
        if (placement.row > lastRow && tiles.isNotEmpty()) {
            bands.add(MosaicBand(lastRow - firstRow + 1, tiles))
            tiles = ArrayList()
            firstRow = placement.row
        }
        tiles.add(placement.copy(row = placement.row - firstRow))
        lastRow = max(lastRow, placement.row + placement.size.rowSpan - 1)
    }
    if (tiles.isNotEmpty()) {
        bands.add(MosaicBand(lastRow - firstRow + 1, tiles))
    }
    return bands
}

/**
 * How many columns fit in a width: 4 on a phone, as the rows have a fixed height and squares look best, and more when there's room for them.
 */
fun mosaicColumnsFor(availableWidthDp: Float, gutterDp: Float): Int {
    val fitting = ((availableWidthDp + gutterDp) / (MOSAIC_MIN_CELL_WIDTH_DP + gutterDp)).toInt()
    return max(fitting, DEFAULT_MOSAIC_COLUMNS)
}

const val DEFAULT_MOSAIC_COLUMNS = 4
const val DEFAULT_MAX_FEATURED = 2
private const val MOSAIC_WIDE_ONE_IN = 6
private const val MOSAIC_MIN_CELL_WIDTH_DP = 90f
