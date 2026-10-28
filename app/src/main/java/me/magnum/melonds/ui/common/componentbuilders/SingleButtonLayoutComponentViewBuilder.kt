package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder
import me.magnum.melonds.ui.common.view.RaisedButtons

class SingleButtonLayoutComponentViewBuilder(private val layoutComponent: LayoutComponent) : LayoutComponentViewBuilder() {
    override fun build(context: Context, style: ControllerStyle): View {
        return when (layoutComponent) {
            LayoutComponent.BUTTON_L -> RaisedButtons.shoulderL(context, style)
            LayoutComponent.BUTTON_R -> RaisedButtons.shoulderR(context, style)
            LayoutComponent.BUTTON_START -> RaisedButtons.start(context, style)
            LayoutComponent.BUTTON_SELECT -> RaisedButtons.select(context, style)
            else -> RaisedButtons.iconButton(context, style, SingleButtonIcons.icon(layoutComponent))
        }
    }

    override fun getAspectRatio() = 1f
}
