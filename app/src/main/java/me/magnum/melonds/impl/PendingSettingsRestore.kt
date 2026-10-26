package me.magnum.melonds.impl

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * Applies a settings restore that [SettingsBackupManager.restore] staged. It runs when the app starts, before anything reads the
 * preferences or the data files and before the migrations, so it can't use Hilt and is created by hand.
 *
 * Folders and files it uses, all in filesDir:
 * - [STAGING_DIR] holds the checked backup. Its [READY_MARKER] is written last, so only a fully staged restore is applied.
 * - [ROLLBACK_DIR] holds the data files and the preferences from before the restore. It's built in [ROLLBACK_TEMP_DIR] and renamed, so
 *   it's always complete, and it's only made once, so a retry after the process is killed still has the true originals.
 * - [ROLLBACK_NEEDED_MARKER] in [ROLLBACK_DIR] means applying failed and the originals have to be put back before anything else.
 * - [RESULT_FILE] tells the first screen whether the restore worked. It's read once by [consumeResult].
 */
class PendingSettingsRestore(
    private val context: Context,
    private val preferences: SharedPreferences,
) {
    companion object {
        private const val TAG = "PendingSettingsRestore"

        internal const val SETTINGS_FILE = "settings.json"
        internal const val CONTROLLER_FILE = "controller_config.json"
        internal const val LAYOUTS_FILE = "layouts.json"
        internal const val ROM_DATA_FILE = "rom_data.json"
        internal val DATA_FILES = listOf(CONTROLLER_FILE, LAYOUTS_FILE, ROM_DATA_FILE)

        internal const val STAGING_DIR = "settings_restore"
        internal const val READY_MARKER = "ready"
        private const val ROLLBACK_DIR = "settings_restore_rollback"
        private const val ROLLBACK_TEMP_DIR = "settings_restore_rollback.tmp"
        private const val ROLLBACK_NEEDED_MARKER = "rollback_needed"
        private const val PREFERENCES_SNAPSHOT_FILE = "preferences.json"
        private const val ABSENT_SUFFIX = ".absent"
        private const val TEMP_SUFFIX = ".tmp"
        private const val RESULT_FILE = "settings_restore_result"
        private const val RESULT_SUCCESS = "success"
        private const val RESULT_FAILED = "failed"

        internal const val LAST_VERSION_KEY = "last_version"

        /**
         * Preferences that belong to this install. They are left out of backups, and a restore keeps their current values.
         */
        internal val KEPT_PREF_KEYS = setOf(
            // RetroAchievements login
            "ra_username",
            "ra_token",
            // Folders and files picked with the system picker. The access to them doesn't carry over to another install
            "sram_dir",
            "rom_search_dirs",
            "bios_dir",
            "dsi_bios_dir",
            "dsi_camera_static_image",
            // Tracks the RetroAchievements hash database, which isn't part of the backup
            "ra_hash_library_last_updated",
            // State of the update checker
            "github_updates_last_check",
            "github_updates_skip_version",
            "github_updates_nightly_next_check_date",
            "github_updates_nightly_last_release_date",
        )
        private val LONG_PREF_KEYS = setOf(LAST_VERSION_KEY)

        /**
         * Returns true if a restore was applied since the last call, false if one failed and the previous settings were kept, or null if
         * there was no restore.
         */
        fun consumeResult(context: Context): Boolean? {
            val resultFile = File(context.filesDir, RESULT_FILE)
            if (!resultFile.isFile) {
                return null
            }

            val result = runCatching { resultFile.readText() }.getOrNull()
            resultFile.delete()
            return when (result) {
                RESULT_SUCCESS -> true
                RESULT_FAILED -> false
                else -> null
            }
        }
    }

    private val filesDir = context.filesDir
    private val stagingDir = File(filesDir, STAGING_DIR)
    private val readyMarker = File(stagingDir, READY_MARKER)
    private val rollbackDir = File(filesDir, ROLLBACK_DIR)
    private val rollbackNeededMarker = File(rollbackDir, ROLLBACK_NEEDED_MARKER)

    fun applyIfPending() {
        // Left behind by a copy that was interrupted. Nothing else is running yet, so none of them are in use
        filesDir.listFiles { file -> file.name.endsWith(".json$TEMP_SUFFIX") }?.forEach { it.delete() }

        when {
            // An earlier attempt failed and couldn't put everything back. Finish that before anything else
            rollbackNeededMarker.isFile -> {
                if (rollBack()) {
                    cleanUp()
                }
            }
            readyMarker.isFile -> applyStagedRestore()
            // Also cleans up after a restore that was only partly staged, or one that finished but wasn't cleaned up
            else -> cleanUp()
        }
    }

    private fun applyStagedRestore() {
        val settingsJson = try {
            JSONObject(File(stagingDir, SETTINGS_FILE).readText())
        } catch (e: Exception) {
            Log.e(TAG, "Failed to read the staged settings", e)
            if (rollbackDir.isDirectory) {
                // An earlier attempt was killed part way, so some files may already be replaced
                failAndRollBack()
            } else {
                writeResult(RESULT_FAILED)
                cleanUp()
            }
            return
        }

        // Only made once. If an earlier attempt was killed part way, the snapshot from before it still holds the true originals
        if (!rollbackDir.isDirectory) {
            try {
                createRollbackSnapshot()
            } catch (e: Exception) {
                // Nothing was changed yet
                Log.e(TAG, "Failed to save the current settings before restoring", e)
                writeResult(RESULT_FAILED)
                cleanUp()
                return
            }
        }

        try {
            // A file that isn't in the backup means there was none, so the current one is removed. This keeps every data file in the same
            // format as the restored "last_version", which the migrations rely on
            DATA_FILES.forEach { fileName ->
                val stagedFile = File(stagingDir, fileName)
                val destination = File(filesDir, fileName)
                if (stagedFile.isFile) {
                    copyFileSafely(stagedFile, destination)
                } else if (destination.exists() && !destination.delete()) {
                    throw IOException("Could not remove $fileName")
                }
            }
            applyPreferences(settingsJson)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to apply the settings restore. Rolling back", e)
            failAndRollBack()
            return
        }

        writeResult(RESULT_SUCCESS)
        cleanUp()
    }

    private fun failAndRollBack() {
        writeResult(RESULT_FAILED)
        // Written before rolling back, so if the process is killed now, the next start finishes the rollback instead of trying again
        runCatching { rollbackNeededMarker.createNewFile() }
        if (rollBack()) {
            cleanUp()
        }
        // Otherwise the marker and the snapshot are kept, so the next start tries the rollback again
    }

    /**
     * Replaces all preferences with the ones in the backup. Keys that aren't in the backup go back to their defaults, except the ones in
     * [KEPT_PREF_KEYS], which keep their current values.
     */
    private fun applyPreferences(settingsJson: JSONObject) {
        val currentPreferences = preferences.all
        // clear() is always applied before the other changes in the same edit, whatever order they are made in
        val editor = preferences.edit().clear()

        currentPreferences.forEach { (key, value) ->
            // A backup without a version is treated as being in the current format, so the migrations don't run on it again
            if (key in KEPT_PREF_KEYS || (key == LAST_VERSION_KEY && !settingsJson.has(LAST_VERSION_KEY))) {
                editor.putValue(key, value)
            }
        }

        for (key in settingsJson.keys()) {
            if (key in KEPT_PREF_KEYS) continue
            when (val value = settingsJson.get(key)) {
                is Boolean -> editor.putBoolean(key, value)
                is Int -> {
                    if (key in LONG_PREF_KEYS) {
                        editor.putLong(key, value.toLong())
                    } else {
                        editor.putInt(key, value)
                    }
                }
                is Long -> editor.putLong(key, value)
                is Double -> editor.putFloat(key, value.toFloat())
                is String -> editor.putString(key, value)
                is JSONArray -> {
                    val set = mutableSetOf<String>()
                    for (i in 0 until value.length()) {
                        set.add(value.getString(i))
                    }
                    editor.putStringSet(key, set)
                }
                is Number -> {
                    if (key in LONG_PREF_KEYS) {
                        editor.putLong(key, value.toLong())
                    } else {
                        editor.putFloat(key, value.toFloat())
                    }
                }
            }
        }

        if (!editor.commit()) {
            throw IOException("Could not save the restored settings")
        }
    }

    /**
     * Saves the current data files and preferences in [rollbackDir]. A data file that doesn't exist is recorded with an empty marker file,
     * so a rollback knows to remove it.
     */
    private fun createRollbackSnapshot() {
        val tempDir = File(filesDir, ROLLBACK_TEMP_DIR)
        tempDir.deleteRecursively()
        if (!tempDir.mkdirs()) {
            throw IOException("Could not create the rollback folder")
        }

        DATA_FILES.forEach { fileName ->
            val currentFile = File(filesDir, fileName)
            if (currentFile.isFile) {
                currentFile.copyTo(File(tempDir, fileName))
            } else if (!File(tempDir, fileName + ABSENT_SUFFIX).createNewFile()) {
                throw IOException("Could not record that $fileName doesn't exist")
            }
        }
        File(tempDir, PREFERENCES_SNAPSHOT_FILE).writeText(preferencesToJson(preferences.all).toString())

        if (!tempDir.renameTo(rollbackDir)) {
            throw IOException("Could not save the rollback folder")
        }
    }

    /**
     * Puts back the data files and the preferences from [rollbackDir]. Returns true if everything was put back.
     */
    private fun rollBack(): Boolean {
        var succeeded = true

        DATA_FILES.forEach { fileName ->
            try {
                val destination = File(filesDir, fileName)
                val savedFile = File(rollbackDir, fileName)
                if (savedFile.isFile) {
                    copyFileSafely(savedFile, destination)
                } else if (File(rollbackDir, fileName + ABSENT_SUFFIX).isFile) {
                    if (destination.exists() && !destination.delete()) {
                        throw IOException("Could not remove $fileName")
                    }
                } else {
                    throw IOException("The rollback folder has nothing for $fileName")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to roll back $fileName", e)
                succeeded = false
            }
        }

        try {
            val savedPreferences = JSONObject(File(rollbackDir, PREFERENCES_SNAPSHOT_FILE).readText())
            val editor = preferences.edit().clear()
            putPreferencesFromJson(editor, savedPreferences)
            // commit() updates the in-memory values before writing them, so they are right for this run even if writing fails
            if (!editor.commit()) {
                throw IOException("Could not save the previous settings")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to roll back the preferences", e)
            succeeded = false
        }

        return succeeded
    }

    /**
     * Removes both markers before any folder, because deleteRecursively() removes files in no particular order. An interrupted clean-up
     * then never leaves a marker without the files it needs, which would make the restore or the rollback run again on partial data.
     */
    private fun cleanUp() {
        readyMarker.delete()
        rollbackNeededMarker.delete()
        stagingDir.deleteRecursively()
        rollbackDir.deleteRecursively()
        File(filesDir, ROLLBACK_TEMP_DIR).deleteRecursively()
    }

    private fun writeResult(result: String) {
        runCatching { File(filesDir, RESULT_FILE).writeText(result) }
            .onFailure { Log.w(TAG, "Failed to save the restore result", it) }
    }

    /**
     * Copies to a temporary file first and then swaps it in, so [destination] stays intact if copying fails part way.
     */
    private fun copyFileSafely(source: File, destination: File) {
        val tempFile = File(destination.parentFile, destination.name + TEMP_SUFFIX)
        try {
            source.copyTo(tempFile, overwrite = true)
            if (!tempFile.renameTo(destination)) {
                throw IOException("Could not replace ${destination.name}")
            }
        } catch (e: Exception) {
            tempFile.delete()
            throw e
        }
    }

    /**
     * Keeps the type of every value, since JSON alone can't tell an Int from a Long or a Float.
     */
    private fun preferencesToJson(values: Map<String, *>): JSONObject {
        val json = JSONObject()
        values.forEach { (key, value) ->
            val entry = when (value) {
                is Boolean -> JSONObject().put("type", "boolean").put("value", value)
                is Int -> JSONObject().put("type", "int").put("value", value)
                is Long -> JSONObject().put("type", "long").put("value", value)
                is Float -> JSONObject().put("type", "float").put("value", value.toDouble())
                is String -> JSONObject().put("type", "string").put("value", value)
                is Set<*> -> JSONObject().put("type", "set").put("value", JSONArray(value.filterIsInstance<String>()))
                else -> null
            }
            if (entry != null) {
                json.put(key, entry)
            }
        }
        return json
    }

    private fun putPreferencesFromJson(editor: SharedPreferences.Editor, json: JSONObject) {
        for (key in json.keys()) {
            val entry = json.getJSONObject(key)
            when (entry.getString("type")) {
                "boolean" -> editor.putBoolean(key, entry.getBoolean("value"))
                "int" -> editor.putInt(key, entry.getInt("value"))
                "long" -> editor.putLong(key, entry.getLong("value"))
                "float" -> editor.putFloat(key, entry.getDouble("value").toFloat())
                "string" -> editor.putString(key, entry.getString("value"))
                "set" -> {
                    val array = entry.getJSONArray("value")
                    editor.putStringSet(key, (0 until array.length()).map { array.getString(it) }.toSet())
                }
            }
        }
    }

    private fun SharedPreferences.Editor.putValue(key: String, value: Any?) {
        when (value) {
            is Boolean -> putBoolean(key, value)
            is Int -> putInt(key, value)
            is Long -> putLong(key, value)
            is Float -> putFloat(key, value)
            is String -> putString(key, value)
            is Set<*> -> putStringSet(key, value.filterIsInstance<String>().toSet())
        }
    }
}
