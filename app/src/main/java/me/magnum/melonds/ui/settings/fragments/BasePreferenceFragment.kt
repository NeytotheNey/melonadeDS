package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceGroup
import androidx.preference.SeekBarPreference
import androidx.recyclerview.widget.RecyclerView
import me.magnum.melonds.R
import me.magnum.melonds.ui.settings.PreferenceCardDecoration

abstract class BasePreferenceFragment : PreferenceFragmentCompat() {

    override fun onCreateRecyclerView(inflater: LayoutInflater, parent: ViewGroup, savedInstanceState: Bundle?): RecyclerView {
        return super.onCreateRecyclerView(inflater, parent, savedInstanceState).apply {
            clipToPadding = false
            scrollBarStyle = View.SCROLLBARS_OUTSIDE_OVERLAY

            // Rows are drawn as grouped cards with a margin from the screen edges
            val horizontalMargin = resources.getDimensionPixelSize(R.dimen.preference_card_margin_horizontal)
            val bottomPadding = resources.getDimensionPixelSize(R.dimen.preference_list_padding_bottom)
            updatePadding(left = horizontalMargin, right = horizontalMargin)
            addItemDecoration(PreferenceCardDecoration { preferenceScreen })

            ViewCompat.setOnApplyWindowInsetsListener(this) { view, windowInsets ->
                val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
                view.updatePadding(bottom = insets.bottom + bottomPadding)

                WindowInsetsCompat.CONSUMED
            }
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        // The gaps between the cards separate the rows, so no dividers are needed
        setDivider(null)
    }

    /**
     * Hides [group] while none of its rows is visible; call it again whenever a row's visibility changes.
     */
    protected fun updateGroupVisibility(group: PreferenceGroup) {
        group.isVisible = (0 until group.preferenceCount).any { group.getPreference(it).isVisible }
    }

    /**
     * Shows the saved value of [preference], formatted by [format], as its summary, kept current by the summary provider.
     */
    protected fun setSliderSummary(preference: SeekBarPreference, format: (Int) -> String) {
        preference.summaryProvider = Preference.SummaryProvider<SeekBarPreference> { format(it.value) }
    }
}
