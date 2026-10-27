package me.magnum.melonds.ui.layouteditor

import android.content.Context
import android.util.AttributeSet
import android.widget.ScrollView
import me.magnum.melonds.impl.dpToPixels
import kotlin.math.min

/**
 * Scroll view for the layout editor's item panel. It's never wider than 480dp and never taller than half of the space its parent gives it, so that
 * the panel doesn't cover the whole editor.
 */
class LayoutEditorPanelScrollView(context: Context, attrs: AttributeSet?) : ScrollView(context, attrs) {

    private val maxWidth = context.dpToPixels(480f).toInt()

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val widthSpec = MeasureSpec.makeMeasureSpec(min(MeasureSpec.getSize(widthMeasureSpec), maxWidth), MeasureSpec.getMode(widthMeasureSpec))
        val heightSpec = if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.UNSPECIFIED) {
            heightMeasureSpec
        } else {
            MeasureSpec.makeMeasureSpec(MeasureSpec.getSize(heightMeasureSpec) / 2, MeasureSpec.AT_MOST)
        }
        super.onMeasure(widthSpec, heightSpec)
    }
}
