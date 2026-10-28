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
 * The layout editor's snap grid setting, stored in the default shared preferences. The editors on the device and on an external display share it.
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
     * Calls [onChanged] whenever the setting changes. The returned listener must be kept and passed to [removeListener] later, since the
     * preferences only hold a weak reference to it.
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
