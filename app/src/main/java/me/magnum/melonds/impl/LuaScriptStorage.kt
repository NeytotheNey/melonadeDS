package me.magnum.melonds.impl

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream

/**
 * Reads the games' Lua scripts from the folder linked to each game (see RomModsConfig.luaScriptFolder). The app only ever reads that
 * folder: scripts are added and removed by the user, outside the app. The ROM details screen lists the folder, and the emulator reads the
 * enabled scripts at launch. Native never opens script files itself.
 */
class LuaScriptStorage(private val context: Context) {

    /**
     * A `.lua` file at the top of a linked folder
     */
    class ScriptFile(val name: String, val uri: Uri)

    sealed class ReadResult {
        class Success(val source: ByteArray) : ReadResult()
        data object FolderNotFound : ReadResult()
        data object FileNotFound : ReadResult()
        data object TooLarge : ReadResult()
        data object Failed : ReadResult()
    }

    fun isLuaScriptName(fileName: String): Boolean {
        return fileName.endsWith(LUA_EXTENSION, ignoreCase = true)
    }

    /**
     * The `.lua` files at the top of the folder, in no particular order. Subfolders are skipped. Returns null if the folder can't be read:
     * the permission was lost, it was moved or deleted, it's on a card that isn't in, or a cloud provider is still fetching it
     */
    fun listScripts(folderUri: Uri): List<ScriptFile>? {
        return try {
            // Some providers list a missing folder as empty, which would drop every script, so the folder itself is checked first.
            // isDirectory is also false when the permission is gone
            val folder = DocumentFile.fromTreeUri(context, folderUri)?.takeIf { it.isDirectory } ?: return null
            // DocumentFile asks the provider once per file for its name and once for its type, which is slow in a big folder, so the
            // children are queried in one go instead
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(folder.uri, DocumentsContract.getDocumentId(folder.uri))
            val projection = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
            )
            context.contentResolver.query(childrenUri, projection, null, null, null)?.use { cursor ->
                // Cloud providers can answer with part of the list (or none of it) while they fetch the rest, or with an error. That
                // isn't the folder's real content, so the folder counts as unreadable and the script list is kept
                val extras = cursor.extras
                if (extras?.getBoolean(DocumentsContract.EXTRA_LOADING) == true || extras?.getString(DocumentsContract.EXTRA_ERROR) != null) {
                    return null
                }
                buildList {
                    while (cursor.moveToNext()) {
                        val documentId = cursor.getString(0) ?: continue
                        val name = cursor.getString(1) ?: continue
                        if (cursor.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR || !isLuaScriptName(name)) {
                            continue
                        }
                        add(ScriptFile(name, DocumentsContract.buildDocumentUriUsingTree(folder.uri, documentId)))
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Could not list the script folder", e)
            null
        }
    }

    /**
     * Reads scripts for the emulator, in the order given. The folder is listed once, and each script is looked up in it by name
     *
     * @param folderUri The game's script folder. With none, every script is [ReadResult.FolderNotFound]
     */
    fun readScripts(folderUri: Uri?, fileNames: List<String>): List<Pair<String, ReadResult>> {
        val scriptFiles = folderUri?.let { listScripts(it) }?.associateBy { it.name }
        return fileNames.map { fileName ->
            val result = if (scriptFiles == null) {
                ReadResult.FolderNotFound
            } else {
                scriptFiles[fileName]?.let { readScript(it) } ?: ReadResult.FileNotFound
            }
            if (result !is ReadResult.Success) {
                Log.w(TAG, "Skipping Lua script $fileName: $result")
            }
            fileName to result
        }
    }

    private fun readScript(scriptFile: ScriptFile): ReadResult {
        return try {
            // One byte over the limit is enough to know the file is too large, so a huge file is never read whole
            val content = context.contentResolver.openInputStream(scriptFile.uri)?.use { readAtMost(it, MAX_SCRIPT_SIZE + 1) } ?: return ReadResult.Failed
            if (content.size > MAX_SCRIPT_SIZE) ReadResult.TooLarge else ReadResult.Success(content)
        } catch (e: Exception) {
            Log.w(TAG, "Could not read Lua script ${scriptFile.name}", e)
            ReadResult.Failed
        }
    }

    /**
     * The script's description from its header (see [parseDescription]), or null if it has none or can't be read. Only the start of the
     * file is read
     */
    fun readDescription(scriptFile: ScriptFile): String? {
        return try {
            context.contentResolver.openInputStream(scriptFile.uri)?.use { parseDescription(readAtMost(it, DESCRIPTION_READ_LIMIT)) }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Finds a script's description in its leading comment block (the comments before any code, after an optional `#!` line). A
     * `-- description: <text>` line wins, otherwise the block's first non-empty line is used. No Lua runs here. Returns null without a
     * leading comment
     */
    fun parseDescription(source: ByteArray): String? {
        val lines = source.copyOf(minOf(source.size, DESCRIPTION_READ_LIMIT)).decodeToString().removePrefix("﻿").lines()
        var firstLine: String? = null
        var isInBlockComment = false

        for ((index, rawLine) in lines.withIndex()) {
            val line = rawLine.trim()
            val text = when {
                isInBlockComment -> {
                    val closerIndex = BLOCK_COMMENT_CLOSER.find(line)?.range?.first
                    if (closerIndex != null) {
                        isInBlockComment = false
                    }
                    if (closerIndex != null) line.substring(0, closerIndex) else line
                }
                index == 0 && line.startsWith("#!") -> continue
                line.isEmpty() -> continue
                line.startsWith("--") -> {
                    val opener = BLOCK_COMMENT_OPENER.find(line)
                    if (opener != null) {
                        val rest = line.substring(opener.range.last + 1)
                        val closerIndex = BLOCK_COMMENT_CLOSER.find(rest)?.range?.first
                        isInBlockComment = closerIndex == null
                        if (closerIndex != null) rest.substring(0, closerIndex) else rest
                    } else {
                        // Also turns separator lines like "------" into nothing
                        line.trimStart('-')
                    }
                }
                // Code: the leading comment block is over
                else -> break
            }.trim()

            if (text.isEmpty()) {
                continue
            }
            val descriptionMatch = DESCRIPTION_KEY.matchEntire(text)
            if (descriptionMatch != null) {
                // An empty description line is skipped, and is never used as the fallback either
                val description = descriptionMatch.groupValues[1].trim()
                if (description.isNotEmpty()) {
                    return capDescription(description)
                }
                continue
            }
            // Banner lines like "-- ==========" have no letters or digits, so they aren't a description
            if (firstLine == null && text.any { it.isLetterOrDigit() }) {
                firstLine = text
            }
        }

        return firstLine?.let { capDescription(it) }
    }

    private fun capDescription(text: String): String {
        if (text.length <= MAX_DESCRIPTION_LENGTH) {
            return text
        }

        var cut = MAX_DESCRIPTION_LENGTH - 1
        // Don't split a surrogate pair
        if (Character.isHighSurrogate(text[cut - 1]) && Character.isLowSurrogate(text[cut])) {
            cut--
        }
        return text.take(cut).trimEnd() + "…"
    }

    /**
     * Older versions copied picked scripts into `files/scripts/`. Scripts are read from a linked folder now, and those copies were only ever
     * made by the app, so they're deleted. Does nothing once they're gone
     */
    fun deleteOldScriptCopies() {
        val oldScriptsFolder = File(context.filesDir, OLD_SCRIPTS_FOLDER)
        if (oldScriptsFolder.exists() && !oldScriptsFolder.deleteRecursively()) {
            Log.w(TAG, "Could not delete the old script copies")
        }
    }

    private fun readAtMost(inputStream: InputStream, limit: Int): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (output.size() < limit) {
            val read = inputStream.read(buffer, 0, minOf(buffer.size, limit - output.size()))
            if (read < 0) {
                break
            }
            output.write(buffer, 0, read)
        }
        return output.toByteArray()
    }

    companion object {
        private const val TAG = "melonDS-lua"
        private const val OLD_SCRIPTS_FOLDER = "scripts"
        private const val LUA_EXTENSION = ".lua"
        const val MAX_SCRIPT_SIZE = 1024 * 1024

        // Script headers
        private const val DESCRIPTION_READ_LIMIT = 4 * 1024
        private const val MAX_DESCRIPTION_LENGTH = 120
        private val DESCRIPTION_KEY = Regex("description\\s*:\\s*(.*)", RegexOption.IGNORE_CASE)
        private val BLOCK_COMMENT_OPENER = Regex("^--\\[=*\\[")
        private val BLOCK_COMMENT_CLOSER = Regex("]=*]")
    }
}
