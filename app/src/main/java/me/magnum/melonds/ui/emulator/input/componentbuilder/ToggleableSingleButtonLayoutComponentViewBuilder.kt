package me.magnum.melonds.ui.emulator.input.componentbuilder

import android.content.Context
import android.view.View
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder
import me.magnum.melonds.ui.common.componentbuilders.SingleButtonIcons
import me.magnum.melonds.ui.common.view.RaisedButtons

class ToggleableSingleButtonLayoutComponentViewBuilder(private val layoutComponent: LayoutComponent) : LayoutComponentViewBuilder() {

    override fun build(context: Context, style: ControllerStyle): View {
        // Fast forward has no off icon, so its icon is dimmed while it's off
        return RaisedButtons.iconButton(
            context = context,
            style = style,
            iconRes = SingleButtonIcons.icon(layoutComponent),
            offIconRes = SingleButtonIcons.offIcon(layoutComponent),
            dimWhenOff = true,
        )
    }

    override fun getAspectRatio() = 1f
}
