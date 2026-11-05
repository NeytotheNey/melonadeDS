package me.magnum.melonds.impl.retroachievements

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.io.File
import java.io.IOException

/**
 * The art kinds in the order a tile prefers them.
 */
enum class RetroAchievementsArtKind(val fileSuffix: String) {
    BOX_ART("box_art"),
    TITLE("title"),
    ICON("icon"),
}

/**
 * Downloaded art, one file per game and kind as `<game id>_<kind>.png`. Coil's own disk cache is an LRU that could evict it, so this store is
 * separate. File names never hold API text. The extension is fixed whatever the image's real format, since decoders go by its content.
 *
 * @param getRootDir The art's folder, or null while no storage is available
 */
class RetroAchievementsArtStore(private val getRootDir: () -> File?) {

    enum class SaveResult {
        SAVED,
        NOT_AN_IMAGE,
        /** The art was cleared after the download started. */
        DISCARDED,
        FAILED,
    }

    private val _version = MutableStateFlow(0L)
    private val clearLock = Any()

    /** Changes whenever art is added or removed. */
    val version: StateFlow<Long> = _version.asStateFlow()

    /** Take this before starting a download and hand it to [save], so that a download still in flight during [clear] can't put art back. */
    @Volatile
    var clearCount = 0
        private set

    fun isAvailable(): Boolean = getRootDir() != null

    fun exists(gameId: Long, kind: RetroAchievementsArtKind): Boolean {
        return fileFor(gameId, kind)?.isFile == true
    }

    /**
     * The best art there is for the game.
     */
    fun findArt(gameId: Long): File? {
        return findArt(listOf(gameId))[gameId]
    }

    /**
     * The best art there is for each of the games that have any, looking through the folder once rather than for every game.
     */
    fun findArt(gameIds: Collection<Long>): Map<Long, File> {
        val rootDir = getRootDir() ?: return emptyMap()
        val fileNames = rootDir.list()?.toSet() ?: return emptyMap()

        return gameIds.distinct().mapNotNull { gameId ->
            RetroAchievementsArtKind.entries
                .firstOrNull { fileName(gameId, it) in fileNames }
                ?.let { gameId to File(rootDir, fileName(gameId, it)) }
        }.toMap()
    }

    /**
     * The art of one [kind] for each of the games that have it, whatever else they have.
     */
    fun findArt(gameIds: Collection<Long>, kind: RetroAchievementsArtKind): Map<Long, File> {
        val rootDir = getRootDir() ?: return emptyMap()
        val fileNames = rootDir.list()?.toSet() ?: return emptyMap()

        return gameIds.distinct()
            .filter { fileName(it, kind) in fileNames }
            .associateWith { File(rootDir, fileName(it, kind)) }
    }

    /**
     * Stores [bytes] if they look like an image, so an error page saved by mistake never becomes a game's art. The file appears only once
     * it's complete, so an interrupted write can't count as already downloaded.
     *
     * @param expectedClearCount [clearCount] from before the download started
     */
    fun save(gameId: Long, kind: RetroAchievementsArtKind, bytes: ByteArray, expectedClearCount: Int = clearCount): SaveResult {
        if (!looksLikeImage(bytes)) {
            return SaveResult.NOT_AN_IMAGE
        }

        val file = fileFor(gameId, kind) ?: return SaveResult.FAILED
        val tempFile = File(file.parentFile, "${file.name}.tmp")
        return try {
            file.parentFile?.mkdirs()
            tempFile.writeBytes(bytes)
            val result = synchronized(clearLock) {
                when {
                    clearCount != expectedClearCount -> SaveResult.DISCARDED
                    // A failed rename can only mean the old file is still there on some file systems
                    tempFile.renameTo(file) || (file.delete() && tempFile.renameTo(file)) -> SaveResult.SAVED
                    else -> SaveResult.FAILED
                }
            }
            if (result == SaveResult.SAVED) {
                _version.update { it + 1 }
            }
            result
        } catch (_: IOException) {
            SaveResult.FAILED
        } finally {
            tempFile.delete()
        }
    }

    fun deleteArt(gameId: Long) {
        RetroAchievementsArtKind.entries.forEach { fileFor(gameId, it)?.delete() }
        _version.update { it + 1 }
    }

    fun clear() {
        synchronized(clearLock) {
            clearCount++
            getRootDir()?.deleteRecursively()
        }
        _version.update { it + 1 }
    }

    private fun fileFor(gameId: Long, kind: RetroAchievementsArtKind): File? {
        return getRootDir()?.let { File(it, fileName(gameId, kind)) }
    }

    private fun fileName(gameId: Long, kind: RetroAchievementsArtKind) = "${gameId}_${kind.fileSuffix}.png"

    private fun looksLikeImage(bytes: ByteArray): Boolean {
        fun startsWith(vararg signature: Int, offset: Int = 0): Boolean {
            return bytes.size >= offset + signature.size && signature.indices.all { (bytes[offset + it].toInt() and 0xFF) == signature[it] }
        }

        val isPng = startsWith(0x89, 0x50, 0x4E, 0x47)
        val isJpeg = startsWith(0xFF, 0xD8, 0xFF)
        val isGif = startsWith(0x47, 0x49, 0x46, 0x38)
        val isWebp = startsWith(0x52, 0x49, 0x46, 0x46) && startsWith(0x57, 0x45, 0x42, 0x50, offset = 8)
        return isPng || isJpeg || isGif || isWebp
    }
}
