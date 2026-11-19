package me.magnum.melonds.impl.emulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDateTime

class SaveBackupNamesTest {

    private val time = LocalDateTime.of(2026, 11, 19, 11, 47, 7)

    @Test
    fun nameHasTheSaveNameAndLocalTime() {
        assertEquals("Game.sav.20261119-114707.bak", SaveBackupNames.backupName("Game.sav", time))
    }

    @Test
    fun nameKeepsSingleDigitsPadded() {
        assertEquals("Game.sav.20260102-030405.bak", SaveBackupNames.backupName("Game.sav", LocalDateTime.of(2026, 1, 2, 3, 4, 5)))
    }

    @Test
    fun nameIsParsedBack() {
        val name = SaveBackupNames.backupName("Pokemon (NDSi Enhanced).sav", time)
        assertEquals(time, SaveBackupNames.parseTime("Pokemon (NDSi Enhanced).sav", name))
    }

    @Test
    fun anotherGamesBackupIsNotParsed() {
        // Games can share a save folder, and one save name can start with another
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game 2.sav.20261119-114707.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Other.sav.20261119-114707.bak"))
        assertNull(SaveBackupNames.parseTime("Game", "Game.sav.20261119-114707.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.2.sav.20261119-114707.bak"))
    }

    @Test
    fun otherFilesAreNotParsed() {
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20261119-114707.bak (1)"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20261119-114707 (1).bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.2026111-114707.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20261119114707.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20261119-114707.tmp"))
    }

    @Test
    fun impossibleDatesAreNotParsed() {
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20261131-114707.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20261119-254707.bak"))
        assertNull(SaveBackupNames.parseTime("Game.sav", "Game.sav.20270229-000000.bak"))
    }

    @Test
    fun newestFirstSortsByTheNameNotTheListingOrder() {
        val files = listOf(
            "Game.sav.20261118-090000.bak",
            "Game.sav.20261119-114707.bak",
            "Other.sav.20261120-000000.bak",
            "Game.sav.20251231-235959.bak",
            "notes.txt",
            "Game.sav.20261119-114706.bak",
        )

        val sorted = SaveBackupNames.newestFirst("Game.sav", files) { it }.map { it.file }

        assertEquals(
            listOf(
                "Game.sav.20261119-114707.bak",
                "Game.sav.20261119-114706.bak",
                "Game.sav.20261118-090000.bak",
                "Game.sav.20251231-235959.bak",
            ),
            sorted,
        )
    }

    @Test
    fun filesWithoutANameAreLeftOut() {
        val sorted = SaveBackupNames.newestFirst("Game.sav", listOf<String?>(null, "Game.sav.20261119-114707.bak")) { it }
        assertEquals(listOf("Game.sav.20261119-114707.bak"), sorted.map { it.file })
    }

    @Test
    fun nothingIsPrunedUpToThree() {
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(listOf("c", "b", "a")))
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(emptyList<String>()))
    }

    @Test
    fun theOldestBeyondThreeArePruned() {
        assertEquals(listOf("b", "a"), SaveBackupNames.toPrune(listOf("e", "d", "c", "b", "a")))
    }

    @Test
    fun aProtectedBackupIsNeverPruned() {
        // The backup being restored can be the oldest
        assertEquals(listOf("b"), SaveBackupNames.toPrune(listOf("e", "d", "c", "b", "a")) { it == "a" })
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(listOf("d", "c", "b", "a")) { it == "a" })
    }

    @Test
    fun aProtectedBackupAmongTheNewestStillCounts() {
        assertEquals(listOf("a"), SaveBackupNames.toPrune(listOf("d", "c", "b", "a")) { it == "d" })
    }

    @Test
    fun aFullReadIsUsable() {
        assertEquals(SaveBackupNames.ReadCheck.USABLE, SaveBackupNames.checkRead(ByteArray(512), 512) { true })
    }

    @Test
    fun anUnknownLengthIsNotCheckedAgainst() {
        // COLUMN_SIZE is optional, so a provider can report 0 for a full save
        assertEquals(SaveBackupNames.ReadCheck.USABLE, SaveBackupNames.checkRead(ByteArray(512), 0) { true })
    }

    @Test
    fun aShortReadIsUnusable() {
        assertEquals(SaveBackupNames.ReadCheck.UNUSABLE, SaveBackupNames.checkRead(ByteArray(256), 512) { true })
        assertEquals(SaveBackupNames.ReadCheck.UNUSABLE, SaveBackupNames.checkRead(ByteArray(0), 512) { true })
    }

    @Test
    fun anEmptyFileHasNothingToBackUp() {
        assertEquals(SaveBackupNames.ReadCheck.NOTHING_TO_BACK_UP, SaveBackupNames.checkRead(ByteArray(0), 0) { true })
    }

    @Test
    fun aFailedReadOfAFileThatsThereIsUnusable() {
        assertEquals(SaveBackupNames.ReadCheck.UNUSABLE, SaveBackupNames.checkRead(null, 0) { true })
        assertEquals(SaveBackupNames.ReadCheck.UNUSABLE, SaveBackupNames.checkRead(null, 512) { true })
    }

    @Test
    fun aFailedReadOfAMissingFileHasNothingToBackUp() {
        assertEquals(SaveBackupNames.ReadCheck.NOTHING_TO_BACK_UP, SaveBackupNames.checkRead(null, 0) { false })
    }

    @Test
    fun existenceIsOnlyAskedWhenTheReadFailed() {
        var asked = false
        SaveBackupNames.checkRead(ByteArray(512), 0) { asked = true; true }
        assertFalse(asked)
    }

    @Test
    fun sameBytesAsTheNewestMeansNoNewBackup() {
        assertTrue(SaveBackupNames.isSameAsNewest(byteArrayOf(1, 2, 3), byteArrayOf(1, 2, 3)))
    }

    @Test
    fun differentBytesOrNoBackupMeansANewOne() {
        assertFalse(SaveBackupNames.isSameAsNewest(byteArrayOf(1, 2, 3), byteArrayOf(1, 2, 4)))
        assertFalse(SaveBackupNames.isSameAsNewest(byteArrayOf(1, 2, 3), byteArrayOf(1, 2)))
        assertFalse(SaveBackupNames.isSameAsNewest(byteArrayOf(1, 2, 3), null))
    }
}
