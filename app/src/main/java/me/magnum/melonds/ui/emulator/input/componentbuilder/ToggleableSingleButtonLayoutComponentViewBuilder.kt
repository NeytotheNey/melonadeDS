package me.magnum.melonds.ui.emulator.input.componentbuilder

import android.content.Context
import android.view.View
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder
import me.magnum.melonds.ui.common.view.RaisedButtons

class ToggleableSingleButtonLayoutComponentViewBuilder(private val layoutComponent: LayoutComponent) : LayoutComponentViewBuilder() {

    override fun build(context: Context, style: ControllerStyle): View {
        return when (layoutComponent) {
            LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE -> RaisedButtons.iconButton(context, style, R.drawable.ic_button_fast_forward, dimWhenOff = true)
            LayoutComponent.BUTTON_MICROPHONE_TOGGLE -> RaisedButtons.iconButton(context, style, R.drawable.ic_button_microphone, R.drawable.ic_button_microphone_off)
            LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT -> RaisedButtons.iconButton(context, style, R.drawable.ic_button_touch, R.drawable.ic_button_touch_off)
            else -> RaisedButtons.iconButton(context, style, null)
        }
    }

    override fun getAspectRatio() = 1f
}
