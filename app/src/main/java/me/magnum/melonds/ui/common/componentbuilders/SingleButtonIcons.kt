package me.magnum.melonds.ui.common.componentbuilders

import androidx.annotation.DrawableRes
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.layout.LayoutComponent

/**
 * Icons of the single buttons that show an icon instead of a label. Shared by the emulator, the layout editor and the controller style previews.
 */
object SingleButtonIcons {

    @DrawableRes
    fun icon(component: LayoutComponent): Int? {
        return when (component) {
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

    /**
     * The icon a toggle button shows while it's off. Toggles without one show their normal icon dimmed instead.
     */
    @DrawableRes
    fun offIcon(component: LayoutComponent): Int? {
        return when (component) {
            LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT -> R.drawable.ic_button_touch_off
            LayoutComponent.BUTTON_MICROPHONE_TOGGLE -> R.drawable.ic_button_microphone_off
            else -> null
        }
    }
}
