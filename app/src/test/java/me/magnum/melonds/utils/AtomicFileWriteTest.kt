package me.magnum.melonds.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
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
}
