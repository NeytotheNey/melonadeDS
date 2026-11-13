package me.magnum.melonds.impl.emulator

import org.junit.Assert.assertEquals
import org.junit.Test

class SramFileLookupTest {

    private fun find(children: List<String?>, isRomFolder: Boolean) = SramFileLookup.find(children, { it }, "Pokemon.sav", isRomFolder)

    @Test
    fun saveNextToTheRomIsFound() {
        assertEquals(SramFileLookup.Result.Found("Pokemon.sav"), find(listOf("Pokemon.nds", "Pokemon.sav"), isRomFolder = true))
    }

    @Test
    fun missingSaveInAListedRomFolderIsMissing() {
        assertEquals(SramFileLookup.Result.Missing, find(listOf("Other.nds", "Pokemon.nds"), isRomFolder = true))
    }

    @Test
    fun emptyRomFolderIsUnreadable() {
        // What a folder hidden by Android looks like: an empty listing with no error flag
        assertEquals(SramFileLookup.Result.RomFolderUnreadable, find(emptyList(), isRomFolder = true))
    }

    @Test
    fun romFolderWithoutTheRomIsOnlyMissing() {
        // A stale folder link (ROM moved within a cloud tree) must not block the game for good; the name guard on create covers it
        assertEquals(SramFileLookup.Result.Missing, find(listOf("Other.nds", null), isRomFolder = true))
    }

    @Test
    fun emptySeparateSaveFolderIsMissing() {
        assertEquals(SramFileLookup.Result.Missing, find(emptyList(), isRomFolder = false))
    }

    @Test
    fun saveInASeparateFolderIsFound() {
        assertEquals(SramFileLookup.Result.Found("Pokemon.sav"), find(listOf("Other.sav", "Pokemon.sav"), isRomFolder = false))
    }

    @Test
    fun namesStopBeingReadAtTheSave() {
        val read = mutableListOf<String>()
        SramFileLookup.find(listOf("Pokemon.sav", "Pokemon.nds", "Other.nds"), { read.add(it); it }, "Pokemon.sav", isRomFolder = true)

        assertEquals(listOf("Pokemon.sav"), read)
    }
}
