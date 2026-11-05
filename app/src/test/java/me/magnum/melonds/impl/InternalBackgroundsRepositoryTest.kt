package me.magnum.melonds.impl

import android.content.ContextWrapper
import com.google.gson.Gson
import kotlinx.coroutines.runBlocking
import me.magnum.melonds.domain.model.Background
import me.magnum.melonds.domain.model.BuiltInBackgrounds
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.util.UUID

class InternalBackgroundsRepositoryTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    private val filesDir by lazy { tempFolder.newFolder("files") }
    private val dataFile by lazy { File(filesDir, "backgrounds.json") }

    // Only filesDir is used: the built-in backgrounds' copies fail on the rest of the Context being stubbed, which leaves them out, and that's fine here
    private val context by lazy {
        object : ContextWrapper(null) {
            override fun getFilesDir(): File = this@InternalBackgroundsRepositoryTest.filesDir
        }
    }

    private val gson = Gson()
    private val repository by lazy { InternalBackgroundsRepository(context, gson, BackgroundThumbnailProvider(context)) }

    // Uri can't be created in local unit tests, and these only need the ID; Gson builds a Background without one
    private fun background(id: UUID): Background {
        return gson.fromJson("""{"id":"$id","name":"Background"}""", Background::class.java)
    }

    private val builtInId = BuiltInBackgrounds.all.first().id

    @Test
    fun builtInIsNotSavedWhenAdded() = runBlocking {
        repository.addBackground(background(builtInId))

        assertFalse(dataFile.exists())
    }

    @Test
    fun builtInIsNotDeleted() = runBlocking {
        // Saved directly so there's something a delete would remove from the file, if it went through
        val savedData = gson.toJson(listOf(background(builtInId)))
        dataFile.writeText(savedData)

        repository.deleteBackground(background(builtInId))

        assertEquals(savedData, dataFile.readText())
    }

    @Test
    fun addingToAnUnloadedRepositoryKeepsTheSavedBackgrounds() = runBlocking {
        val savedId = UUID.randomUUID()
        val newId = UUID.randomUUID()
        dataFile.writeText(gson.toJson(listOf(background(savedId))))

        repository.addBackground(background(newId))

        assertNotNull(repository.getBackground(savedId))
        assertNotNull(repository.getBackground(newId))
        // A fresh repository only has what reached the file
        val reloadedRepository = InternalBackgroundsRepository(context, gson, BackgroundThumbnailProvider(context))
        assertNotNull(reloadedRepository.getBackground(savedId))
        assertNotNull(reloadedRepository.getBackground(newId))
    }

    @Test
    fun savingLeavesNoTemporaryFileNextToTheData() = runBlocking {
        repository.addBackground(background(UUID.randomUUID()))

        assertEquals(emptyList<String>(), filesDir.list().orEmpty().filter { it.endsWith(".tmp") })
    }

    @Test
    fun userBackgroundsCanStillBeAddedAndDeleted() = runBlocking {
        val id = UUID.randomUUID()

        repository.addBackground(background(id))
        assertNotNull(repository.getBackground(id))

        repository.deleteBackground(background(id))
        assertNull(repository.getBackground(id))
        assertEquals("[]", dataFile.readText())
    }
}
