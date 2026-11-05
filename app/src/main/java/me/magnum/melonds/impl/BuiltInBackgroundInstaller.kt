package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.BuiltInBackground
import me.magnum.melonds.domain.model.BuiltInBackgrounds
import me.magnum.melonds.utils.writeFileAtomically
import java.io.File
import java.io.InputStream
import java.util.UUID
import java.util.concurrent.TimeUnit

/**
 * Keeps a copy of each built-in background in [directory], so they're plain files that everything reading a background's URI can open. A file is
 * copied when it's missing or when [versionStamp] differs from the one of the last full copy, which is how an app update ships new artwork. A new
 * stamp also clears out whatever the current list doesn't have, so a retired background leaves nothing behind.
 *
 * @param openAsset Opens a built-in background's file from the app's assets, given its file name
 * @param invalidateThumbnail Drops the thumbnail of the background with this ID because its file was replaced or removed, and returns whether it could
 * be sure of that
 */
internal class BuiltInBackgroundInstaller(
    private val directory: File,
    private val versionStamp: String,
    private val openAsset: (fileName: String) -> InputStream,
    private val invalidateThumbnail: (UUID) -> Boolean,
) {
    companion object {
        private const val STAMP_FILE = "version"
        private const val SVG_EXTENSION = ".svg"
        private val INSTALL_RETRY_COOLDOWN_NANOS = TimeUnit.SECONDS.toNanos(30)

        /**
         * Whether to try again after an install that left some built-in backgrounds out, given how many it got and when it was attempted. The cooldown
         * keeps a disk that stays full from being tried again on every lookup.
         */
        fun shouldRetryInstall(installedCount: Int, nowNanos: Long, lastAttemptNanos: Long): Boolean {
            return installedCount < BuiltInBackgrounds.all.size && nowNanos - lastAttemptNanos >= INSTALL_RETRY_COOLDOWN_NANOS
        }
    }

    /**
     * Does the copying, which reads and writes files, so it must not run on the main thread. Returns the backgrounds whose file is in place, in
     * their listed order. One that failed to copy keeps its old file if it has one, and is tried again the next time.
     */
    fun install(): List<Pair<BuiltInBackground, File>> {
        directory.mkdirs()
        val stampFile = File(directory, STAMP_FILE)
        val isUpToDate = runCatching { stampFile.readText() == versionStamp }.getOrDefault(false)

        // The stamp says everything is done, so it's held back if a copy failed or an old thumbnail may have survived it
        var isComplete = true
        val installedBackgrounds = BuiltInBackgrounds.all.mapNotNull { background ->
            val file = File(directory, background.fileName)
            if (!isUpToDate || !file.isFile) {
                if (!copyAsset(background.fileName, file) || !tryInvalidateThumbnail(background.id)) {
                    isComplete = false
                }
            }
            if (file.isFile) background to file else null
        }

        if (!isUpToDate) {
            removeRetiredFiles()
            if (isComplete) {
                runCatching { stampFile.writeText(versionStamp) }.onFailure { it.printStackTrace() }
            }
        }
        return installedBackgrounds
    }

    private fun copyAsset(fileName: String, destination: File): Boolean {
        return runCatching {
            openAsset(fileName).use { input ->
                writeFileAtomically(destination) { output ->
                    input.copyTo(output)
                    true
                }
            }
        }.onFailure {
            it.printStackTrace()
        }.getOrDefault(false)
    }

    private fun tryInvalidateThumbnail(backgroundId: UUID): Boolean {
        return runCatching { invalidateThumbnail(backgroundId) }.onFailure { it.printStackTrace() }.getOrDefault(false)
    }

    /**
     * Deletes everything in the directory that isn't the stamp or a current background: retired backgrounds, and temporary files left by a kill.
     */
    private fun removeRetiredFiles() {
        val currentFileNames = BuiltInBackgrounds.all.mapTo(HashSet()) { it.fileName }
        directory.listFiles().orEmpty().filter { it.name != STAMP_FILE && it.name !in currentFileNames }.forEach { file ->
            if (file.name.endsWith(SVG_EXTENSION)) {
                tryInvalidateThumbnail(BuiltInBackgrounds.stableId(file.name))
            }
            runCatching { file.deleteRecursively() }.onFailure { it.printStackTrace() }
        }
    }
}
