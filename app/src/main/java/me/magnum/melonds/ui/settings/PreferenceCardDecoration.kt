package me.magnum.melonds.ui.settings

import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.view.View
import androidx.core.view.children
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceGroup
import androidx.preference.PreferenceScreen
import androidx.recyclerview.widget.RecyclerView
import me.magnum.melonds.R

/**
 * Draws the rows between two [PreferenceCategory] headers as one card group (gaps between rows, large outer and small
 * inner corners), recomputed on every layout and draw so hidden rows don't break the shape.
 */
class PreferenceCardDecoration(private val preferenceScreenProvider: () -> PreferenceScreen?) : RecyclerView.ItemDecoration() {

    private companion object {
        const val SHAPE_FIRST = 1
        const val SHAPE_LAST = 2
    }

    override fun getItemOffsets(outRect: Rect, view: View, parent: RecyclerView, state: RecyclerView.State) {
        val position = parent.getChildAdapterPosition(view)
        if (position == RecyclerView.NO_POSITION) {
            return
        }

        val categoryPositions = findCategoryPositions(parent)
        if (position !in categoryPositions && position > 0 && (position - 1) !in categoryPositions) {
            outRect.top = parent.resources.getDimensionPixelSize(R.dimen.preference_card_gap)
        }
        updateCardShape(view, position, categoryPositions, parent)
    }

    override fun onDraw(c: Canvas, parent: RecyclerView, state: RecyclerView.State) {
        // Rows can be bound again without a new layout pass (when a value changes, for example), so make sure their shape is still correct
        val categoryPositions = findCategoryPositions(parent)
        parent.children.forEach { child ->
            val position = parent.getChildAdapterPosition(child)
            if (position != RecyclerView.NO_POSITION) {
                updateCardShape(child, position, categoryPositions, parent)
            }
        }
    }

    private fun updateCardShape(view: View, position: Int, categoryPositions: Set<Int>, parent: RecyclerView) {
        if (position in categoryPositions) {
            return
        }

        val background = view.background as? RippleDrawable ?: return
        if (background.numberOfLayers == 0 || background.getDrawable(0) !is GradientDrawable) {
            return
        }

        val itemCount = parent.adapter?.itemCount ?: return
        val isFirst = position == 0 || (position - 1) in categoryPositions
        val isLast = position == itemCount - 1 || (position + 1) in categoryPositions
        val shape = (if (isFirst) SHAPE_FIRST else 0) or (if (isLast) SHAPE_LAST else 0)
        if (view.getTag(R.id.preference_card_background) == shape) {
            return
        }

        val resources = parent.resources
        val outerRadius = resources.getDimension(R.dimen.preference_card_corner_radius_outer)
        val innerRadius = resources.getDimension(R.dimen.preference_card_corner_radius_inner)
        val topRadius = if (isFirst) outerRadius else innerRadius
        val bottomRadius = if (isLast) outerRadius else innerRadius

        // Each row needs its own copy of the shape, otherwise changing the corners of one row would change all of them
        val card = (background.mutate() as RippleDrawable).getDrawable(0) as GradientDrawable
        card.cornerRadii = floatArrayOf(topRadius, topRadius, topRadius, topRadius, bottomRadius, bottomRadius, bottomRadius, bottomRadius)
        view.setTag(R.id.preference_card_background, shape)
    }

    private fun findCategoryPositions(parent: RecyclerView): Set<Int> {
        val positionCallback = parent.adapter as? PreferenceGroup.PreferencePositionCallback ?: return emptySet()
        val preferenceScreen = preferenceScreenProvider() ?: return emptySet()

        val positions = mutableSetOf<Int>()
        collectCategoryPositions(preferenceScreen, positionCallback, positions)
        return positions
    }

    private fun collectCategoryPositions(group: PreferenceGroup, positionCallback: PreferenceGroup.PreferencePositionCallback, positions: MutableSet<Int>) {
        for (i in 0 until group.preferenceCount) {
            val preference = group.getPreference(i)
            if (preference is PreferenceCategory) {
                val position = positionCallback.getPreferenceAdapterPosition(preference)
                if (position != RecyclerView.NO_POSITION) {
                    positions.add(position)
                }
                collectCategoryPositions(preference, positionCallback, positions)
            }
        }
    }
}
