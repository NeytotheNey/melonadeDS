package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.view.View
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder
import me.magnum.melonds.ui.common.view.PressableImageView
import me.magnum.melonds.ui.common.view.RaisedButtons

class SingleButtonLayoutComponentViewBuilder(private val layoutComponent: LayoutComponent) : LayoutComponentViewBuilder() {
    override fun build(context: Context): View {
        return when (layoutComponent) {
            LayoutComponent.BUTTON_L -> RaisedButtons.shoulderL(context)
            LayoutComponent.BUTTON_R -> RaisedButtons.shoulderR(context)
            LayoutComponent.BUTTON_START -> RaisedButtons.start(context)
            LayoutComponent.BUTTON_SELECT -> RaisedButtons.select(context)
            else -> PressableImageView(context).apply {
                setImageResource(getInputDrawable())
            }
        }
    }

    override fun getAspectRatio() = 1f

    private fun getInputDrawable(): Int {
        return when (layoutComponent) {
            LayoutComponent.BUTTON_HINGE -> R.drawable.button_toggle_lid
            LayoutComponent.BUTTON_PAUSE -> R.drawable.button_menu
            LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE -> R.drawable.button_fast_forward
            LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT -> R.drawable.ic_touch_enabled
            LayoutComponent.BUTTON_RESET -> R.drawable.button_reset
            LayoutComponent.BUTTON_SWAP_SCREENS -> R.drawable.button_swap_screens
            LayoutComponent.BUTTON_QUICK_SAVE -> R.drawable.button_quick_save
            LayoutComponent.BUTTON_QUICK_LOAD -> R.drawable.button_quick_load
            LayoutComponent.BUTTON_REWIND -> R.drawable.button_rewind
            LayoutComponent.BUTTON_MICROPHONE_TOGGLE -> R.drawable.button_microphone
            else -> -1
        }
    }
}
