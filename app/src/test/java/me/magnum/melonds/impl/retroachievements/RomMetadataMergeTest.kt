package me.magnum.melonds.impl.retroachievements

import me.magnum.melonds.database.entities.retroachievements.RAGameAchievementCounts
import me.magnum.melonds.database.entities.retroachievements.RAGameEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameHashEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.domain.model.rom.RomMetadataFetchStatus
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class RomMetadataMergeTest {

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private val banner = RomLibraryMetadata.fromBanner("Banner Publisher")
    private val game = RAGameEntity(1, null, "RA Title", "https://media.retroachievements.org/Images/1.png")

    private fun row(
        publisher: String? = "RA Publisher",
        developer: String? = "RA Developer",
        genre: String? = "RPG",
        released: String? = "2009-03-01",
        status: Int = RAGameMetadataEntity.STATUS_OK,
    ) = RAGameMetadataEntity(1, publisher, developer, genre, released, "day", "/Images/2.png", null, null, null, 0, status)

    private fun merge(
        banner: RomLibraryMetadata = this.banner,
        game: RAGameEntity? = null,
        row: RAGameMetadataEntity? = null,
        artFile: File? = null,
        hasAchievements: Boolean = false,
    ) = mergeRomMetadata(banner, game, row, artFile, RAGameAchievementCounts(1, 10, 0, false).takeIf { hasAchievements })

    @Test
    fun noRetroAchievementsDataLeavesTheBannerOnly() {
        val merged = merge()

        assertEquals(RomLibraryMetadata(publisher = "Banner Publisher"), merged)
        assertNull(merged.tileImage)
        assertEquals("Banner Publisher", merged.caption())
    }

    @Test
    fun aBlankBannerMeansNoPublisher() {
        val merged = merge(banner = RomLibraryMetadata.fromBanner("  "))

        assertNull(merged.publisher)
        assertNull(merged.caption())
        assertNull(merged.details())
    }

    @Test
    fun theConnectApiGameAddsTitleAndIconOnly() {
        val merged = merge(game = game)

        assertEquals("RA Title", merged.title)
        assertEquals(game.icon, merged.gameIconUrl)
        assertEquals(game.icon, merged.tileImage)
        assertEquals("Banner Publisher", merged.publisher)
        assertNull(merged.developer)
        assertNull(merged.releaseYear)
    }

    @Test
    fun aMalformedIconIsDropped() {
        assertNull(merge(game = game.copy(icon = "not a url")).gameIconUrl)
        assertNull(merge(game = game.copy(icon = "")).gameIconUrl)
    }

    @Test
    fun aBlankTitleIsDropped() {
        assertNull(merge(game = game.copy(title = " ")).title)
    }

    @Test
    fun webApiFieldsWinOverTheBanner() {
        val merged = merge(row = row())

        assertEquals("RA Publisher", merged.publisher)
        assertEquals("RA Developer", merged.developer)
        assertEquals("RPG", merged.genre)
        assertEquals(2009, merged.releaseYear)
        assertEquals("RA Publisher · 2009", merged.caption())
    }

    @Test
    fun blankWebApiFieldsFallBackOrStayNull() {
        val merged = merge(row = row(publisher = "  ", developer = "", genre = null, released = null))

        assertEquals("Banner Publisher", merged.publisher)
        assertNull(merged.developer)
        assertNull(merged.genre)
        assertNull(merged.releaseYear)
        assertEquals("Banner Publisher", merged.caption())
    }

    @Test
    fun aRowWhoseRefreshFailedStillShowsItsData() {
        val merged = merge(row = row(status = RAGameMetadataEntity.STATUS_FAILED))

        assertEquals("RA Publisher", merged.publisher)
        assertEquals(2009, merged.releaseYear)
    }

    @Test
    fun anEmptyNotFoundRowChangesNothing() {
        val emptyRow = RAGameMetadataEntity(1, null, null, null, null, null, null, null, null, null, 0, RAGameMetadataEntity.STATUS_NOT_FOUND)

        assertEquals(
            RomLibraryMetadata(publisher = "Banner Publisher", fetchStatus = RomMetadataFetchStatus.NOT_FOUND, fetchedAt = 0),
            merge(row = emptyRow),
        )
    }

    @Test
    fun downloadedArtWinsOverTheIcon() {
        val art = File("1_box_art.png")

        assertEquals(art, merge(game = game, artFile = art).tileImage)
    }

    @Test
    fun artDownloadedAgainToTheSamePathCanBeToldApart() {
        val art = temporaryFolder.newFile("1_box_art.png")
        art.setLastModified(1_000_000)
        val first = merge(artFile = art)

        art.setLastModified(2_000_000)
        val second = merge(artFile = art)

        assertEquals(first.artFile, second.artFile)
        assertNotEquals(first.artModifiedAt, second.artModifiedAt)
        assertNull(merge().artModifiedAt)
    }

    @Test
    fun achievementsComeFromTheFlagPassedIn() {
        assertTrue(merge(hasAchievements = true).hasAchievements)
        assertFalse(merge(game = game, row = row()).hasAchievements)
    }

    @Test
    fun releaseYearsAreReadFromEveryGranularity() {
        assertEquals(2009, parseReleaseYear("2009-03-01"))
        assertEquals(2009, parseReleaseYear("2009-03"))
        assertEquals(2009, parseReleaseYear("2009"))
        assertEquals(2009, parseReleaseYear(" 2009-03-01 00:00:00 "))
    }

    @Test
    fun releaseDatesWeCannotReadGiveNoYear() {
        assertNull(parseReleaseYear(""))
        assertNull(parseReleaseYear("March 2009"))
        assertNull(parseReleaseYear("200"))
        assertNull(parseReleaseYear("20090301"))
    }

    @Test
    fun aSnapshotLooksGamesUpByHashAndAddsTheBanner() {
        val snapshot = buildRomMetadataSnapshot(
            gameHashes = listOf(RAGameHashEntity("hash", 1), RAGameHashEntity("other-dump", 1), RAGameHashEntity("no-publisher", 2)),
            games = listOf(game),
            gameMetadata = listOf(row(), row(publisher = " ", developer = null, genre = null, released = null).copy(gameId = 2)),
            achievementCounts = mapOf(1L to RAGameAchievementCounts(1, 10, 0, false)),
            artFiles = mapOf(1L to File("1_box_art.png")),
        )

        val found = snapshot.metadataFor("hash", "Banner Publisher")
        assertEquals("RA Publisher", found.publisher)
        assertEquals("RA Title", found.title)
        assertEquals(File("1_box_art.png"), found.artFile)
        assertTrue(found.hasAchievements)
        assertEquals(found, snapshot.metadataFor("other-dump", ""))

        val blankPublisher = snapshot.metadataFor("no-publisher", "Banner Publisher")
        assertEquals("Banner Publisher", blankPublisher.publisher)
        assertFalse(blankPublisher.hasAchievements)
    }

    @Test
    fun theReleaseDateAndItsGranularityComeThroughAsTheyAre() {
        val merged = merge(row = row())

        assertEquals("2009-03-01", merged.released)
        assertEquals("day", merged.releasedGranularity)
    }

    @Test
    fun theFetchStatusAndTimeComeFromTheRow() {
        assertEquals(RomMetadataFetchStatus.OK, merge(row = row()).fetchStatus)
        assertEquals(RomMetadataFetchStatus.NOT_FOUND, merge(row = row(status = RAGameMetadataEntity.STATUS_NOT_FOUND)).fetchStatus)
        assertEquals(RomMetadataFetchStatus.FAILED, merge(row = row(status = RAGameMetadataEntity.STATUS_FAILED)).fetchStatus)
        assertEquals(0L, merge(row = row()).fetchedAt)
        assertNull(merge().fetchStatus)
        assertNull(merge().fetchedAt)
    }

    @Test
    fun earnedAchievementsAreOnlyKnownOnceTheUserDataWasFetched() {
        val known = mergeRomMetadata(banner, null, null, null, RAGameAchievementCounts(1, 42, 0, isUserDataKnown = true))
        val unknown = mergeRomMetadata(banner, null, null, null, RAGameAchievementCounts(1, 42, 0, isUserDataKnown = false))

        assertTrue(known.hasAchievements)
        assertEquals(42, known.achievementsInSet)
        assertEquals(0, known.achievementsEarned)
        assertEquals(42, unknown.achievementsInSet)
        assertNull(unknown.achievementsEarned)
    }

    @Test
    fun aSnapshotKnowsWhichHashesTheLibraryLacks() {
        val snapshot = buildRomMetadataSnapshot(
            gameHashes = emptyList(),
            games = emptyList(),
            gameMetadata = emptyList(),
            achievementCounts = emptyMap(),
            artFiles = emptyMap(),
            unmatchedRomHashes = setOf("unknown"),
        )

        assertTrue(snapshot.isUnmatched("unknown"))
        assertFalse(snapshot.isUnmatched("anything else"))
        assertFalse(RomMetadataSnapshot.EMPTY.isUnmatched("unknown"))
    }

    @Test
    fun aSnapshotKeepsTitleArtApartFromTheTileArt() {
        val snapshot = buildRomMetadataSnapshot(
            gameHashes = listOf(RAGameHashEntity("hash", 1)),
            games = listOf(game),
            gameMetadata = emptyList(),
            achievementCounts = emptyMap(),
            artFiles = mapOf(1L to File("1_box_art.png")),
            titleArtFiles = mapOf(1L to File("1_title.png")),
        )

        val metadata = snapshot.metadataFor("hash", "")
        assertEquals(File("1_box_art.png"), metadata.artFile)
        assertEquals(File("1_title.png"), metadata.titleArtFile)
    }

    @Test
    fun aSnapshotKnowsNothingOfOtherRomsBeyondTheirBanner() {
        assertEquals(RomLibraryMetadata(publisher = "Banner"), RomMetadataSnapshot.EMPTY.metadataFor("hash", "Banner"))
        assertEquals(RomLibraryMetadata(), RomMetadataSnapshot.EMPTY.metadataFor("", " "))
    }

    @Test
    fun detailsSkipADeveloperThatRepeatsThePublisher() {
        val metadata = RomLibraryMetadata(publisher = "Same", developer = "Same", genre = "RPG", releaseYear = 2009)

        assertEquals("Same · RPG · 2009", metadata.details())
    }

    @Test
    fun detailsListEverythingKnown() {
        val metadata = RomLibraryMetadata(publisher = "Publisher", developer = "Developer", genre = "RPG", releaseYear = 2009)

        assertEquals("Publisher · Developer · RPG · 2009", metadata.details())
    }
}
