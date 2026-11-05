package me.magnum.melonds.impl

import android.content.Context
import androidx.core.net.toUri
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.common.Deletable
import me.magnum.melonds.common.filterNotDeleted
import me.magnum.melonds.domain.model.Background
import me.magnum.melonds.domain.model.BuiltInBackgrounds
import me.magnum.melonds.domain.repositories.BackgroundRepository
import me.magnum.melonds.utils.PackageManagerCompat
import me.magnum.melonds.utils.writeFileAtomically
import java.io.File
import java.io.FileReader
import java.lang.reflect.Type
import java.util.UUID

class InternalBackgroundsRepository(
    private val context: Context,
    private val gson: Gson,
    private val backgroundThumbnailProvider: BackgroundThumbnailProvider,
) : BackgroundRepository {
    companion object {
        private const val DATA_FILE = "backgrounds.json"
        private const val BUILT_IN_BACKGROUNDS_DIR = "backgrounds/builtin"
        private val backgroundListType: Type = object : TypeToken<List<Background>>(){}.type
    }

    private val backgroundLoadLock = Mutex()
    private var areBackgroundsLoaded = false
    // Only the user's backgrounds, which is what gets saved
    private val backgrounds = MutableStateFlow<List<Deletable<Background>>>(emptyList())
    // Set when loading, before the user's backgrounds are published, and again by a retry if some were left out
    private val builtInBackgrounds = MutableStateFlow<List<Background>>(emptyList())
    // Guarded by backgroundLoadLock
    private var lastInstallAttemptNanos = 0L

    override fun getBackgrounds(): Flow<List<Background>> {
        return combine(builtInBackgrounds, backgrounds) { builtIn, user ->
            BuiltInBackgrounds.merge(builtIn, user.filterNotDeleted()) { background -> background.id }
        }.onStart { ensureBackgroundsAreLoaded() }
    }

    override suspend fun getBackground(id: UUID): Background? {
        ensureBackgroundsAreLoaded()
        return builtInBackgrounds.value.firstOrNull { it.id == id }
            ?: backgrounds.value.firstOrNull { !it.isDeleted && it.data.id == id }?.data
    }

    override suspend fun addBackground(background: Background) {
        if (BuiltInBackgrounds.isBuiltIn(background.id)) {
            return
        }

        // Saving writes the whole list, so it has to have the saved backgrounds in it
        ensureBackgroundsAreLoaded()
        if (background.id == null) {
            val newBackground = background.copy(
                    id = UUID.randomUUID()
            )
            backgrounds.update {
                it.toMutableList().apply {
                    add(Deletable(newBackground, false))
                }
            }
        } else {
            val index = backgrounds.value.indexOfFirst { it.data.id == background.id }
            backgrounds.update {
                if (index >= 0) {
                    it.toMutableList().apply {
                        set(index, Deletable(background, false))
                    }
                } else {
                    it.toMutableList().apply {
                        add(Deletable(background, false))
                    }
                }
            }
        }
        saveBackgrounds()
    }

    override suspend fun deleteBackground(background: Background) {
        if (BuiltInBackgrounds.isBuiltIn(background.id)) {
            return
        }

        ensureBackgroundsAreLoaded()
        val backgroundToDelete = backgrounds.value.find { !it.isDeleted && it.data.id == background.id }
        if (backgroundToDelete != null) {
            backgrounds.update {
                val backgroundIndex = it.indexOf(backgroundToDelete)
                it.toMutableList().apply {
                    set(backgroundIndex, backgroundToDelete.copy(isDeleted = true))
                }
            }
            saveBackgrounds()
        }
    }

    private suspend fun ensureBackgroundsAreLoaded() = withContext(Dispatchers.IO) {
        backgroundLoadLock.withLock {
            if (areBackgroundsLoaded) {
                val installedCount = builtInBackgrounds.value.size
                if (BuiltInBackgroundInstaller.shouldRetryInstall(installedCount, System.nanoTime(), lastInstallAttemptNanos)) {
                    installBuiltInBackgrounds()
                }
            } else {
                installBuiltInBackgrounds()
                val deletableBackgrounds = loadBackgrounds().map { background -> Deletable(background, false) }
                backgrounds.value = deletableBackgrounds
                areBackgroundsLoaded = true
            }
        }
    }

    private fun installBuiltInBackgrounds() {
        lastInstallAttemptNanos = System.nanoTime()
        val installer = BuiltInBackgroundInstaller(
            directory = File(context.filesDir, BUILT_IN_BACKGROUNDS_DIR),
            versionStamp = getInstallStamp(),
            openAsset = { fileName -> context.assets.open("${BuiltInBackgrounds.ASSET_FOLDER}/$fileName") },
            invalidateThumbnail = backgroundThumbnailProvider::invalidateThumbnail,
        )
        builtInBackgrounds.value = installer.install().map { (background, file) -> Background(background.id, background.name, file.toUri()) }
    }

    // The update time is there because the version code stays the same across development builds, whose artwork must still get updated
    private fun getInstallStamp(): String {
        val lastUpdateTime = runCatching {
            PackageManagerCompat.getPackageInfo(context.packageManager, context.packageName, 0).lastUpdateTime
        }.getOrDefault(0L)
        return "${BuildConfig.VERSION_CODE}:$lastUpdateTime"
    }

    private fun loadBackgrounds(): List<Background> {
        val dataFile = File(context.filesDir, DATA_FILE)
        if (!dataFile.isFile) {
            return emptyList()
        }

        return try {
            val backgrounds = FileReader(dataFile).use { gson.fromJson<List<Background>>(it, backgroundListType) }
            backgrounds ?: emptyList()
        } catch (_: Exception) {
            emptyList()
        }
    }

    private suspend fun saveBackgrounds() = withContext(Dispatchers.IO) {
        val dataFile = File(context.filesDir, DATA_FILE)

        try {
            // Exclude deleted backgrounds
            val backgroundsToSave = backgrounds.value.filterNotDeleted()
            val layoutsJson = gson.toJson(backgroundsToSave)

            writeFileAtomically(dataFile) {
                it.write(layoutsJson.toByteArray(Charsets.UTF_8))
                true
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }
}