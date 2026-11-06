package me.magnum.melonds.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

class AtomicFileWriteTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    private val destination by lazy { File(tempFolder.root, "data.json") }

    private fun assertOnlyDestinationIsLeft() {
        assertEquals(listOf(destination.name), tempFolder.root.list()?.toList())
    }

    @Test
    fun writesANewFile() {
        assertTrue(writeFileAtomically(destination) { it.write("new".toByteArray()); true })

        assertEquals("new", destination.readText())
        assertOnlyDestinationIsLeft()
    }

    @Test
    fun replacesAnExistingFile() {
        destination.writeText("old")

        assertTrue(writeFileAtomically(destination) { it.write("new".toByteArray()); true })

        assertEquals("new", destination.readText())
        assertOnlyDestinationIsLeft()
    }

    @Test
    fun destinationIsUntouchedWhileWriting() {
        destination.writeText("old")

        writeFileAtomically(destination) {
            it.write("new".toByteArray())
            assertEquals("old", destination.readText())
            true
        }
    }

    @Test
    fun aWriteThatReportsFailureKeepsTheOldFile() {
        destination.writeText("old")

        // Like Bitmap.compress(), which can fail halfway without throwing
        assertFalse(writeFileAtomically(destination) { it.write("cut".toByteArray()); false })

        assertEquals("old", destination.readText())
        assertOnlyDestinationIsLeft()
    }

    @Test
    fun aWriteThatThrowsKeepsTheOldFile() {
        destination.writeText("old")

        assertThrows(IOException::class.java) {
            writeFileAtomically(destination) {
                it.write("cut".toByteArray())
                throw IOException("Disk full")
            }
        }

        assertEquals("old", destination.readText())
        assertOnlyDestinationIsLeft()
    }

    @Test
    fun aTemporaryFileLeftByAnEarlierCrashDoesNotGetInTheWay() {
        destination.writeText("old")
        val staleFile = File(tempFolder.root, "data.json.123.tmp").apply { writeText("stale") }

        assertTrue(writeFileAtomically(destination) { it.write("new".toByteArray()); true })

        assertEquals("new", destination.readText())
        assertEquals("stale", staleFile.readText())
    }

    @Test
    fun leftoversOfTheDestinationAreDeleted() {
        destination.writeText("old")
        File(tempFolder.root, "data.json.123.tmp").writeText("stale")
        File(tempFolder.root, "data.json.tmp").writeText("stale")
        val kept = listOf("data.json", "data.json.corrupt", "other.json.123.tmp", "data.json2.123.tmp")
        kept.drop(1).forEach { File(tempFolder.root, it).writeText("keep") }

        deleteAtomicWriteLeftovers(destination)

        assertEquals(kept.sorted(), tempFolder.root.list()?.sorted())
        assertEquals("old", destination.readText())
    }

    @Test
    fun theSizeOfAFileThatExistsIsGiven() {
        destination.writeText("old")

        assertEquals(3L, sizeIfFileExists(destination))
    }

    @Test
    fun aMissingFileHasNoSize() {
        assertNull(sizeIfFileExists(destination))
    }

    @Test
    fun aDirectoryThrowsInsteadOfCountingAsMissing() {
        destination.mkdir()

        assertThrows(IOException::class.java) { sizeIfFileExists(destination) }
    }

    @Test
    fun deletingLeftoversWithNoFolderYetDoesNothing() {
        deleteAtomicWriteLeftovers(File(File(tempFolder.root, "missing"), "data.json"))

        assertEquals(emptyList<String>(), tempFolder.root.list()?.toList())
    }
}
