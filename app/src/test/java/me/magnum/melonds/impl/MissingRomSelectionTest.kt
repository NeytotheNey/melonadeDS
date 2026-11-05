package me.magnum.melonds.impl

import org.junit.Assert.assertEquals
import org.junit.Test

class MissingRomSelectionTest {

    private data class TestRom(val name: String, val directory: String?, val exists: Boolean)

    private val askedDirectories = mutableListOf<String>()

    private fun select(roms: List<TestRom>, directories: List<String> = listOf("card", "internal"), inaccessible: Set<String> = emptySet()): List<String> {
        return selectRemovableMissingRoms(
            roms = roms,
            searchDirectories = directories,
            isUnderDirectory = { rom, directory -> rom.directory == directory },
            fileExists = { it.exists },
            isDirectoryAccessible = { directory ->
                askedDirectories.add(directory)
                directory !in inaccessible
            },
        ).map { it.name }
    }

    @Test
    fun aRomWhoseFileIsThereStays() {
        assertEquals(emptyList<String>(), select(listOf(TestRom("a", "card", exists = true))))
    }

    @Test
    fun aMissingRomInAnAccessibleDirectoryGoes() {
        assertEquals(listOf("a"), select(listOf(TestRom("a", "card", exists = false))))
    }

    @Test
    fun aMissingRomInADirectoryThatCannotBeReachedStays() {
        val roms = listOf(TestRom("a", "card", exists = false), TestRom("b", "card", exists = false))

        assertEquals(emptyList<String>(), select(roms, inaccessible = setOf("card")))
    }

    @Test
    fun oneUnreachableDirectoryDoesNotProtectAnother() {
        val roms = listOf(TestRom("a", "card", exists = false), TestRom("b", "internal", exists = false))

        assertEquals(listOf("b"), select(roms, inaccessible = setOf("card")))
    }

    @Test
    fun aMissingRomUnderNoCurrentDirectoryStays() {
        assertEquals(emptyList<String>(), select(listOf(TestRom("a", "removed", exists = false), TestRom("b", null, exists = false))))
    }

    @Test
    fun noDirectoriesMeansNothingGoes() {
        assertEquals(emptyList<String>(), select(listOf(TestRom("a", "card", exists = false)), directories = emptyList()))
    }

    @Test
    fun aDirectoryIsOnlyAskedOnce() {
        val roms = (1..50).map { TestRom("g$it", "card", exists = false) }

        select(roms)

        assertEquals(listOf("card"), askedDirectories)
    }

    @Test
    fun aDirectoryIsNotAskedWhenNothingThereIsMissing() {
        select(listOf(TestRom("a", "card", exists = true), TestRom("b", "removed", exists = false)))

        assertEquals(emptyList<String>(), askedDirectories)
    }
}
