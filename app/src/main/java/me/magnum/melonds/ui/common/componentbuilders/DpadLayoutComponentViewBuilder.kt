package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder
import me.magnum.melonds.ui.common.view.RaisedButtonView
import me.magnum.melonds.ui.common.view.RaisedButtons

class DpadLayoutComponentViewBuilder : LayoutComponentViewBuilder() {
    override fun build(context: Context, style: ControllerStyle): View {
        return RaisedButtons.dpad(context, style)
    }

    override fun build(context: Context, style: ControllerStyle, spacing: Float): View {
        return RaisedButtons.dpad(context, style, spacing)
    }

    override fun applySpacing(view: View, spacing: Float) {
        (view as? RaisedButtonView)?.setParts(RaisedButtons.dpadParts(spacing), RaisedButtons.spacedDesignSize(spacing))
    }

    override fun getAspectRatio() = 1f
}
