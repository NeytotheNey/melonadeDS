package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.BuiltInBackgrounds
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException
import java.util.UUID
import java.util.concurrent.TimeUnit

class BuiltInBackgroundInstallerTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    private val directory by lazy { File(tempFolder.root, "backgrounds/builtin") }
    private val allFileNames = BuiltInBackgrounds.all.map { it.fileName }
    private val allIds = BuiltInBackgrounds.all.map { it.id }
    private val invalidated = mutableListOf<UUID>()

    private fun install(
        version: String = "1",
        artwork: String = "artwork",
        failing: Set<String> = emptySet(),
        invalidation: () -> Boolean = { true },
    ): List<String> {
        invalidated.clear()
        return BuiltInBackgroundInstaller(
            directory = directory,
            versionStamp = version,
            openAsset = { fileName ->
                if (fileName in failing) {
                    throw IOException("Can't open $fileName")
                }
                "$artwork $fileName".byteInputStream()
            },
            invalidateThumbnail = {
                invalidated.add(it)
                invalidation()
            },
        ).install().map { (background, file) ->
            assertEquals(background.fileName, file.name)
            background.fileName
        }
    }

    private fun contentsOf(fileName: String) = File(directory, fileName).readText()

    @Test
    fun firstInstallCopiesEverythingInOrder() {
        assertEquals(allFileNames, install())
        assertEquals(allIds, invalidated)
        assertEquals("artwork carbon.svg", contentsOf("carbon.svg"))
    }

    @Test
    fun sameVersionCopiesNothing() {
        install()

        assertEquals(allFileNames, install(artwork = "new artwork"))
        assertEquals(emptyList<UUID>(), invalidated)
        assertEquals("artwork carbon.svg", contentsOf("carbon.svg"))
    }

    @Test
    fun newVersionCopiesEverythingAgain() {
        install(version = "1")

        assertEquals(allFileNames, install(version = "2", artwork = "new artwork"))
        assertEquals(allIds, invalidated)
        assertEquals("new artwork carbon.svg", contentsOf("carbon.svg"))
    }

    @Test
    fun missingFileIsCopiedAloneOnTheSameVersion() {
        install()
        File(directory, "hex.svg").delete()

        assertEquals(allFileNames, install())
        assertEquals(listOf(BuiltInBackgrounds.stableId("hex.svg")), invalidated)
    }

    @Test
    fun failedFirstCopyIsLeftOutAndTriedAgain() {
        assertEquals(allFileNames - "kiwi.svg", install(failing = setOf("kiwi.svg")))
        assertFalse(invalidated.contains(BuiltInBackgrounds.stableId("kiwi.svg")))
        assertEquals(emptyList<String>(), directory.list().orEmpty().filter { it.endsWith(".tmp") })

        assertEquals(allFileNames, install())
        assertTrue(invalidated.contains(BuiltInBackgrounds.stableId("kiwi.svg")))
    }

    @Test
    fun failedUpdateKeepsTheOldFileAndTriesAgain() {
        install(version = "1")

        assertEquals(allFileNames, install(version = "2", artwork = "new artwork", failing = setOf("kiwi.svg")))
        assertEquals("artwork kiwi.svg", contentsOf("kiwi.svg"))
        assertEquals("new artwork carbon.svg", contentsOf("carbon.svg"))

        // The version wasn't marked as installed, so the failed one still gets its update
        install(version = "2", artwork = "new artwork")
        assertEquals("new artwork kiwi.svg", contentsOf("kiwi.svg"))
    }

    @Test
    fun failedInvalidationDoesNotBreakTheInstallAndIsTriedAgain() {
        // Throwing, or just saying it couldn't reach the thumbnails, as when the external cache directory isn't available
        assertEquals(allFileNames, install(invalidation = { throw IOException("No cache") }))
        assertEquals(allFileNames, install(invalidation = { false }))

        // Neither marked the version as installed, so this one still invalidates everything
        assertEquals(allFileNames, install())
        assertEquals(allIds, invalidated)

        assertEquals(allFileNames, install())
        assertEquals(emptyList<UUID>(), invalidated)
    }

    @Test
    fun recopyRemovesWhatIsNoLongerListed() {
        install(version = "1")
        File(directory, "retired.svg").writeText("old artwork")
        File(directory, "hex.svg.123.tmp").writeText("left by a kill")
        File(directory, "folder").mkdir()
        val outsideFile = File(directory.parentFile, "outside.svg").apply { writeText("not a built-in") }

        install(version = "2")

        assertEquals((allFileNames + "version").toSet(), directory.list()?.toSet())
        assertTrue(outsideFile.isFile)
        assertTrue(invalidated.contains(BuiltInBackgrounds.stableId("retired.svg")))
    }

    @Test
    fun sameVersionLeavesUnlistedFilesAlone() {
        install()
        val unlistedFile = File(directory, "retired.svg").apply { writeText("old artwork") }

        install()

        assertTrue(unlistedFile.isFile)
    }

    @Test
    fun retryOnlyWhenSomeAreMissingAndTheCooldownHasPassed() {
        val total = BuiltInBackgrounds.all.size
        val lastAttempt = TimeUnit.MINUTES.toNanos(5)
        val afterCooldown = lastAttempt + TimeUnit.MINUTES.toNanos(1)
        val beforeCooldown = lastAttempt + TimeUnit.SECONDS.toNanos(1)

        assertTrue(BuiltInBackgroundInstaller.shouldRetryInstall(total - 1, afterCooldown, lastAttempt))
        assertTrue(BuiltInBackgroundInstaller.shouldRetryInstall(0, afterCooldown, lastAttempt))
        assertFalse(BuiltInBackgroundInstaller.shouldRetryInstall(total - 1, beforeCooldown, lastAttempt))
        assertFalse(BuiltInBackgroundInstaller.shouldRetryInstall(total, afterCooldown, lastAttempt))
    }
}
