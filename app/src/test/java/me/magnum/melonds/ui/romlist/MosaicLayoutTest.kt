package me.magnum.melonds.ui.romlist

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MosaicLayoutTest {

    private val games = listOf("a", "b", "c", "d", "e", "f", "g", "h")

    @Test
    fun noFeaturedItemsKeepsTheOrder() {
        val tiles = arrangeMosaic(games) { false }

        assertEquals(games, tiles.map { it.item })
        assertTrue(tiles.none { it.isFeatured })
    }

    @Test
    fun featuredItemsStartFullRows() {
        // "c" and "f" were played recently; each should head a row with a single filler after it on a 3 column grid
        val tiles = arrangeMosaic(games, columns = 3) { it == "c" || it == "f" }

        assertEquals(listOf("c", "a", "f", "b", "d", "e", "g", "h"), tiles.map { it.item })
        assertEquals(listOf(true, false, true, false, false, false, false, false), tiles.map { it.isFeatured })
        assertRowsAreFull(tiles, columns = 3)
    }

    @Test
    fun onlyTheFirstFeaturedItemsArePromoted() {
        val tiles = arrangeMosaic(games, columns = 3, maxFeatured = 2) { true }

        assertEquals(2, tiles.count { it.isFeatured })
        assertEquals(listOf("a", "c", "b", "d", "e", "f", "g", "h"), tiles.map { it.item })
        assertRowsAreFull(tiles, columns = 3)
    }

    @Test
    fun twoColumnsPutFeaturedItemsOnTheirOwnRow() {
        val tiles = arrangeMosaic(games, columns = 2) { it == "b" }

        assertEquals(listOf("b", "a", "c", "d", "e", "f", "g", "h"), tiles.map { it.item })
        assertRowsAreFull(tiles, columns = 2)
    }

    @Test
    fun fewerItemsThanFillersStillWorks() {
        val tiles = arrangeMosaic(listOf("a"), columns = 4) { true }

        assertEquals(listOf("a"), tiles.map { it.item })
        assertTrue(tiles.single().isFeatured)
    }

    @Test
    fun nothingIsLostOrDuplicated() {
        val tiles = arrangeMosaic(games, columns = 4, maxFeatured = 3) { it in setOf("b", "e", "h") }

        assertEquals(games.sorted(), tiles.map { it.item }.sorted())
    }

    /** Walks the tiles the way a lazy grid would place them and checks that no featured tile would wrap and leave a hole. */
    private fun assertRowsAreFull(tiles: List<MosaicTile<String>>, columns: Int) {
        var column = 0
        tiles.forEach { tile ->
            assertTrue("tile ${tile.item} would wrap at column $column", column + tile.columnSpan <= columns)
            column = (column + tile.columnSpan) % columns
        }
    }
}
