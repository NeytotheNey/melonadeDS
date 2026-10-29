package me.magnum.melonds.impl

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import me.magnum.melonds.domain.model.rom.Rom
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.security.MessageDigest

/**
 * The games' Lua scripts in app storage, in `files/scripts/<folder>/<file name>`, where the folder comes from [getScriptFolderName]. The ROM
 * details screen adds and removes them, and the emulator reads the enabled ones at launch. Native never opens script files itself.
 */
class LuaScriptStorage(private val context: Context) {

    sealed class ImportResult {
        data object Success : ImportResult()
        data object NotLuaScript : ImportResult()
        data object TooLarge : ImportResult()
        data object Failed : ImportResult()
    }

    /**
     * The folder that holds the ROM's scripts. A real game code is used as is, so the scripts survive the ROM being moved. Homebrew often keeps
     * a placeholder code (ndstool's `####`, or zeros), and ROMs that share one have separate configs, so those get a folder of their own, keyed
     * by the ROM's URI like its config is. [gameCode] is null if the header couldn't be read
     */
    fun getScriptFolderName(rom: Rom, gameCode: String?): String {
        if (gameCode != null && GAME_CODE_REGEX.matches(gameCode) && gameCode !in PLACEHOLDER_GAME_CODES) {
            return gameCode
        }

        val uriHash = MessageDigest.getInstance("SHA-256").digest(rom.uri.toString().toByteArray())
        return HOMEBREW_FOLDER_PREFIX + uriHash.take(8).joinToString("") { "%02x".format(it) }
    }

    /**
     * The name of a file picked with the system picker, or null if the provider doesn't say
     */
    fun getPickedFileName(uri: Uri): String? {
        return DocumentFile.fromSingleUri(context, uri)?.name
    }

    fun isLuaScriptName(fileName: String): Boolean {
        return fileName.endsWith(LUA_EXTENSION, ignoreCase = true)
    }

    fun scriptExists(folderName: String, fileName: String): Boolean {
        return getScriptFile(folderName, fileName)?.isFile == true
    }

    /**
     * Reads a script for the emulator. Returns null if the file is missing, unreadable or over [MAX_SCRIPT_SIZE]
     */
    fun readScript(folderName: String, fileName: String): ByteArray? {
        val file = getScriptFile(folderName, fileName)
        if (file == null) {
            Log.w(TAG, "Skipping Lua script $fileName: not inside the game's script folder")
            return null
        }
        if (!file.isFile) {
            Log.d(TAG, "Skipping Lua script $fileName: file not found")
            return null
        }
        if (file.length() > MAX_SCRIPT_SIZE) {
            Log.w(TAG, "Skipping Lua script $fileName: over $MAX_SCRIPT_SIZE bytes")
            return null
        }

        return try {
            file.readBytes()
        } catch (e: Exception) {
            Log.w(TAG, "Skipping Lua script $fileName: could not be read", e)
            null
        }
    }

    /**
     * The script's description from its header (see [parseDescription]), or null if it has none or can't be read. Only the start of the
     * file is read
     */
    fun readDescription(folderName: String, fileName: String): String? {
        val file = getScriptFile(folderName, fileName) ?: return null
        return try {
            file.inputStream().use { parseDescription(readAtMost(it, DESCRIPTION_READ_LIMIT)) }
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
     * Copies a picked file into the ROM's folder as [fileName], replacing a script with the same name. The old script is kept if the copy fails
     */
    fun importScript(folderName: String, sourceUri: Uri, fileName: String): ImportResult {
        if (!isLuaScriptName(fileName)) {
            return ImportResult.NotLuaScript
        }

        var tempFile: File? = null
        return try {
            val file = getScriptFile(folderName, fileName) ?: return ImportResult.Failed
            // One byte over the limit is enough to know the file is too large, so a huge file is never read whole
            val content = context.contentResolver.openInputStream(sourceUri)?.use { readAtMost(it, MAX_SCRIPT_SIZE + 1) } ?: return ImportResult.Failed
            if (content.size > MAX_SCRIPT_SIZE) {
                return ImportResult.TooLarge
            }

            val scriptFolder = file.parentFile!!
            if (!scriptFolder.isDirectory && !scriptFolder.mkdirs()) {
                return ImportResult.Failed
            }
            // Written next to it and renamed, so a failed copy never leaves half a script. The temp name is short, so it fits whatever the
            // script's name is, and doesn't end in .lua, so it can't clash with one
            tempFile = File.createTempFile(TEMP_PREFIX, TEMP_SUFFIX, scriptFolder)
            tempFile.writeBytes(content)
            if (tempFile.renameTo(file)) ImportResult.Success else ImportResult.Failed
        } catch (e: Exception) {
            Log.w(TAG, "Failed to import Lua script $fileName", e)
            ImportResult.Failed
        } finally {
            // Gone after a successful rename
            tempFile?.delete()
        }
    }

    /**
     * Returns true if the script's file is gone, including when it never existed
     */
    fun deleteScript(folderName: String, fileName: String): Boolean {
        return try {
            val file = getScriptFile(folderName, fileName) ?: return true
            !file.exists() || file.delete()
        } catch (e: Exception) {
            Log.w(TAG, "Failed to delete Lua script $fileName", e)
            false
        }
    }

    /**
     * The script's file, or null if [folderName] or [fileName] would point outside the ROM's folder. The names come from the ROM config, the
     * cart header and the file picker, so they can't be trusted
     */
    private fun getScriptFile(folderName: String, fileName: String): File? {
        return try {
            val scriptsRoot = File(context.filesDir, SCRIPTS_FOLDER).canonicalFile
            val scriptFolder = File(scriptsRoot, folderName).canonicalFile
            val file = File(scriptFolder, fileName).canonicalFile
            if (scriptFolder.parentFile == scriptsRoot && file.parentFile == scriptFolder) file else null
        } catch (e: IOException) {
            null
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
        private const val SCRIPTS_FOLDER = "scripts"
        private const val HOMEBREW_FOLDER_PREFIX = "homebrew-"
        private const val LUA_EXTENSION = ".lua"
        private const val TEMP_PREFIX = "lua"
        private const val TEMP_SUFFIX = ".tmp"
        const val MAX_SCRIPT_SIZE = 1024 * 1024

        // Retail codes are 4 uppercase letters or digits. Anything else (NUL bytes, spaces, ndstool's ####) is treated as a placeholder
        private val GAME_CODE_REGEX = Regex("[A-Z0-9]{4}")
        private val PLACEHOLDER_GAME_CODES = setOf("0000")

        // Script headers
        private const val DESCRIPTION_READ_LIMIT = 4 * 1024
        private const val MAX_DESCRIPTION_LENGTH = 120
        private val DESCRIPTION_KEY = Regex("description\\s*:\\s*(.*)", RegexOption.IGNORE_CASE)
        private val BLOCK_COMMENT_OPENER = Regex("^--\\[=*\\[")
        private val BLOCK_COMMENT_CLOSER = Regex("]=*]")
    }
}
