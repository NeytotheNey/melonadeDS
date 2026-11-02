package me.magnum.melonds.impl

import android.content.SharedPreferences
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.common.retroachievements.AndroidRAUserAuthStore
import org.json.JSONArray
import org.json.JSONObject

/**
 * File names and value encoding shared by [SettingsBackupManager], which writes backups and stages restores, and [PendingSettingsRestore],
 * which applies a staged restore on the next start so in-memory repositories can't overwrite it.
 */
internal object SettingsBackupFormat {

    const val SETTINGS_FILE = "settings.json"
    // The data files in filesDir, named here for the repositories and migrations that read and write them too
    const val CONTROLLER_FILE = "controller_config.json"
    const val LAYOUTS_FILE = "layouts.json"
    const val ROM_DATA_FILE = "rom_data.json"
    val DATA_FILES = listOf(CONTROLLER_FILE, LAYOUTS_FILE, ROM_DATA_FILE)

    // The staged restore in filesDir, whose marker is written last so only a fully staged restore is applied
    const val STAGING_DIR = "settings_restore"
    const val READY_MARKER = "ready"

    const val LAST_VERSION_KEY = "last_version"

    /**
     * Preferences that belong to this install: left out of backups and kept as they are by a restore.
     */
    val KEPT_PREF_KEYS = setOf(
        // RetroAchievements login
        AndroidRAUserAuthStore.USERNAME_KEY,
        AndroidRAUserAuthStore.TOKEN_KEY,
        // Folders and files picked with the system picker, whose access doesn't carry over to another install
        "sram_dir",
        "rom_search_dirs",
        "bios_dir",
        "dsi_bios_dir",
        "dsi_camera_static_image",
        PreferenceKeys.PERF_LOG_DIR,
        // Debug logging and the process exits it reported are per install, so a restored backup never starts recording
        PreferenceKeys.ENABLE_DEBUG_LOG,
        PreferenceKeys.DEBUG_LAST_REPORTED_EXIT_MS,
        // Tracks the RetroAchievements hash database, which isn't part of the backup
        AndroidRetroAchievementsRepository.RA_HASH_LIBRARY_LAST_UPDATED,
    )

    /**
     * Long preferences, which backups made before the values had types store as plain numbers that can't tell a Long from an Int.
     */
    val LONG_PREF_KEYS = setOf(LAST_VERSION_KEY)

    // Each value is written as an object with its type and its value
    private const val FIELD_TYPE = "type"
    private const val FIELD_VALUE = "value"
    private const val TYPE_BOOLEAN = "boolean"
    private const val TYPE_INT = "int"
    private const val TYPE_LONG = "long"
    private const val TYPE_FLOAT = "float"
    private const val TYPE_STRING = "string"
    private const val TYPE_SET = "set"

    /**
     * Keeps every value's type (JSON alone can't tell an Int from a Long or a Float), for backups and the rollback snapshot.
     */
    fun preferencesToJson(values: Map<String, *>): JSONObject {
        val json = JSONObject()
        values.forEach { (key, value) ->
            val entry = when (value) {
                is Boolean -> JSONObject().put(FIELD_TYPE, TYPE_BOOLEAN).put(FIELD_VALUE, value)
                is Int -> JSONObject().put(FIELD_TYPE, TYPE_INT).put(FIELD_VALUE, value)
                is Long -> JSONObject().put(FIELD_TYPE, TYPE_LONG).put(FIELD_VALUE, value)
                is Float -> JSONObject().put(FIELD_TYPE, TYPE_FLOAT).put(FIELD_VALUE, value.toDouble())
                is String -> JSONObject().put(FIELD_TYPE, TYPE_STRING).put(FIELD_VALUE, value)
                is Set<*> -> JSONObject().put(FIELD_TYPE, TYPE_SET).put(FIELD_VALUE, JSONArray(value.filterIsInstance<String>()))
                else -> null
            }
            if (entry != null) {
                json.put(key, entry)
            }
        }
        return json
    }

    /**
     * Puts a value written by [preferencesToJson].
     */
    fun putTypedPreference(editor: SharedPreferences.Editor, key: String, entry: JSONObject) {
        val value: Any? = when (entry.getString(FIELD_TYPE)) {
            TYPE_BOOLEAN -> entry.getBoolean(FIELD_VALUE)
            TYPE_INT -> entry.getInt(FIELD_VALUE)
            TYPE_LONG -> entry.getLong(FIELD_VALUE)
            TYPE_FLOAT -> entry.getDouble(FIELD_VALUE).toFloat()
            TYPE_STRING -> entry.getString(FIELD_VALUE)
            TYPE_SET -> {
                val array = entry.getJSONArray(FIELD_VALUE)
                (0 until array.length()).map { array.getString(it) }.toSet()
            }
            else -> null
        }
        editor.putValue(key, value)
    }

    /**
     * Puts a value of any preference type, as [SharedPreferences.getAll] returns them; a value of another type is skipped.
     */
    fun SharedPreferences.Editor.putValue(key: String, value: Any?) {
        when (value) {
            is Boolean -> putBoolean(key, value)
            is Int -> putInt(key, value)
            is Long -> putLong(key, value)
            is Float -> putFloat(key, value)
            is String -> putString(key, value)
            is Set<*> -> putStringSet(key, value.filterIsInstance<String>().toSet())
        }
    }

    /**
     * Reads a long from a backup's settings, typed or a plain number (see [LONG_PREF_KEYS]).
     */
    fun getBackupLong(settingsJson: JSONObject, key: String): Long {
        return when (val value = settingsJson.get(key)) {
            is JSONObject -> value.getLong(FIELD_VALUE)
            else -> settingsJson.getLong(key)
        }
    }
}
