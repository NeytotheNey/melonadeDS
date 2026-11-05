package me.magnum.melonds.ui.romlist

import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.domain.model.rom.RomMetadataFetchStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.Locale
import java.util.concurrent.TimeUnit

class RomSheetContentTest {

    private val now = 1_800_000_000_000L
    private val threeDaysAgo = now - TimeUnit.DAYS.toMillis(3)

    private val fullMetadata = RomLibraryMetadata(
        publisher = "Starlane",
        developer = "Pocket Orbit",
        genre = "Action",
        releaseYear = 2008,
        artFile = File("1_box_art.png"),
        hasAchievements = true,
        released = "2008-03-14",
        releasedGranularity = "day",
        achievementsInSet = 42,
        achievementsEarned = 3,
        fetchStatus = RomMetadataFetchStatus.OK,
        fetchedAt = threeDaysAgo,
    )

    private fun content(
        metadata: RomLibraryMetadata = RomLibraryMetadata(),
        customName: String? = null,
        name: String = "Comet Courier",
        fileName: String = "comet.nds",
        lastPlayedAt: Long? = null,
        isHashUnmatched: Boolean = false,
        isHashMissing: Boolean = false,
        hasWebApiKey: Boolean = true,
    ) = buildRomSheetContent(customName, name, fileName, lastPlayedAt, metadata, isHashUnmatched, isHashMissing, hasWebApiKey, now, Locale.US)

    private fun RomSheetContent.value(label: RomSheetFactLabel) = facts.firstOrNull { it.label == label }?.value

    @Test
    fun aKnownGameShowsEveryFactInOrder() {
        val sheet = content(fullMetadata, lastPlayedAt = now - TimeUnit.HOURS.toMillis(2))

        assertEquals(
            listOf(
                RomSheetFactLabel.PUBLISHER,
                RomSheetFactLabel.DEVELOPER,
                RomSheetFactLabel.GENRE,
                RomSheetFactLabel.RELEASED,
                RomSheetFactLabel.ACHIEVEMENTS,
                RomSheetFactLabel.LAST_PLAYED,
            ),
            sheet.facts.map { it.label },
        )
        assertEquals(RomSheetFactValue.Text("Mar 14, 2008"), sheet.value(RomSheetFactLabel.RELEASED))
        assertEquals(RomSheetFactValue.AchievementCount(42, 3), sheet.value(RomSheetFactLabel.ACHIEVEMENTS))
        assertEquals(RomSheetFactValue.Played(RelativeAge.Hours(2)), sheet.value(RomSheetFactLabel.LAST_PLAYED))
        assertEquals("Comet Courier", sheet.title)
        assertEquals("Starlane · 2008", sheet.subtitle)
        assertNull(sheet.note)
    }

    @Test
    fun whatIsMissingLeavesNoRow() {
        val sheet = content(RomLibraryMetadata(genre = "Puzzle", fetchStatus = RomMetadataFetchStatus.OK, fetchedAt = now))

        assertEquals(listOf(RomSheetFactLabel.GENRE, RomSheetFactLabel.LAST_PLAYED), sheet.facts.map { it.label })
    }

    @Test
    fun aGameNeverPlayedSaysSo() {
        assertEquals(RomSheetFactValue.NeverPlayed, content(fullMetadata).value(RomSheetFactLabel.LAST_PLAYED))
    }

    @Test
    fun earnedIsOnlyShownWhenKnown() {
        val unknownEarned = content(fullMetadata.copy(achievementsEarned = null))
        val nothingEarned = content(fullMetadata.copy(achievementsEarned = 0))

        assertEquals(RomSheetFactValue.AchievementCount(42, null), unknownEarned.value(RomSheetFactLabel.ACHIEVEMENTS))
        assertEquals(RomSheetFactValue.AchievementCount(42, 0), nothingEarned.value(RomSheetFactLabel.ACHIEVEMENTS))
    }

    @Test
    fun aGameOpenedWithALoginThatHasNoAchievementsHasNoSet() {
        val sheet = content(RomLibraryMetadata(title = "Comet Courier"))

        assertEquals(RomSheetFactValue.NoAchievementSet, sheet.value(RomSheetFactLabel.ACHIEVEMENTS))
    }

    @Test
    fun aGameNeverOpenedSaysNothingAboutAchievements() {
        val sheet = content(RomLibraryMetadata(genre = "Puzzle", fetchStatus = RomMetadataFetchStatus.OK, fetchedAt = now))

        assertNull(sheet.value(RomSheetFactLabel.ACHIEVEMENTS))
    }

    @Test
    fun achievementsWithoutACountSayHasAchievements() {
        val sheet = content(RomLibraryMetadata(hasAchievements = true))

        assertEquals(RomSheetFactValue.HasAchievements, sheet.value(RomSheetFactLabel.ACHIEVEMENTS))
    }

    @Test
    fun anUnknownRomShowsFileAndLastPlayedAndWhy() {
        val sheet = content(name = "ROM_0412", fileName = "ROM_0412.nds", lastPlayedAt = now - TimeUnit.DAYS.toMillis(3), isHashUnmatched = true)

        assertEquals("ROM_0412", sheet.title)
        assertEquals("ROM_0412.nds", sheet.subtitle)
        assertEquals(listOf(RomSheetFactLabel.FILE, RomSheetFactLabel.LAST_PLAYED), sheet.facts.map { it.label })
        assertEquals(RomSheetFactValue.Text("ROM_0412.nds"), sheet.value(RomSheetFactLabel.FILE))
        assertEquals(RomSheetNote.NOT_FOUND_SHOWING_FILE_NAME, sheet.note)
    }

    @Test
    fun aRomWithABannerKeepsItsTitleAndPublisherWhenRetroAchievementsHasNothing() {
        val sheet = content(RomLibraryMetadata(publisher = "Starlane"), isHashUnmatched = true)

        assertEquals("Comet Courier", sheet.title)
        assertEquals("Starlane", sheet.subtitle)
        assertEquals(listOf(RomSheetFactLabel.PUBLISHER, RomSheetFactLabel.FILE, RomSheetFactLabel.LAST_PLAYED), sheet.facts.map { it.label })
        assertEquals(RomSheetNote.NOT_FOUND_SHOWING_BANNER, sheet.note)
    }

    @Test
    fun aCachedRowIsCreditedWithItsAge() {
        assertEquals(RomSheetCredit.Cached(3), content(fullMetadata).credit)
        assertEquals(RomSheetCredit.Cached(0), content(fullMetadata.copy(fetchedAt = now)).credit)
    }

    @Test
    fun aRowWithItsTimeClearedHasNoAge() {
        // Clearing an image that failed for good sets the time to 0, and the age of that would be decades
        val cleared = fullMetadata.copy(fetchedAt = 0)

        assertEquals(RomSheetCredit.Cached(null), content(cleared).credit)
        assertEquals(RomSheetCredit.Cached(null), content(fullMetadata.copy(fetchedAt = -1)).credit)
        assertEquals(RomSheetCredit.Cached(null), content(fullMetadata.copy(fetchedAt = null)).credit)
    }

    @Test
    fun aFailedFetchWithItsTimeClearedHasNoAge() {
        val sheet = content(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.FAILED, fetchedAt = 0))

        assertEquals(RomSheetCredit.FetchPending(null), sheet.credit)
    }

    @Test
    fun aNotFoundWithItsTimeClearedHasNoAge() {
        val sheet = content(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.NOT_FOUND, fetchedAt = 0))

        assertEquals(RomSheetCredit.NotFoundChecked(null), sheet.credit)
    }

    @Test
    fun aMissingHashGetsItsOwnNoteAndCreditWithAKeyOrWithout() {
        listOf(true, false).forEach { hasKey ->
            val sheet = content(isHashMissing = true, hasWebApiKey = hasKey)

            assertEquals(RomSheetNote.HASH_MISSING, sheet.note)
            assertEquals(RomSheetCredit.HashMissing, sheet.credit)
        }
    }

    @Test
    fun aFailedFetchWithNoKeyDoesNotPromiseAnotherTry() {
        val sheet = content(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.FAILED, fetchedAt = threeDaysAgo), hasWebApiKey = false)

        assertEquals(RomSheetNote.NO_WEB_API_KEY, sheet.note)
        assertEquals(RomSheetCredit.NoKey, sheet.credit)
    }

    @Test
    fun aRowWaitingForARefreshWithNoKeyIsNotDueForAnything() {
        val sheet = content(fullMetadata.copy(fetchStatus = RomMetadataFetchStatus.FAILED), hasWebApiKey = false)

        assertEquals(RomSheetCredit.Cached(null), sheet.credit)
    }

    @Test
    fun aRetroAchievementsTitleFillsInForANameMadeUpFromTheFile() {
        val sheet = content(RomLibraryMetadata(title = "Comet Courier"), name = "comet", fileName = "comet.nds")

        assertEquals("Comet Courier", sheet.title)
    }

    @Test
    fun theRealNotFoundStatusIsCreditedWithWhenItWasChecked() {
        val sheet = content(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.NOT_FOUND, fetchedAt = threeDaysAgo))

        assertEquals(RomSheetCredit.NotFoundChecked(3), sheet.credit)
        assertEquals(RomSheetNote.NOT_FOUND_SHOWING_BANNER, sheet.note)
    }

    @Test
    fun aHashTheLibraryLacksIsNotFoundWithAKeyOrWithout() {
        assertEquals(RomSheetCredit.HashNotFound, content(isHashUnmatched = true, hasWebApiKey = true).credit)
        assertEquals(RomSheetCredit.HashNotFound, content(isHashUnmatched = true, hasWebApiKey = false).credit)
    }

    @Test
    fun noKeyIsNeverCalledNotFound() {
        val sheet = content(hasWebApiKey = false)

        assertEquals(RomSheetCredit.NoKey, sheet.credit)
        assertEquals(RomSheetNote.NO_WEB_API_KEY, sheet.note)
    }

    @Test
    fun noRowYetWithAKeyIsNeverCalledNotFound() {
        val sheet = content(hasWebApiKey = true)

        assertEquals(RomSheetCredit.NotDownloaded, sheet.credit)
        assertEquals(RomSheetNote.NOT_DOWNLOADED_YET, sheet.note)
    }

    @Test
    fun aRowWinsOverThereBeingNoKey() {
        assertEquals(RomSheetCredit.Cached(3), content(fullMetadata, hasWebApiKey = false).credit)
    }

    @Test
    fun aFailedFetchWithNothingToShowSaysItWillBeTriedAgain() {
        val sheet = content(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.FAILED, fetchedAt = threeDaysAgo))

        assertEquals(RomSheetCredit.FetchPending(3), sheet.credit)
        assertEquals(RomSheetNote.FETCH_PENDING, sheet.note)
    }

    @Test
    fun aRowWaitingForARefreshStillShowsItsData() {
        val sheet = content(fullMetadata.copy(fetchStatus = RomMetadataFetchStatus.FAILED))

        assertEquals(RomSheetCredit.DueForRefresh, sheet.credit)
        assertNull(sheet.note)
        assertEquals(RomSheetFactValue.Text("Pocket Orbit"), sheet.value(RomSheetFactLabel.DEVELOPER))
    }

    @Test
    fun onlyATitleFromALoginIsCreditedAsSuch() {
        val metadata = RomLibraryMetadata(title = "Comet Courier")

        assertEquals(RomSheetCredit.TitleOnlyNoKey, content(metadata, hasWebApiKey = false).credit)
        assertEquals(RomSheetCredit.TitleOnlyNotDownloaded, content(metadata, hasWebApiKey = true).credit)
        assertNull(content(metadata, hasWebApiKey = false).note)
    }

    @Test
    fun theReleaseDateShowsNoMoreThanItsGranularityVouchesFor() {
        assertEquals("Mar 14, 2008", formatReleaseDate("2008-03-14", "day", Locale.US))
        assertEquals("Mar 14, 2008", formatReleaseDate("2008-03-14 00:00:00", "day", Locale.US))
        assertEquals("Mar 2008", formatReleaseDate("2008-03-01", "month", Locale.US))
        assertEquals("2008", formatReleaseDate("2008-01-01", "year", Locale.US))
        assertEquals("2008", formatReleaseDate("2008-03-14", null, Locale.US))
        assertEquals("2008", formatReleaseDate("2008", "day", Locale.US))
    }

    @Test
    fun aReleaseDateWeCannotReadShowsNothing() {
        assertNull(formatReleaseDate(null, "day", Locale.US))
        assertNull(formatReleaseDate("March 2008", "month", Locale.US))
        assertEquals("2008", formatReleaseDate("2008-13-40", "day", Locale.US))
    }

    @Test
    fun relativeAgesReadLikeTheMockup() {
        fun ago(amount: Long, unit: TimeUnit) = relativeAge(now - unit.toMillis(amount), now)

        assertEquals(RelativeAge.JustNow, ago(10, TimeUnit.MINUTES))
        assertEquals(RelativeAge.Hours(2), ago(2, TimeUnit.HOURS))
        assertEquals(RelativeAge.Yesterday, ago(30, TimeUnit.HOURS))
        assertEquals(RelativeAge.Days(3), ago(3, TimeUnit.DAYS))
        assertEquals(RelativeAge.LastWeek, ago(8, TimeUnit.DAYS))
        assertEquals(RelativeAge.Weeks(2), ago(15, TimeUnit.DAYS))
        assertEquals(RelativeAge.LastMonth, ago(40, TimeUnit.DAYS))
        assertEquals(RelativeAge.Months(3), ago(95, TimeUnit.DAYS))
        assertEquals(RelativeAge.LastYear, ago(400, TimeUnit.DAYS))
        assertEquals(RelativeAge.Years(3), ago(1100, TimeUnit.DAYS))
    }

    @Test
    fun aClockThatWentBackwardsIsJustNow() {
        assertEquals(RelativeAge.JustNow, relativeAge(now + 5000, now))
    }

    @Test
    fun metadataKnowsWhetherAnythingCameFromRetroAchievements() {
        assertFalse(RomLibraryMetadata(publisher = "Banner").hasRetroAchievementsData)
        assertFalse(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.NOT_FOUND, fetchedAt = now).hasRetroAchievementsData)
        assertTrue(RomLibraryMetadata(fetchStatus = RomMetadataFetchStatus.OK, fetchedAt = now).hasRetroAchievementsData)
        assertTrue(RomLibraryMetadata(title = "Comet Courier").hasRetroAchievementsData)
    }
}
