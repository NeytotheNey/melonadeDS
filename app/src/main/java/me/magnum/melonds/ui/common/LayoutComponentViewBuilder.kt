package me.magnum.melonds.ui.common

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle

abstract class LayoutComponentViewBuilder {
    abstract fun build(context: Context, style: ControllerStyle): View

    /**
     * Builds the view with its buttons already spread out by [spacing], as [applySpacing] would.
     */
    open fun build(context: Context, style: ControllerStyle, spacing: Float): View {
        return build(context, style).also {
            if (spacing > 0f) {
                applySpacing(it, spacing)
            }
        }
    }

    /**
     * Spreads out the buttons of a view made by [build] from 0 to 1 (only the D-pad and face buttons override it); the view must grow to keep their
     * size.
     */
    open fun applySpacing(view: View, spacing: Float) {}

    abstract fun getAspectRatio(): Float
}