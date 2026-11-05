package me.magnum.melonds.ui.romlist

import me.magnum.melonds.domain.model.SortingMode
import me.magnum.melonds.domain.model.SortingOrder
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

class RomLibraryPresentationTest {

    private data class Game(val name: String, val played: Boolean = false, val achievements: Boolean = false)

    private val played = Game("played", played = true)
    private val untouched = Game("untouched")
    private val withAchievements = Game("achievements", played = true, achievements = true)
    private val games = listOf(played, untouched, withAchievements)

    private fun filter(filter: RomLibraryFilter) = filterLibrary(games, filter, { it.played }, { it.achievements })

    @Test
    fun allKeepsEverythingInOrder() {
        assertEquals(games, filter(RomLibraryFilter.ALL))
    }

    @Test
    fun playedAndNotPlayedSplitTheLibrary() {
        assertEquals(listOf(played, withAchievements), filter(RomLibraryFilter.PLAYED))
        assertEquals(listOf(untouched), filter(RomLibraryFilter.NOT_PLAYED))
    }

    @Test
    fun hasAchievementsGoesByAchievementsOnly() {
        assertEquals(listOf(withAchievements), filter(RomLibraryFilter.HAS_ACHIEVEMENTS))
    }

    @Test
    fun anEmptyLibraryIsSaidToBeEmptyWhateverTheFilter() {
        RomLibraryFilter.entries.forEach {
            assertEquals(LibraryEmptyReason.NO_ROMS, libraryEmptyReason(false, it, isSearching = false))
        }
    }

    @Test
    fun aSearchWithNoMatchesIsBlamedOnTheSearch() {
        assertEquals(LibraryEmptyReason.NO_SEARCH_RESULTS, libraryEmptyReason(false, RomLibraryFilter.ALL, isSearching = true))
        assertEquals(LibraryEmptyReason.NO_SEARCH_RESULTS, libraryEmptyReason(false, RomLibraryFilter.PLAYED, isSearching = true))
    }

    @Test
    fun aSearchThatTheFilterEmptiesIsBlamedOnBoth() {
        assertEquals(LibraryEmptyReason.NO_SEARCH_AND_FILTER_RESULTS, libraryEmptyReason(true, RomLibraryFilter.PLAYED, isSearching = true))
    }

    @Test
    fun aFilterAloneGetsItsOwnMessage() {
        assertEquals(LibraryEmptyReason.NOTHING_PLAYED, libraryEmptyReason(true, RomLibraryFilter.PLAYED, isSearching = false))
        assertEquals(LibraryEmptyReason.EVERYTHING_PLAYED, libraryEmptyReason(true, RomLibraryFilter.NOT_PLAYED, isSearching = false))
        assertEquals(LibraryEmptyReason.NO_ACHIEVEMENTS, libraryEmptyReason(true, RomLibraryFilter.HAS_ACHIEVEMENTS, isSearching = false))
    }

    @Test
    fun theSectionLabelNamesTheRealSort() {
        assertEquals(MosaicSectionLabel.RECENTLY_PLAYED, mosaicSectionLabel(SortingMode.RECENTLY_PLAYED, SortingOrder.DESCENDING, hasPlayedGames = true))
        assertEquals(MosaicSectionLabel.LEAST_RECENTLY_PLAYED, mosaicSectionLabel(SortingMode.RECENTLY_PLAYED, SortingOrder.ASCENDING, hasPlayedGames = true))
        assertEquals(MosaicSectionLabel.A_TO_Z, mosaicSectionLabel(SortingMode.ALPHABETICALLY, SortingOrder.ASCENDING, hasPlayedGames = true))
        assertEquals(MosaicSectionLabel.Z_TO_A, mosaicSectionLabel(SortingMode.ALPHABETICALLY, SortingOrder.DESCENDING, hasPlayedGames = false))
    }

    @Test
    fun nothingPlayedMeansNoRecentlyPlayedLabel() {
        assertNull(mosaicSectionLabel(SortingMode.RECENTLY_PLAYED, SortingOrder.DESCENDING, hasPlayedGames = false))
        assertNull(mosaicSectionLabel(SortingMode.RECENTLY_PLAYED, SortingOrder.ASCENDING, hasPlayedGames = false))
    }

    private fun style(
        customName: String? = null,
        name: String = "demo_final",
        fileName: String = "demo_final.nds",
        metadata: RomLibraryMetadata = RomLibraryMetadata(),
        hasArt: Boolean = false,
    ) = romTileStyle(customName, name, fileName, metadata, hasArt)

    @Test
    fun artWinsWhateverElseIsMissing() {
        assertEquals(RomTileStyle.ART, style(hasArt = true))
    }

    @Test
    fun aNameMadeUpFromTheFileIsNoMetadata() {
        assertEquals(RomTileStyle.NO_METADATA, style())
        assertEquals(RomTileStyle.NO_METADATA, style(name = ""))
    }

    @Test
    fun aBannerTitleLooksIntentional() {
        assertEquals(RomTileStyle.TITLED, style(name = "Comet Courier"))
    }

    @Test
    fun aPublisherOrRetroAchievementsTitleOrCustomNameIsEnoughToCountAsKnown() {
        assertEquals(RomTileStyle.TITLED, style(metadata = RomLibraryMetadata(publisher = "Starlane")))
        assertEquals(RomTileStyle.TITLED, style(metadata = RomLibraryMetadata(title = "Comet Courier")))
        assertEquals(RomTileStyle.TITLED, style(customName = "My game"))
    }

    @Test
    fun theFileStemLosesOnlyTheExtension() {
        assertEquals("Comet_Courier_(EU)", romFileStem("Comet_Courier_(EU).nds"))
        assertEquals("v1.1.demo", romFileStem("v1.1.demo.zip"))
        assertEquals("no extension", romFileStem("no extension"))
    }

    @Test
    fun displayTitlesPreferTheUserThenTheBannerThenRetroAchievementsThenTheFile() {
        val withRaTitle = RomLibraryMetadata(title = "RA Title")
        val noRaTitle = RomLibraryMetadata()

        assertEquals("Mine", withRaTitle.displayTitle("Mine", "Banner", "game.nds"))
        assertEquals("Banner", withRaTitle.displayTitle(null, "Banner", "game.nds"))
        assertEquals("RA Title", withRaTitle.displayTitle(null, "game", "game.nds"))
        assertEquals("RA Title", withRaTitle.displayTitle(null, " ", "game.nds"))
        assertEquals("game", noRaTitle.displayTitle(null, "game", "game.nds"))
        assertEquals("game", noRaTitle.displayTitle(null, "", "game.nds"))
    }

    @Test
    fun anIconOfOneColorIsBlank() {
        assertEquals(true, isBlankIcon(IntArray(1024)))
        assertEquals(true, isBlankIcon(IntArray(1024) { 0x7F123456 }))
        assertEquals(false, isBlankIcon(IntArray(1024) { if (it == 500) 1 else 0 }))
    }

    @Test
    fun theAppNameSplitsAroundTheAccent() {
        assertEquals(AppNameParts("melonade", "DS", ""), splitAppName("melonadeDS"))
        assertEquals(AppNameParts("melonade", "DS", " Dev"), splitAppName("melonadeDS Dev"))
        assertEquals(AppNameParts("melonade", "DS", " Nightly"), splitAppName("melonadeDS Nightly"))
        assertEquals(AppNameParts("Other", "", ""), splitAppName("Other"))
    }

    @Test
    fun titleArtOnItsOwnDoesNotMakeATileLookLikeItHasArt() {
        // A tile says whether it has art, since the art depends on the tile's size
        assertEquals(RomTileStyle.NO_METADATA, style(metadata = RomLibraryMetadata(titleArtFile = File("1_title.png"))))
    }
}
