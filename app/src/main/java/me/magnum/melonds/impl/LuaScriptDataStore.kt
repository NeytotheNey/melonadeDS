package me.magnum.melonds.impl

import android.content.Context
import android.util.Log
import java.io.File
import java.io.IOException
import java.security.MessageDigest

/**
 * Stores the blobs scripts save with melon.storage under `files/lua-data/<game key>/<script>.bin`; writes are atomic and the data outlives the
 * script leaving the folder.
 */
class LuaScriptDataStore(private val context: Context) {

    private val lock = Any()
    // Game key of the session running scripts, whose data can't be deleted meanwhile because native would write it back
    @Volatile
    private var sessionGameKey: String? = null

    /**
     * Sets the game key of the session that's running scripts, or null when none is
     */
    fun setSessionGameKey(gameKey: String?) {
        sessionGameKey = gameKey
    }

    /**
     * The saved data of [scriptName], or null if there's none or it can't be read
     */
    fun read(gameKey: String, scriptName: String): ByteArray? {
        synchronized(lock) {
            val file = getFile(gameKey, scriptName) ?: return null
            if (!file.isFile) {
                return null
            }
            return try {
                // Native never makes a blob this large, so a larger file is left for native to replace
                if (file.length() > MAX_DATA_SIZE) {
                    Log.w(LUA_LOG_TAG, "Saved data of $scriptName is too large, so it's not used")
                    null
                } else {
                    file.readBytes()
                }
            } catch (e: IOException) {
                Log.w(LUA_LOG_TAG, "Could not read the saved data of $scriptName", e)
                null
            }
        }
    }

    /**
     * Saves [data] for [scriptName], deleting the file when it's empty
     */
    fun write(gameKey: String, scriptName: String, data: ByteArray) {
        synchronized(lock) {
            val file = getFile(gameKey, scriptName) ?: return
            if (data.isEmpty()) {
                file.delete()
                return
            }

            val folder = file.parentFile ?: return
            var tempFile: File? = null
            try {
                folder.mkdirs()
                tempFile = File.createTempFile(TEMP_PREFIX, TEMP_SUFFIX, folder)
                tempFile.writeBytes(data)
                if (!tempFile.renameTo(file)) {
                    throw IOException("Could not rename the temp file")
                }
                tempFile = null
            } catch (e: IOException) {
                Log.w(LUA_LOG_TAG, "Could not save the data of $scriptName", e)
            } finally {
                tempFile?.delete()
            }
        }
    }

    /**
     * How many bytes [scriptName] has saved, 0 if nothing
     */
    fun getSize(gameKey: String, scriptName: String): Long {
        synchronized(lock) {
            return getFile(gameKey, scriptName)?.takeIf { it.isFile }?.length() ?: 0
        }
    }

    /**
     * Deletes [scriptName]'s data unless its game is running scripts (see [setSessionGameKey])
     */
    fun delete(gameKey: String, scriptName: String) {
        synchronized(lock) {
            if (gameKey == sessionGameKey) {
                Log.w(LUA_LOG_TAG, "Not deleting the saved data of $scriptName while its game is running")
                return
            }
            getFile(gameKey, scriptName)?.delete()
        }
    }

    private fun getFile(gameKey: String, scriptName: String): File? {
        // The game key comes from getGameKey(), but native hands it back, so it's checked before it becomes a path
        if (!GAME_KEY.matches(gameKey)) {
            return null
        }
        return File(File(File(context.filesDir, DATA_FOLDER), gameKey), getFileName(scriptName))
    }

    companion object {
        private const val DATA_FOLDER = "lua-data"
        private const val TEMP_PREFIX = "data"
        private const val TEMP_SUFFIX = ".tmp"
        private const val HOMEBREW_PREFIX = "homebrew-"
        private const val DATA_SUFFIX = ".bin"
        // Room for the suffix within the 255 bytes a file name can have
        private const val MAX_PLAIN_NAME_BYTES = 200

        /**
         * Native's limit for a saved blob
         */
        const val MAX_DATA_SIZE = 256 * 1024

        // Retail codes are 4 uppercase letters or digits, and anything else (NUL bytes, spaces, ndstool's ####) is a placeholder
        private val GAME_CODE = Regex("[A-Z0-9]{4}")
        private val PLACEHOLDER_GAME_CODES = setOf("0000")
        private val GAME_KEY = Regex("[A-Z0-9]{4}|$HOMEBREW_PREFIX[0-9a-f]{16}")

        /**
         * Folder key for a game: the real game code, or a hash of the ROM URI for placeholder codes shared by homebrew; null when the header
         * couldn't be read, so data stays session-only
         */
        fun getGameKey(gameCode: String?, romUri: String): String? {
            if (gameCode == null) {
                return null
            }
            if (GAME_CODE.matches(gameCode) && gameCode !in PLACEHOLDER_GAME_CODES) {
                return gameCode
            }

            val uriHash = MessageDigest.getInstance("SHA-256").digest(romUri.toByteArray())
            return HOMEBREW_PREFIX + uriHash.toShortHex()
        }

        /**
         * The script's file name plus `.bin`, or a hash when the name can't be a file name here (a path separator, a NUL, `.`/`..`, or too long)
         */
        fun getFileName(scriptName: String): String {
            val isPlainNameSafe = scriptName.isNotEmpty() && scriptName != "." && scriptName != ".." &&
                scriptName.none { it == '/' || it == '\\' || it == '\u0000' } && scriptName.encodeToByteArray().size <= MAX_PLAIN_NAME_BYTES
            if (isPlainNameSafe) {
                return scriptName + DATA_SUFFIX
            }

            val nameHash = MessageDigest.getInstance("SHA-256").digest(scriptName.encodeToByteArray())
            return "script-" + nameHash.toShortHex() + DATA_SUFFIX
        }

        private fun ByteArray.toShortHex(): String {
            return take(8).joinToString("") { "%02x".format(it) }
        }
    }
}
