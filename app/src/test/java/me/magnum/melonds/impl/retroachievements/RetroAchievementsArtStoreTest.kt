package me.magnum.melonds.impl.retroachievements

import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtStore.SaveResult
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class RetroAchievementsArtStoreTest {

    private val png = byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A)

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private val rootDir: File by lazy { File(temporaryFolder.root, "ra_art") }
    private val store by lazy { RetroAchievementsArtStore { rootDir } }

    @Test
    fun filesAreNamedFromTheGameIdAndKindOnly() {
        assertEquals(SaveResult.SAVED, store.save(12, RetroAchievementsArtKind.BOX_ART, png))
        assertEquals(SaveResult.SAVED, store.save(12, RetroAchievementsArtKind.TITLE, png))
        assertEquals(SaveResult.SAVED, store.save(13, RetroAchievementsArtKind.ICON, png))

        assertEquals(setOf("12_box_art.png", "12_title.png", "13_icon.png"), rootDir.list()!!.toSet())
    }

    @Test
    fun savedBytesCanBeReadBack() {
        store.save(1, RetroAchievementsArtKind.ICON, png)

        assertArrayEquals(png, store.findArt(1)!!.readBytes())
        assertTrue(store.exists(1, RetroAchievementsArtKind.ICON))
        assertFalse(store.exists(1, RetroAchievementsArtKind.BOX_ART))
    }

    @Test
    fun savingAgainReplacesTheFile() {
        val longerPng = png + byteArrayOf(1, 2, 3)

        store.save(1, RetroAchievementsArtKind.ICON, png)
        store.save(1, RetroAchievementsArtKind.ICON, longerPng)

        assertArrayEquals(longerPng, store.findArt(1)!!.readBytes())
        assertEquals(1, rootDir.list()!!.size)
    }

    @Test
    fun theBestKindIsFound() {
        store.save(1, RetroAchievementsArtKind.ICON, png)
        assertEquals("1_icon.png", store.findArt(1)!!.name)

        store.save(1, RetroAchievementsArtKind.TITLE, png)
        assertEquals("1_title.png", store.findArt(1)!!.name)

        store.save(1, RetroAchievementsArtKind.BOX_ART, png)
        assertEquals("1_box_art.png", store.findArt(1)!!.name)
    }

    @Test
    fun oneKindIsFoundWhateverElseAGameHas() {
        store.save(1, RetroAchievementsArtKind.BOX_ART, png)
        store.save(1, RetroAchievementsArtKind.TITLE, png)
        store.save(2, RetroAchievementsArtKind.BOX_ART, png)

        assertEquals(mapOf(1L to File(rootDir, "1_title.png")), store.findArt(listOf(1L, 2L, 3L), RetroAchievementsArtKind.TITLE))
        assertEquals(emptyMap<Long, File>(), RetroAchievementsArtStore { null }.findArt(listOf(1L), RetroAchievementsArtKind.TITLE))
    }

    @Test
    fun clearingRemovesEveryKind() {
        store.save(1, RetroAchievementsArtKind.BOX_ART, png)
        store.save(1, RetroAchievementsArtKind.TITLE, png)

        store.clear()

        assertFalse(store.exists(1, RetroAchievementsArtKind.BOX_ART))
        assertFalse(store.exists(1, RetroAchievementsArtKind.TITLE))
    }

    @Test
    fun anythingThatIsNotAnImageIsRefused() {
        assertEquals(SaveResult.NOT_AN_IMAGE, store.save(1, RetroAchievementsArtKind.ICON, "<html>Not found</html>".toByteArray()))
        assertEquals(SaveResult.NOT_AN_IMAGE, store.save(1, RetroAchievementsArtKind.ICON, ByteArray(0)))
        assertNull(store.findArt(1))
    }

    @Test
    fun theCommonImageFormatsAreAccepted() {
        val jpeg = byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte(), 0xE0.toByte())
        val gif = "GIF89a".toByteArray()
        val webp = "RIFF".toByteArray() + ByteArray(4) + "WEBP".toByteArray()

        listOf(jpeg, gif, webp).forEach {
            assertEquals(SaveResult.SAVED, store.save(1, RetroAchievementsArtKind.ICON, it))
        }
    }

    @Test
    fun deletingOnlyAffectsThatGame() {
        store.save(1, RetroAchievementsArtKind.BOX_ART, png)
        store.save(1, RetroAchievementsArtKind.ICON, png)
        store.save(2, RetroAchievementsArtKind.ICON, png)

        store.deleteArt(1)

        assertNull(store.findArt(1))
        assertEquals("2_icon.png", store.findArt(2)!!.name)
    }

    @Test
    fun clearRemovesEverything() {
        store.save(1, RetroAchievementsArtKind.ICON, png)
        store.save(2, RetroAchievementsArtKind.ICON, png)

        store.clear()

        assertNull(store.findArt(1))
        assertNull(store.findArt(2))
        // The folder comes back with the next file
        assertEquals(SaveResult.SAVED, store.save(1, RetroAchievementsArtKind.ICON, png))
    }

    @Test
    fun aDownloadThatStartedBeforeAClearCannotPutArtBack() {
        val clearCountAtStart = store.clearCount

        store.clear()

        assertEquals(SaveResult.DISCARDED, store.save(1, RetroAchievementsArtKind.ICON, png, clearCountAtStart))
        assertNull(store.findArt(1))
        assertEquals(SaveResult.SAVED, store.save(1, RetroAchievementsArtKind.ICON, png, store.clearCount))
    }

    @Test
    fun severalGamesAreFoundFromOneLookThroughTheFolder() {
        store.save(1, RetroAchievementsArtKind.ICON, png)
        store.save(1, RetroAchievementsArtKind.BOX_ART, png)
        store.save(2, RetroAchievementsArtKind.TITLE, png)

        val art = store.findArt(listOf(1, 2, 3))

        assertEquals(setOf(1L, 2L), art.keys)
        assertEquals("1_box_art.png", art.getValue(1).name)
        assertEquals("2_title.png", art.getValue(2).name)
    }

    @Test
    fun aMissingFolderMeansNoArtRatherThanAnError() {
        assertTrue(store.findArt(listOf(1)).isEmpty())
    }

    @Test
    fun changesBumpTheVersion() {
        val initial = store.version.value

        store.save(1, RetroAchievementsArtKind.ICON, png)
        val afterSave = store.version.value
        assertNotEquals(initial, afterSave)

        store.save(1, RetroAchievementsArtKind.ICON, "not an image".toByteArray())
        assertEquals(afterSave, store.version.value)

        store.clear()
        assertNotEquals(afterSave, store.version.value)
    }

    @Test
    fun withNoStorageNothingIsSavedOrFound() {
        val noStorage = RetroAchievementsArtStore { null }

        assertFalse(noStorage.isAvailable())
        assertEquals(SaveResult.FAILED, noStorage.save(1, RetroAchievementsArtKind.ICON, png))
        assertNull(noStorage.findArt(1))
        assertFalse(noStorage.exists(1, RetroAchievementsArtKind.ICON))
    }
}
