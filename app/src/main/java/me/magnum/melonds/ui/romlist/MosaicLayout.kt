package me.magnum.melonds.ui.romlist

/**
 * One cell of the mosaic view. A featured tile spans two columns and shows a wide 2:1 image, a normal tile is a single square.
 */
data class MosaicTile<T>(
    val item: T,
    val isFeatured: Boolean,
) {
    val columnSpan: Int
        get() = if (isFeatured) FEATURED_SPAN else 1

    companion object {
        const val FEATURED_SPAN = 2
    }
}

/**
 * Orders [items] so that a grid of [columns] columns with featured tiles spanning two columns packs without holes.
 *
 * Up to [maxFeatured] items that satisfy [isFeatured] move to the front, in their original order. Each of them starts a row, and the row is
 * completed with the next (columns - 2) ordinary items, so every row is full. The remaining items follow as single tiles. A lazy grid
 * cannot pack densely on its own, so this is done up front.
 */
fun <T> arrangeMosaic(
    items: List<T>,
    columns: Int = DEFAULT_MOSAIC_COLUMNS,
    maxFeatured: Int = DEFAULT_MAX_FEATURED,
    isFeatured: (T) -> Boolean,
): List<MosaicTile<T>> {
    require(columns >= MosaicTile.FEATURED_SPAN) { "A mosaic needs at least ${MosaicTile.FEATURED_SPAN} columns" }

    val featuredIndices = items.indices.filter { isFeatured(items[it]) }.take(maxFeatured).toSet()
    val ordinary = items.filterIndexed { index, _ -> index !in featuredIndices }.iterator()
    val fillersPerFeaturedRow = columns - MosaicTile.FEATURED_SPAN

    return buildList(items.size) {
        featuredIndices.forEach { index ->
            add(MosaicTile(items[index], isFeatured = true))
            repeat(fillersPerFeaturedRow) {
                if (ordinary.hasNext()) add(MosaicTile(ordinary.next(), isFeatured = false))
            }
        }
        ordinary.forEachRemaining { add(MosaicTile(it, isFeatured = false)) }
    }
}

const val DEFAULT_MOSAIC_COLUMNS = 3
const val DEFAULT_MAX_FEATURED = 2
