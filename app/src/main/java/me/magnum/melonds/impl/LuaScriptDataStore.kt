package me.magnum.melonds.impl

import android.content.Context
import android.util.Log
import java.io.File
import java.io.IOException
import java.security.MessageDigest

/**
 * Keeps what scripts save with melon.storage, in app-private files: `files/lua-data/<game key>/<script file name>.bin`. Native encodes the
 * data (it's a blob the app never looks into) and hands over snapshots; this writes them and reads them back at launch. The data stays when
 * its script leaves the folder, so putting the script back brings it back.
 *
 * Thread safe: writes go through a temp file and a rename, under one lock.
 */
class LuaScriptDataStore(private val context: Context) {

    private val lock = Any()
    // The game key of the emulator session whose scripts are running, if any. Their data lives in native memory until the session ends, and a
    // running script would write deleted data back, so it can't be deleted meanwhile
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
                // Native never makes a blob this large, so a larger file isn't one of its blobs. It's left for native to replace
                if (file.length() > MAX_DATA_SIZE) {
                    Log.w(TAG, "Saved data of $scriptName is too large, so it's not used")
                    null
                } else {
                    file.readBytes()
                }
            } catch (e: IOException) {
                Log.w(TAG, "Could not read the saved data of $scriptName", e)
                null
            }
        }
    }

    /**
     * Saves [data] for [scriptName]. Empty data deletes the file
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
                Log.w(TAG, "Could not save the data of $scriptName", e)
            } finally {
                tempFile?.delete()
            }
        }
    }

    /**
     * How many bytes [scriptName] has saved. 0 if nothing
     */
    fun getSize(gameKey: String, scriptName: String): Long {
        synchronized(lock) {
            return getFile(gameKey, scriptName)?.takeIf { it.isFile }?.length() ?: 0
        }
    }

    /**
     * Deletes the saved data of [scriptName]. Ignored while a session of the same game is running scripts (see [setSessionGameKey]). That can't
     * happen from the ROM details screen today, since the emulator is stopped before that screen is shown again, but it's cheap to make sure
     */
    fun delete(gameKey: String, scriptName: String) {
        synchronized(lock) {
            if (gameKey == sessionGameKey) {
                Log.w(TAG, "Not deleting the saved data of $scriptName while its game is running")
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
        private const val TAG = "melonDS-lua"
        private const val DATA_FOLDER = "lua-data"
        private const val TEMP_PREFIX = "data"
        private const val TEMP_SUFFIX = ".tmp"
        private const val HOMEBREW_PREFIX = "homebrew-"
        private const val DATA_SUFFIX = ".bin"
        // Room for the suffix within the 255 bytes a file name can have
        private const val MAX_PLAIN_NAME_BYTES = 200

        /**
         * Native's limit. A saved blob is never larger
         */
        const val MAX_DATA_SIZE = 256 * 1024

        // Retail codes are 4 uppercase letters or digits. Anything else (NUL bytes, spaces, ndstool's ####) is a placeholder
        private val GAME_CODE = Regex("[A-Z0-9]{4}")
        private val PLACEHOLDER_GAME_CODES = setOf("0000")
        private val GAME_KEY = Regex("[A-Z0-9]{4}|$HOMEBREW_PREFIX[0-9a-f]{16}")

        /**
         * The folder a game's script data goes in. A real game code is used as is, so the data survives the ROM being moved. Homebrew often
         * keeps a placeholder code, and ROMs that share one shouldn't share data, so those are keyed by the ROM's URI instead (and lose their
         * data if the ROM moves). [gameCode] is null if the header couldn't be read, in which case there's no key: guessing one would start the
         * game with no data and save it in the wrong place, so its scripts keep their data for the session only
         */
        fun getGameKey(gameCode: String?, romUri: String): String? {
            if (gameCode == null) {
                return null
            }
            if (GAME_CODE.matches(gameCode) && gameCode !in PLACEHOLDER_GAME_CODES) {
                return gameCode
            }

            val uriHash = MessageDigest.getInstance("SHA-256").digest(romUri.toByteArray())
            return HOMEBREW_PREFIX + uriHash.take(8).joinToString("") { "%02x".format(it) }
        }

        /**
         * The data file's name for a script: its own file name plus `.bin`, unless that couldn't be a file name here (a path separator, a
         * NUL, `.`/`..`, or too long), in which case it's a hash of the name
         */
        fun getFileName(scriptName: String): String {
            val isPlainNameSafe = scriptName.isNotEmpty() && scriptName != "." && scriptName != ".." &&
                scriptName.none { it == '/' || it == '\\' || it == '\u0000' } && scriptName.encodeToByteArray().size <= MAX_PLAIN_NAME_BYTES
            if (isPlainNameSafe) {
                return scriptName + DATA_SUFFIX
            }

            val nameHash = MessageDigest.getInstance("SHA-256").digest(scriptName.encodeToByteArray())
            return "script-" + nameHash.take(8).joinToString("") { "%02x".format(it) } + DATA_SUFFIX
        }
    }
}
