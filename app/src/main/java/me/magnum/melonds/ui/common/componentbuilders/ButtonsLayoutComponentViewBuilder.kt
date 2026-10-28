package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder
import me.magnum.melonds.ui.common.view.RaisedButtonView
import me.magnum.melonds.ui.common.view.RaisedButtons

class ButtonsLayoutComponentViewBuilder : LayoutComponentViewBuilder() {
    override fun build(context: Context, style: ControllerStyle): View {
        return RaisedButtons.abxy(context, style)
    }

    override fun applySpacing(view: View, spacing: Float) {
        (view as? RaisedButtonView)?.setParts(RaisedButtons.abxyParts(spacing), RaisedButtons.spacedDesignSize(spacing))
    }

    override fun getAspectRatio() = 1f
}
