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
    fun nothingIsPrunedUpToTheKeepCount() {
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(listOf("c", "b", "a"), keep = 3))
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(emptyList<String>(), keep = 3))
    }

    @Test
    fun theOldestBeyondTheKeepCountArePruned() {
        val newestFirst = listOf("g", "f", "e", "d", "c", "b", "a")
        assertEquals(listOf("d", "c", "b", "a"), SaveBackupNames.toPrune(newestFirst, keep = 3))
        assertEquals(listOf("b", "a"), SaveBackupNames.toPrune(newestFirst, keep = 5))
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(newestFirst, keep = 10))
    }

    @Test
    fun aLoweredCountPrunesDownToItInOneGo() {
        // 50 kept, then the setting goes to 3: the next backup leaves the newest 3
        val newestFirst = (50 downTo 1).map { "b$it" }
        val pruned = SaveBackupNames.toPrune(newestFirst, keep = 3)
        assertEquals(47, pruned.size)
        assertEquals(listOf("b50", "b49", "b48"), newestFirst - pruned.toSet())
    }

    @Test
    fun aKeepCountBelowTheMinimumKeepsTheMinimum() {
        assertEquals(3, SaveBackupNames.MIN_KEEP)
        val newestFirst = listOf("e", "d", "c", "b", "a")
        listOf(2, 1, 0, -5).forEach { keep ->
            assertEquals("keep=$keep", listOf("b", "a"), SaveBackupNames.toPrune(newestFirst, keep))
        }
    }

    @Test
    fun aProtectedBackupIsNeverPruned() {
        // The backup being restored can be the oldest
        assertEquals(listOf("b"), SaveBackupNames.toPrune(listOf("e", "d", "c", "b", "a"), keep = 3) { it == "a" })
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(listOf("d", "c", "b", "a"), keep = 3) { it == "a" })
    }

    @Test
    fun aProtectedBackupAmongTheNewestStillCounts() {
        assertEquals(listOf("a"), SaveBackupNames.toPrune(listOf("d", "c", "b", "a"), keep = 3) { it == "d" })
    }

    @Test
    fun bothProtectedBackupsStayForEveryKeepCount() {
        // A restore: "new" is the current save's backup, just made, and "old" is the one being restored
        val newestFirst = listOf("new", "e", "d", "c", "b", "a", "old")
        val protectedNames = setOf("new", "old")
        listOf(3, 5, 10, 20, 50).forEach { keep ->
            val pruned = SaveBackupNames.toPrune(newestFirst, keep) { it in protectedNames }
            assertTrue("keep=$keep", pruned.none { it in protectedNames })
            assertEquals("keep=$keep", newestFirst.drop(keep).filterNot { it in protectedNames }, pruned)
        }
    }

    @Test
    fun aBootRightAfterARestoreKeepsBothRestoreBackups() {
        // Even a stored count of 1: restoring B backs up the current save as N, then the game-start backup G is the only protected one
        val keep = SaveBackupNames.parseKeepCount("1")
        assertEquals(emptyList<String>(), SaveBackupNames.toPrune(listOf("G", "N", "B"), keep) { it == "G" })
        assertEquals(listOf("x"), SaveBackupNames.toPrune(listOf("G", "N", "B", "x"), keep) { it == "G" })
    }

    @Test
    fun aJustWrittenBackupThatSortsOlderIsKept() {
        // The clock went back, so the new backup isn't the newest by name
        val pruned = SaveBackupNames.toPrune(listOf("e", "d", "c", "new", "b", "a"), keep = 3) { it == "new" }
        assertEquals(listOf("b", "a"), pruned)
    }

    @Test
    fun theKeepCountIsReadFromTheStoredValue() {
        assertEquals(3, SaveBackupNames.parseKeepCount("3"))
        assertEquals(50, SaveBackupNames.parseKeepCount("50"))
        assertEquals(SaveBackupNames.DEFAULT_KEEP, SaveBackupNames.parseKeepCount(null))
        assertEquals(SaveBackupNames.DEFAULT_KEEP, SaveBackupNames.parseKeepCount("lots"))
    }

    @Test
    fun aStoredCountBelowTheMinimumClampsToIt() {
        // "1" was offered by a build that never shipped
        assertEquals(3, SaveBackupNames.parseKeepCount("1"))
        assertEquals(3, SaveBackupNames.parseKeepCount("2"))
        assertEquals(3, SaveBackupNames.parseKeepCount("0"))
        assertEquals(3, SaveBackupNames.parseKeepCount("-1"))
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
