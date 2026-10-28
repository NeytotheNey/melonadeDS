package me.magnum.melonds.ui.common

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle

abstract class LayoutComponentViewBuilder {
    abstract fun build(context: Context, style: ControllerStyle): View
    abstract fun getAspectRatio(): Float
}