package me.magnum.melonds.migrations.helper

import android.content.ContextWrapper
import com.google.gson.Gson
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

class GenericJsonArrayMigrationHelperTest {

    private data class Item(val name: String)

    @get:Rule
    val tempFolder = TemporaryFolder()

    private val filesDir by lazy { tempFolder.newFolder("files") }
    private val dataFile by lazy { File(filesDir, "data.json") }

    // Only filesDir is used, so the rest of the Context can stay stubbed
    private val context by lazy {
        object : ContextWrapper(null) {
            override fun getFilesDir(): File = this@GenericJsonArrayMigrationHelperTest.filesDir
        }
    }

    private val failures = mutableListOf<Throwable>()
    private val helper by lazy { GenericJsonArrayMigrationHelper(context, Gson()) { _, error -> failures.add(error) } }

    private fun renameItems(suffix: String = "!") {
        helper.migrateJsonArrayData("data.json", Item::class.java) { Item(it.name + suffix) }
    }

    private fun assertFailedAndUntouched(contents: String) {
        assertEquals(contents, dataFile.readText())
        assertEquals(1, failures.size)
        assertEquals(listOf(dataFile.name), filesDir.list()?.toList())
    }

    @Test
    fun aValidFileIsConverted() {
        dataFile.writeText("""[{"name":"a"}]""")

        renameItems()

        assertEquals("""[{"name":"a!"}]""", dataFile.readText())
        assertTrue(failures.isEmpty())
        assertEquals(listOf(dataFile.name), filesDir.list()?.toList())
    }

    @Test
    fun itemsTheMapperDropsAreLeftOut() {
        dataFile.writeText("""[{"name":"a"},{"name":"b"}]""")

        helper.migrateJsonArrayData("data.json", Item::class.java) { item -> item.takeIf { it.name == "b" } }

        assertEquals("""[{"name":"b"}]""", dataFile.readText())
    }

    @Test
    fun aMissingFileBecomesAnEmptyList() {
        renameItems()

        assertEquals("[]", dataFile.readText())
        assertTrue(failures.isEmpty())
    }

    @Test
    fun aZeroLengthFileBecomesAnEmptyList() {
        dataFile.writeText("")

        renameItems()

        assertEquals("[]", dataFile.readText())
        assertTrue(failures.isEmpty())
    }

    @Test
    fun truncatedJsonIsLeftUntouched() {
        dataFile.writeText("""[{"name":"a"}""")

        renameItems()

        assertFailedAndUntouched("""[{"name":"a"}""")
    }

    @Test
    fun aNonEmptyFileThatParsesToNothingIsLeftUntouched() {
        dataFile.writeText("  \n")

        renameItems()

        assertFailedAndUntouched("  \n")
    }

    @Test
    fun aMapperThatThrowsLeavesTheFileUntouched() {
        dataFile.writeText("""[{"name":"a"}]""")

        helper.migrateJsonArrayData<Item, Item>("data.json", Item::class.java) { throw NullPointerException() }

        assertFailedAndUntouched("""[{"name":"a"}]""")
    }

    @Test
    fun aFailedWriteKeepsTheOldFile() {
        val contents = """[{"name":"a"}]"""
        dataFile.writeText(contents)
        val movedFolder = File(tempFolder.root, "moved")

        // Runs between the read and the write. The parent turning into a file makes the write itself fail, while the old file stays readable in its new place
        helper.migrateJsonArrayData("data.json", Item::class.java) { item ->
            assertTrue(filesDir.renameTo(movedFolder))
            filesDir.writeText("")
            item
        }

        assertTrue("Was $failures", failures.single() is IOException)
        assertEquals(contents, File(movedFolder, dataFile.name).readText())
        assertEquals(listOf(dataFile.name), movedFolder.list()?.toList())
    }

    @Test
    fun aFileThatCanNotBeCheckedIsNotWrittenOver() {
        // A real way to fail other than the file being missing, which the old isFile check took for missing
        dataFile.mkdir()

        renameItems()

        assertTrue(dataFile.isDirectory)
        assertEquals(1, failures.size)
        assertEquals(listOf(dataFile.name), filesDir.list()?.toList())
    }

    @Test
    fun laterMigrationsOfAFileThatFailedAreSkipped() {
        dataFile.writeText("""[{"name":"a"}]""")
        helper.migrateJsonArrayData<Item, Item>("data.json", Item::class.java) { throw NullPointerException() }

        renameItems()

        assertFailedAndUntouched("""[{"name":"a"}]""")
    }

    @Test
    fun aFailureDoesNotStopOtherFiles() {
        val otherFile = File(filesDir, "other.json").apply { writeText("""[{"name":"b"}]""") }
        dataFile.writeText("broken{")
        renameItems()

        helper.migrateJsonArrayData("other.json", Item::class.java) { Item(it.name + "!") }

        assertEquals("""[{"name":"b!"}]""", otherFile.readText())
        assertEquals("broken{", dataFile.readText())
    }
}
