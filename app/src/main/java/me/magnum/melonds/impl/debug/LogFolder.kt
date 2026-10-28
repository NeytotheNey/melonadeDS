package me.magnum.melonds.impl.debug

import android.content.Context
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.perf.PerformanceLogger
import java.io.File
import java.io.IOException
import java.io.OutputStream

/**
 * Creates files in the log folder picked in the performance settings (the same one the performance logger writes to), or in the default log
 * directory if no folder was picked or it can't be written to.
 */
class LogFolder(private val context: Context, private val settingsRepository: SettingsRepository) {

    private companion object {
        const val TAG = "LogFolder"
    }

    class LogFile(val outputStream: OutputStream)

    /**
     * Creates [fileName] and opens it for writing.
     *
     * @param mimeType MIME type for the document provider. "text/plain" keeps a .txt name as is, a generic type keeps any other extension
     */
    fun createFile(fileName: String, mimeType: String = "text/plain"): LogFile {
        val selectedDirectory = settingsRepository.getPerformanceLogDirectory()
        if (selectedDirectory != null) {
            val directoryDocument = try {
                DocumentFile.fromTreeUri(context, selectedDirectory)?.takeIf { it.isDirectory && it.canWrite() }
            } catch (e: Exception) {
                null
            }

            if (directoryDocument != null) {
                val document = directoryDocument.createFile(mimeType, fileName) ?: throw IOException("Could not create $fileName")
                val outputStream = context.contentResolver.openOutputStream(document.uri, "w") ?: throw IOException("Could not open $fileName")
                return LogFile(outputStream)
            } else {
                Log.w(TAG, "Cannot write to the picked log folder. Using the default log directory instead")
            }
        }

        val defaultDirectory = PerformanceLogger.getDefaultLogDirectory(context)
        if (!defaultDirectory.isDirectory && !defaultDirectory.mkdirs()) {
            throw IOException("Could not create ${defaultDirectory.absolutePath}")
        }
        return LogFile(File(defaultDirectory, fileName).outputStream())
    }
}
