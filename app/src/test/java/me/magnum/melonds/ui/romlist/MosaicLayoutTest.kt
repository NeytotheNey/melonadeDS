package me.magnum.melonds.ui.romlist

import me.magnum.melonds.ui.romlist.MosaicTileSize.FEATURED
import me.magnum.melonds.ui.romlist.MosaicTileSize.SINGLE
import me.magnum.melonds.ui.romlist.MosaicTileSize.WIDE
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.MessageDigest

class MosaicLayoutTest {

    private fun single(item: String) = MosaicEntry(item, SINGLE)
    private fun wide(item: String) = MosaicEntry(item, WIDE)
    private fun featured(item: String) = MosaicEntry(item, FEATURED)

    /** A tile's place on the whole grid, for tests that don't care where one band ends. */
    private data class Cell(val item: String, val size: MosaicTileSize, val column: Int, val row: Int)

    private fun pack(columns: Int, vararg entries: MosaicEntry<String>): List<Cell> {
        val bands = packMosaic(entries.toList(), columns)
        assertHolesOnlyAfterTheLastTile(bands, columns)
        assertEquals("every tile must be placed once", entries.map { it.item }.sorted(), bands.flatMap { it.tiles }.map { it.item }.sorted())

        var firstRow = 0
        return bands.flatMap { band ->
            band.tiles.map { Cell(it.item, it.size, it.column, firstRow + it.row) }.also { firstRow += band.rowCount }
        }
    }

    /**
     * Fills in the whole grid, checking that nothing overlaps or leaves the grid and that a tile can't hang out of its band, and that going through it in
     * reading order no empty cell comes before the start of the last tile.
     */
    private fun assertHolesOnlyAfterTheLastTile(bands: List<MosaicBand<String>>, columns: Int) {
        val filled = HashSet<Pair<Int, Int>>()
        var lastTileStart = -1 to -1
        var firstRow = 0
        bands.forEach { band ->
            band.tiles.forEach { tile ->
                assertTrue("${tile.item} leaves the grid", tile.column + tile.size.columnSpan <= columns)
                assertTrue("${tile.item} hangs out of its band", tile.row + tile.size.rowSpan <= band.rowCount)
                for (row in tile.row until tile.row + tile.size.rowSpan) {
                    for (column in tile.column until tile.column + tile.size.columnSpan) {
                        assertTrue("${tile.item} overlaps another tile", filled.add(firstRow + row to column))
                    }
                }
                lastTileStart = firstRow + tile.row to tile.column
            }
            firstRow += band.rowCount
        }

        for (row in 0..lastTileStart.first) {
            for (column in 0 until columns) {
                if (row < lastTileStart.first || column < lastTileStart.second) {
                    assertTrue("hole at row $row, column $column", row to column in filled)
                }
            }
        }
    }

    private fun sizes(games: List<String>, played: Map<String, Long> = emptyMap(), titleArt: Set<String> = emptySet(), maxFeatured: Int = DEFAULT_MAX_FEATURED): List<MosaicEntry<String>> {
        return chooseMosaicSizes(games, maxFeatured, lastPlayedAt = { played[it] }, hasTitleArt = { it in titleArt })
    }

    @Test
    fun theMostRecentlyPlayedGamesLeadAsFeaturedTiles() {
        val entries = sizes(listOf("a", "b", "c", "d"), played = mapOf("b" to 100, "c" to 300, "d" to 200))

        assertEquals(listOf(featured("c"), featured("d"), single("a"), single("b")), entries)
    }

    @Test
    fun onlyGamesWithTitleArtBecomeWide() {
        val entries = sizes(listOf("a", "b", "c"), titleArt = setOf("b"))

        assertEquals(listOf(single("a"), wide("b"), single("c")), entries)
    }

    @Test
    fun aFeaturedGameWithTitleArtStaysFeatured() {
        val entries = sizes(listOf("a", "b"), played = mapOf("a" to 1), titleArt = setOf("a", "b"))

        assertEquals(listOf(featured("a"), wide("b")), entries)
    }

    @Test
    fun anEmptyLibraryHasNoBands() {
        assertTrue(packMosaic(emptyList<MosaicEntry<String>>(), columns = 4).isEmpty())
    }

    @Test
    fun oneGameThatWasPlayedIsAFeaturedTile() {
        assertEquals(listOf(Cell("a", FEATURED, 0, 0)), pack(4, *sizes(listOf("a"), played = mapOf("a" to 1)).toTypedArray()))
    }

    @Test
    fun oneGameNeverPlayedIsASingleTile() {
        assertEquals(listOf(Cell("a", SINGLE, 0, 0)), pack(4, *sizes(listOf("a")).toTypedArray()))
    }

    @Test
    fun twoGamesBothPlayedSitSideBySide() {
        val cells = pack(4, *sizes(listOf("a", "b"), played = mapOf("a" to 2, "b" to 1)).toTypedArray())

        assertEquals(listOf(Cell("a", FEATURED, 0, 0), Cell("b", FEATURED, 2, 0)), cells)
    }

    @Test
    fun twoGamesOnePlayedFillTheRowInOrder() {
        val cells = pack(4, *sizes(listOf("a", "b"), played = mapOf("b" to 1)).toTypedArray())

        assertEquals(listOf(Cell("b", FEATURED, 0, 0), Cell("a", SINGLE, 2, 0)), cells)
    }

    @Test
    fun threeGamesAllPlayedPutTheThirdBelowTheFeaturedOnes() {
        val cells = pack(4, *sizes(listOf("a", "b", "c"), played = mapOf("a" to 3, "b" to 2, "c" to 1)).toTypedArray())

        assertEquals(listOf(Cell("a", FEATURED, 0, 0), Cell("b", FEATURED, 2, 0), Cell("c", SINGLE, 0, 2)), cells)
    }

    @Test
    fun threeGamesNonePlayedAreARowOfSingles() {
        val cells = pack(4, *sizes(listOf("a", "b", "c")).toTypedArray())

        assertEquals(listOf(Cell("a", SINGLE, 0, 0), Cell("b", SINGLE, 1, 0), Cell("c", SINGLE, 2, 0)), cells)
    }

    @Test
    fun oneFeaturedTileGetsItsSideFilledByTheNextTiles() {
        val cells = pack(4, featured("f"), single("a"), single("b"), single("c"), single("d"), single("e"))

        assertEquals(
            listOf(
                Cell("f", FEATURED, 0, 0),
                Cell("a", SINGLE, 2, 0),
                Cell("b", SINGLE, 3, 0),
                Cell("c", SINGLE, 2, 1),
                Cell("d", SINGLE, 3, 1),
                Cell("e", SINGLE, 0, 2),
            ),
            cells,
        )
    }

    @Test
    fun aWideTileThatDoesNotFitIsBackFilledBySomethingThatDoes() {
        // "w" can't go at the end of the first row, and "b" takes that gap as CSS's dense grid would
        val cells = pack(4, single("a"), wide("x"), wide("w"), single("b"), single("c"))

        assertEquals(
            listOf(
                Cell("a", SINGLE, 0, 0),
                Cell("x", WIDE, 1, 0),
                Cell("b", SINGLE, 3, 0),
                Cell("w", WIDE, 0, 1),
                Cell("c", SINGLE, 2, 1),
            ),
            cells,
        )
    }

    @Test
    fun aWideTileWithNothingToBackFillShrinksRatherThanLeavingAHole() {
        val cells = pack(4, single("a"), single("b"), single("c"), wide("w"))

        assertEquals(Cell("w", SINGLE, 3, 0), cells.last())
    }

    @Test
    fun aWideTileIsOnlyShrunkWhenThereIsNothingElse() {
        val cells = pack(4, single("a"), single("b"), single("c"), wide("w"), wide("x"), single("d"))

        assertEquals(Cell("d", SINGLE, 3, 0), cells[3])
        assertEquals(listOf(Cell("w", WIDE, 0, 1), Cell("x", WIDE, 2, 1)), cells.subList(4, 6))
    }

    @Test
    fun aRowOfNothingButWideTilesStaysWide() {
        val cells = pack(4, wide("a"), wide("b"), wide("c"), wide("d"))

        assertEquals(listOf(WIDE, WIDE, WIDE, WIDE), cells.map { it.size })
    }

    @Test
    fun oddColumnCountsWithOnlyWideTilesStillLeaveNoHoles() {
        val cells = pack(5, wide("a"), wide("b"), wide("c"), wide("d"), wide("e"))

        assertEquals(SINGLE, cells[2].size)
        assertEquals(4, cells[2].column)
    }

    @Test
    fun aFeaturedTileWithNoRoomLeftBecomesASingleTile() {
        val cells = pack(3, featured("a"), featured("b"))

        assertEquals(listOf(Cell("a", FEATURED, 0, 0), Cell("b", SINGLE, 2, 0)), cells)
    }

    @Test
    fun aWideTileBesideAFeaturedOneStaysInItsBand() {
        val bands = packMosaic(listOf(featured("f"), wide("w"), wide("x"), single("a")), columns = 4)

        assertEquals(2, bands.size)
        assertEquals(2, bands[0].rowCount)
        assertEquals(listOf("f", "w", "x"), bands[0].tiles.map { it.item })
        assertEquals(1, bands[1].rowCount)
        assertEquals(listOf("a"), bands[1].tiles.map { it.item })
    }

    @Test
    fun rowsAfterTheFeaturedOnesAreBandsOfTheirOwn() {
        val bands = packMosaic(sizes((1..10).map { "g$it" }, played = mapOf("g1" to 2, "g2" to 1)), columns = 4)

        assertEquals(listOf(2, 1, 1), bands.map { it.rowCount })
        assertEquals(listOf(2, 4, 4), bands.map { it.tiles.size })
        assertEquals(0, bands[1].tiles.first().row)
    }

    @Test
    fun widerGridsKeepTwoFeaturedTilesAndFillTheRest() {
        val cells = pack(6, *sizes((1..8).map { "g$it" }, played = mapOf("g1" to 2, "g2" to 1)).toTypedArray())

        assertEquals(2, cells.count { it.size == FEATURED })
        assertEquals(Cell("g3", SINGLE, 4, 0), cells[2])
        assertEquals(Cell("g5", SINGLE, 4, 1), cells[4])
    }

    @Test
    fun largeLibrariesPackQuicklyAndWithoutHoles() {
        val games = (1..5000).map { "g$it" }
        val entries = sizes(games, played = mapOf("g10" to 2, "g20" to 1), titleArt = games.filterIndexed { index, _ -> index % 3 == 0 }.toSet())

        // Goes through the hole check above as well
        val cells = pack(4, *entries.toTypedArray())

        assertEquals(5000, cells.size)
    }

    @Test
    fun aLibraryOfWideTilesOnAnOddGridDoesNotScanItselfForEveryRow() {
        val entries = (1..20000).map { wide("g$it") }

        val bands = packMosaic(entries, columns = 5)

        assertFalse(bands.isEmpty())
    }

    @Test
    fun whichGamesAreWideGoesByTheGameAndCoversAboutOneInSix() {
        val md5 = MessageDigest.getInstance("MD5")
        val hashes = (1..6000).map { index ->
            md5.digest(index.toString().toByteArray()).joinToString("") { "%02x".format(it) }
        }

        val wide = hashes.count(::isWideMosaicGame)

        assertTrue("$wide of 6000 went wide", wide in 800..1200)
        assertEquals(hashes.filter(::isWideMosaicGame), hashes.reversed().filter(::isWideMosaicGame).reversed())
    }

    @Test
    fun whatIsBesideATileIsLookedUpWithinItsRows() {
        val band = packMosaic(listOf(featured("f"), wide("w"), single("a"), single("b")), columns = 5).first()
        val tile = { item: String -> band.tiles.first { it.item == item } }

        assertTrue(hasTileAtEnd(band, tile("f")))
        // "w" goes to columns 2 and 3 of the first row, and "a" and "b" fill what's left
        assertTrue(hasTileAtEnd(band, tile("w")))
        assertFalse(hasTileAtEnd(band, tile("a")))
    }

    @Test
    fun theLastTileOfAShortLastRowHasNothingBesideIt() {
        val bands = packMosaic(listOf(single("a"), single("b")), columns = 4)
        val band = bands.single()

        assertTrue(hasTileAtEnd(band, band.tiles[0]))
        assertFalse(hasTileAtEnd(band, band.tiles[1]))
    }

    @Test
    fun columnsAreFourOnAPhoneAndMoreOnAWideWindow() {
        val gutter = 6f

        assertEquals(4, mosaicColumnsFor(312f, gutter)) // Fold cover screen
        assertEquals(4, mosaicColumnsFor(380f, gutter)) // The mock-up's phone
        assertEquals(6, mosaicColumnsFor(641f, gutter)) // Unfolded
        assertEquals(9, mosaicColumnsFor(883f, gutter)) // Unfolded in landscape
    }
}
