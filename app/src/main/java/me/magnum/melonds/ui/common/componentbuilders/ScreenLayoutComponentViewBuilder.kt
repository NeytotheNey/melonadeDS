package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.graphics.drawable.Drawable
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder

abstract class ScreenLayoutComponentViewBuilder : LayoutComponentViewBuilder() {
    override fun build(context: Context, style: ControllerStyle): View {
        return View(context).apply {
            background = getBackgroundDrawable(context)
        }
    }

    override fun getAspectRatio() = 256f / 192

    protected abstract fun getBackgroundDrawable(context: Context): Drawable?
}