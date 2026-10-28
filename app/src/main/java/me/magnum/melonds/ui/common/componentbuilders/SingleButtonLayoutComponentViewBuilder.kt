package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.view.View
import me.magnum.melonds.R
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
            else -> RaisedButtons.iconButton(context, style, getInputIcon())
        }
    }

    override fun getAspectRatio() = 1f

    private fun getInputIcon(): Int? {
        return when (layoutComponent) {
            LayoutComponent.BUTTON_HINGE -> R.drawable.ic_button_hinge
            LayoutComponent.BUTTON_PAUSE -> R.drawable.ic_settings
            LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE -> R.drawable.ic_button_fast_forward
            LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT -> R.drawable.ic_button_touch
            LayoutComponent.BUTTON_RESET -> R.drawable.ic_refresh
            LayoutComponent.BUTTON_SWAP_SCREENS -> R.drawable.ic_button_swap_screens
            LayoutComponent.BUTTON_QUICK_SAVE -> R.drawable.ic_button_quick_save
            LayoutComponent.BUTTON_QUICK_LOAD -> R.drawable.ic_button_quick_load
            LayoutComponent.BUTTON_REWIND -> R.drawable.ic_button_rewind
            LayoutComponent.BUTTON_MICROPHONE_TOGGLE -> R.drawable.ic_button_microphone
            else -> null
        }
    }
}
