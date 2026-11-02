package me.magnum.melonds.ui.layouteditor

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import me.magnum.melonds.common.PreferenceKeys

enum class LayoutEditorGridSize(val dp: Int) {
    SMALL(16),
    MEDIUM(32),
    LARGE(48),
}

/**
 * Snap grid setting shared by the on-device and external editors, stored in the default shared preferences.
 */
class LayoutEditorGridSettings(context: Context) {
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)

    var isSnapEnabled: Boolean
        get() = preferences.getBoolean(PreferenceKeys.LAYOUT_EDITOR_SNAP_TO_GRID, false)
        set(value) = preferences.edit { putBoolean(PreferenceKeys.LAYOUT_EDITOR_SNAP_TO_GRID, value) }

    var gridSize: LayoutEditorGridSize
        get() {
            val name = preferences.getString(PreferenceKeys.LAYOUT_EDITOR_GRID_SIZE, null)
            return LayoutEditorGridSize.entries.firstOrNull { it.name == name } ?: LayoutEditorGridSize.MEDIUM
        }
        set(value) = preferences.edit { putString(PreferenceKeys.LAYOUT_EDITOR_GRID_SIZE, value.name) }

    /**
     * Registers [onChanged], whose returned listener must be kept and passed to [removeListener] since the preferences hold it weakly.
     */
    fun addListener(onChanged: () -> Unit): SharedPreferences.OnSharedPreferenceChangeListener {
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == PreferenceKeys.LAYOUT_EDITOR_SNAP_TO_GRID || key == PreferenceKeys.LAYOUT_EDITOR_GRID_SIZE) {
                onChanged()
            }
        }
        preferences.registerOnSharedPreferenceChangeListener(listener)
        return listener
    }

    fun removeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        preferences.unregisterOnSharedPreferenceChangeListener(listener)
    }
}
