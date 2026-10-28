package me.magnum.melonds.ui.common

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle

abstract class LayoutComponentViewBuilder {
    abstract fun build(context: Context, style: ControllerStyle): View

    /**
     * Spreads out the buttons of a view made by [build], from 0 (as built) to 1. The buttons keep their size relative to the whole control, which takes up
     * more room, so the view has to be made bigger for them to keep their size on screen. Only the D-pad and the face buttons have a spacing, so by
     * default this does nothing.
     */
    open fun applySpacing(view: View, spacing: Float) {}

    abstract fun getAspectRatio(): Float
}