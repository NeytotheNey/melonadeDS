package me.magnum.melonds.impl.diagnostics

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import me.magnum.melonds.domain.repositories.SettingsRepository
import java.io.BufferedWriter
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.io.OutputStreamWriter

/**
 * Creates files in the log folder picked in the performance settings, or in the default log directory if no folder was picked or it can't be
 * written to.
 */
class LogFolder(private val context: Context, private val settingsRepository: SettingsRepository) {

    companion object {
        private const val TAG = "LogFolder"
        private const val LOG_DIRECTORY_NAME = "perf_logs"
        const val TEXT_MIME_TYPE = "text/plain"
        // A generic MIME type prevents providers from changing the extension of the file
        const val GENERIC_MIME_TYPE = "application/octet-stream"

        /**
         * Directory used when the user hasn't picked a log folder.
         */
        fun getDefaultLogDirectory(context: Context): File {
            return context.getExternalFilesDir(LOG_DIRECTORY_NAME) ?: File(context.filesDir, LOG_DIRECTORY_NAME)
        }
    }

    /**
     * Creates [fileName] and opens it for writing; a [mimeType] of "text/plain" keeps a .txt name, other extensions need a generic type.
     */
    fun createFile(fileName: String, mimeType: String = TEXT_MIME_TYPE): OutputStream {
        return resolve().openStream(fileName, mimeType)
    }

    /**
     * Picks the folder to write to: the one from the settings if it can be written to, otherwise the default log directory.
     */
    internal fun resolve(): LogDestination {
        val selectedDirectory = settingsRepository.getPerformanceLogDirectory()
        if (selectedDirectory != null) {
            val directoryDocument = try {
                DocumentFile.fromTreeUri(context, selectedDirectory)?.takeIf { it.isDirectory && it.canWrite() }
            } catch (e: Exception) {
                null
            }

            if (directoryDocument != null) {
                return DocumentTreeDestination(context, directoryDocument)
            } else {
                Log.w(TAG, "Cannot write to the picked log folder. Using the default log directory instead")
            }
        }

        val defaultDirectory = getDefaultLogDirectory(context)
        if (!defaultDirectory.isDirectory && !defaultDirectory.mkdirs()) {
            throw IOException("Could not create ${defaultDirectory.absolutePath}")
        }
        return FileDestination(defaultDirectory)
    }
}

/**
 * The folder picked by [LogFolder.resolve], which log files are created in.
 */
internal interface LogDestination {
    val description: String

    fun openStream(fileName: String, mimeType: String = LogFolder.GENERIC_MIME_TYPE): OutputStream

    /**
     * Writes [content] to [fileName], replacing the previous contents if the file was already written by this destination.
     */
    fun writeReplacing(fileName: String, content: String)

    fun openWriter(fileName: String): BufferedWriter {
        return BufferedWriter(OutputStreamWriter(openStream(fileName), Charsets.UTF_8), 64 * 1024)
    }
}

private class DocumentTreeDestination(private val context: Context, private val directory: DocumentFile) : LogDestination {
    override val description: String = directory.uri.toString()
    private val createdDocuments = mutableMapOf<String, Uri>()

    override fun openStream(fileName: String, mimeType: String): OutputStream {
        val documentUri = createDocument(fileName, mimeType)
        return context.contentResolver.openOutputStream(documentUri, "w") ?: throw IOException("Could not open $fileName")
    }

    override fun writeReplacing(fileName: String, content: String) {
        val existingDocument = createdDocuments[fileName]
        if (existingDocument != null) {
            // "wt" truncates the file, and providers that don't support it get the document recreated
            val outputStream = runCatching { context.contentResolver.openOutputStream(existingDocument, "wt") }.getOrNull()
            if (outputStream != null) {
                outputStream.use { it.write(content.toByteArray(Charsets.UTF_8)) }
                return
            }

            DocumentFile.fromSingleUri(context, existingDocument)?.delete()
            createdDocuments.remove(fileName)
        }

        openStream(fileName).use { it.write(content.toByteArray(Charsets.UTF_8)) }
    }

    private fun createDocument(fileName: String, mimeType: String): Uri {
        val document = directory.createFile(mimeType, fileName) ?: throw IOException("Could not create $fileName")
        createdDocuments[fileName] = document.uri
        return document.uri
    }
}

private class FileDestination(private val directory: File) : LogDestination {
    override val description: String = directory.absolutePath

    override fun openStream(fileName: String, mimeType: String): OutputStream {
        return File(directory, fileName).outputStream()
    }

    override fun writeReplacing(fileName: String, content: String) {
        File(directory, fileName).writeText(content, Charsets.UTF_8)
    }
}
