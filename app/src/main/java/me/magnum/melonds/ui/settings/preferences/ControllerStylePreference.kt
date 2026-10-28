package me.magnum.melonds.ui.settings.preferences

import android.content.Context
import android.util.AttributeSet
import androidx.preference.ListPreference

class ControllerStylePreference(context: Context, attrs: AttributeSet?) : ListPreference(context, attrs) {

    override fun onClick() {
        // Disable click behaviour to prevent list dialog from appearing
        /* no-op */
    }
}