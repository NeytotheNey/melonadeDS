package me.magnum.melonds.ui.settings.preferences

import android.content.Context
import android.util.AttributeSet
import androidx.preference.ListPreference

/**
 * A [ListPreference] whose value is picked on the page set with app:fragment instead of in the list dialog. It's still a [ListPreference], so the
 * row can show the name of the saved value as its summary.
 */
class PageListPreference(context: Context, attrs: AttributeSet?) : ListPreference(context, attrs) {

    override fun onClick() {
        // No list dialog
    }
}