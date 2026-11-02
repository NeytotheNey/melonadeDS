package me.magnum.melonds.ui.settings.preferences

import android.content.Context
import android.util.AttributeSet
import androidx.preference.ListPreference

/**
 * A [ListPreference] whose value is picked on its app:fragment page instead of a dialog, so the row still shows the value's name as summary.
 */
class PageListPreference(context: Context, attrs: AttributeSet?) : ListPreference(context, attrs) {

    override fun onClick() {
        // No list dialog
    }
}