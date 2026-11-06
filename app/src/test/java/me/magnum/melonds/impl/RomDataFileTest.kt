package me.magnum.melonds.impl

import com.google.gson.Gson
import com.google.gson.JsonIOException
import com.google.gson.reflect.TypeToken
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.io.Reader

class RomDataFileTest {

    private data class Config(val value: String)
    private data class Dto(val name: String, val config: Config)

    /**
     * Gives [text], then fails like a disk error would.
     */
    private class FailingReader(private val text: String) : Reader() {
        private var isTextRead = false

        override fun read(buffer: CharArray, offset: Int, length: Int): Int {
            if (isTextRead) {
                throw IOException("I/O error")
            }
            isTextRead = true
            text.toCharArray().copyInto(buffer, offset)
            return text.length
        }

        override fun close() = Unit
    }

    @get:Rule
    val tempFolder = TemporaryFolder()

    private val file by lazy { File(tempFolder.root, "rom_data.json") }
    private val romDataFile by lazy { RomDataFile(file, now = { 1000L }) }

    private val gson = Gson()
    private val dtoListType = object : TypeToken<List<Dto>>() {}.type
    private val validJson = """[{"name":"Game","config":{"value":"a"}}]"""

    private fun parseDtos(reader: Reader): List<Dto>? = gson.fromJson(reader, dtoListType)

    // Reads the config like RomDto.toModel() does, which throws when Gson left the field null
    private fun toNames(dtos: List<Dto>): List<String> = dtos.map { "${it.name}:${it.config.value}" }

    private fun RomDataFile.readDtos(parse: (Reader) -> List<Dto>? = ::parseDtos): RomDataRead<List<String>> = read(parse, ::toNames)

    private fun assertIsSetAside(result: RomDataRead<*>, contents: ByteArray) {
        assertTrue("Was $result", result is RomDataRead.SetAside)
        assertFalse(file.exists())
        assertArrayEquals(contents, (result as RomDataRead.SetAside).movedTo.readBytes())
    }

    private fun assertIsUnavailableAndUntouched(result: RomDataRead<*>, contents: String) {
        assertTrue("Was $result", result is RomDataRead.Unavailable)
        assertEquals(contents, file.readText())
        assertEquals(listOf(file.name), tempFolder.root.list()?.toList())
    }

    private fun assertSaveIsSkipped(dataFile: RomDataFile = romDataFile) {
        val contents = file.takeIf { it.isFile }?.readText()

        assertFalse(dataFile.canSave)
        assertFalse(dataFile.save { error("A skipped save must not write") })

        assertEquals(contents, file.takeIf { it.isFile }?.readText())
    }

    // What the repository does once the read data is in its list
    private fun assertSavesStayOffOnceAllowed(dataFile: RomDataFile = romDataFile) {
        dataFile.allowSaves()
        assertSaveIsSkipped(dataFile)
    }

    private fun assertSavesWorkOnceAllowed(dataFile: RomDataFile = romDataFile) {
        dataFile.allowSaves()
        assertTrue(dataFile.canSave)
        assertTrue(dataFile.save { it.write("saved".toByteArray()); true })
        assertEquals("saved", file.readText())
    }

    @Test
    fun aValidFileIsLoaded() {
        file.writeText(validJson)

        assertEquals(RomDataRead.Loaded(listOf("Game:a")), romDataFile.readDtos())
        assertEquals(validJson, file.readText())
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun aMissingFileIsMissing() {
        assertEquals(RomDataRead.Missing, romDataFile.readDtos())
        assertEquals(emptyList<String>(), tempFolder.root.list()?.toList())
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun aZeroLengthFileIsMissingAndStaysInPlace() {
        file.writeText("")

        assertEquals(RomDataRead.Missing, romDataFile.readDtos())
        assertEquals(listOf(file.name), tempFolder.root.list()?.toList())
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun truncatedJsonIsSetAsideByteForByte() {
        val contents = validJson.dropLast(10).toByteArray() + byteArrayOf(0xC3.toByte())
        file.writeBytes(contents)

        assertIsSetAside(romDataFile.readDtos(), contents)
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun malformedJsonIsSetAside() {
        val contents = "[{name:}]".toByteArray()
        file.writeBytes(contents)

        assertIsSetAside(romDataFile.readDtos(), contents)
    }

    @Test
    fun theWrongJsonShapeIsSetAside() {
        val contents = """{"name":"Game"}""".toByteArray()
        file.writeBytes(contents)

        assertIsSetAside(romDataFile.readDtos(), contents)
    }

    @Test
    fun aNonEmptyFileThatParsesToNothingIsSetAside() {
        val contents = "  \n".toByteArray()
        file.writeBytes(contents)

        assertIsSetAside(romDataFile.readDtos(), contents)
    }

    @Test
    fun aDtoWithANullFieldIsSetAside() {
        val contents = """[{"name":"Game"}]""".toByteArray()
        file.writeBytes(contents)

        assertIsSetAside(romDataFile.readDtos(), contents)
    }

    @Test
    fun aNullEntryIsSetAside() {
        val contents = "[null]".toByteArray()
        file.writeBytes(contents)

        assertIsSetAside(romDataFile.readDtos(), contents)
    }

    @Test
    fun aDiskErrorInGsonLeavesTheFileUntouched() {
        file.writeText(validJson)

        // Gson turns this one into a JsonSyntaxException
        val result = romDataFile.readDtos { parseDtos(FailingReader("""[{"name":"Ga""")) }

        assertIsUnavailableAndUntouched(result, validJson)
        assertSavesStayOffOnceAllowed()
    }

    @Test
    fun aDiskErrorOnTheFirstReadLeavesTheFileUntouched() {
        file.writeText(validJson)

        val result = romDataFile.readDtos { parseDtos(FailingReader("")) }

        assertIsUnavailableAndUntouched(result, validJson)
    }

    @Test
    fun aDiskErrorAfterTheJsonLeavesTheFileUntouched() {
        file.writeText(validJson)

        // Gson turns this one into a JsonIOException. The spaces are because it reads again if it gets under 5 characters, which fails inside the JSON instead
        val result = romDataFile.readDtos { parseDtos(FailingReader("[     ]")) }

        assertTrue((result as RomDataRead.Unavailable).cause is JsonIOException)
        assertIsUnavailableAndUntouched(result, validJson)
    }

    @Test
    fun aPlainIOExceptionLeavesTheFileUntouched() {
        file.writeText(validJson)

        assertIsUnavailableAndUntouched(romDataFile.readDtos { throw IOException("I/O error") }, validJson)
    }

    @Test
    fun aJsonIOExceptionLeavesTheFileUntouched() {
        file.writeText(validJson)

        assertIsUnavailableAndUntouched(romDataFile.readDtos { throw JsonIOException("I/O error") }, validJson)
    }

    @Test
    fun aFileThatCanNotBeOpenedIsLeftUntouched() {
        file.writeText(validJson)

        assertIsUnavailableAndUntouched(romDataFile.readDtos { throw FileNotFoundException("Too many open files") }, validJson)
    }

    @Test
    fun runningOutOfMemoryLeavesTheFileUntouched() {
        file.writeText(validJson)

        assertIsUnavailableAndUntouched(romDataFile.readDtos { throw OutOfMemoryError() }, validJson)
    }

    @Test
    fun runningOutOfMemoryWhileMappingLeavesTheFileUntouched() {
        file.writeText(validJson)

        val result = romDataFile.read(::parseDtos) { throw OutOfMemoryError() }

        assertIsUnavailableAndUntouched(result, validJson)
    }

    @Test
    fun anEarlierSetAsideFileSurvivesALaterOne() {
        file.writeText("first{")
        val first = romDataFile.readDtos() as RomDataRead.SetAside
        file.writeText("second{")

        // The same clock reading, so only the counter keeps the names apart
        val second = RomDataFile(file, now = { 1000L }).readDtos() as RomDataRead.SetAside

        assertEquals("rom_data.json.corrupt.1000", first.movedTo.name)
        assertEquals("rom_data.json.corrupt.1000.1", second.movedTo.name)
        assertEquals("first{", first.movedTo.readText())
        assertEquals("second{", second.movedTo.readText())
    }

    @Test
    fun aFailedMoveKeepsTheFileAndStopsSaves() {
        file.writeText("damaged{")
        val dataFile = RomDataFile(file, now = { 1000L }, moveFile = { _, _ -> throw IOException("Move failed") })

        val result = dataFile.readDtos()

        assertIsUnavailableAndUntouched(result, "damaged{")
        assertEquals(1, (result as RomDataRead.Unavailable).cause.suppressed.size)
        assertSavesStayOffOnceAllowed(dataFile)
    }

    @Test
    fun savesAreSkippedBeforeTheFirstRead() {
        file.writeText(validJson)

        assertSaveIsSkipped()
    }

    @Test
    fun savesAreSkippedAfterAGoodReadUntilAllowed() {
        file.writeText(validJson)

        romDataFile.readDtos()

        assertSaveIsSkipped()
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun savesAreSkippedAfterASetAsideUntilAllowed() {
        file.writeText("damaged{")

        romDataFile.readDtos()

        assertSaveIsSkipped()
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun savesAreSkippedAfterAMissingFileUntilAllowed() {
        romDataFile.readDtos()

        assertSaveIsSkipped()
        assertSavesWorkOnceAllowed()
    }

    @Test
    fun aFailedFileCheckIsNotTakenForAMissingFile() {
        file.writeText(validJson)
        val dataFile = RomDataFile(file, fileSize = { throw IOException("I/O error") })

        assertIsUnavailableAndUntouched(dataFile.readDtos(), validJson)
        assertSavesStayOffOnceAllowed(dataFile)
    }

    @Test
    fun aDirectoryAtThePathIsNotTakenForAMissingFile() {
        file.mkdir()

        assertTrue(romDataFile.readDtos() is RomDataRead.Unavailable)

        assertTrue(file.isDirectory)
        assertSavesStayOffOnceAllowed()
    }

    @Test
    fun savesStayOffAfterALaterGoodRead() {
        file.writeText(validJson)
        romDataFile.readDtos { throw IOException("I/O error") }

        assertEquals(RomDataRead.Loaded(listOf("Game:a")), romDataFile.readDtos())

        assertSavesStayOffOnceAllowed()
    }

    @Test
    fun aFailedLaterReadStopsSaves() {
        file.writeText(validJson)
        romDataFile.readDtos()
        romDataFile.allowSaves()

        romDataFile.readDtos { throw IOException("I/O error") }

        assertSavesStayOffOnceAllowed()
    }

    @Test
    fun theFirstReadDeletesLeftoverTemporaryFiles() {
        val leftover = File(tempFolder.root, "rom_data.json.123.tmp").apply { writeText("stale") }

        romDataFile.readDtos()

        assertFalse(leftover.exists())
    }

    @Test
    fun aLaterReadKeepsTemporaryFiles() {
        romDataFile.readDtos()
        romDataFile.allowSaves()
        val temporary = File(tempFolder.root, "rom_data.json.123.tmp").apply { writeText("in use") }

        romDataFile.readDtos()

        assertTrue(temporary.exists())
    }
}
