package me.magnum.melonds.ui.emulator.input.view

import android.content.Context
import android.util.AttributeSet
import me.magnum.melonds.ui.common.view.PressableImageView

class ToggleableImageView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : PressableImageView(context, attrs) {

    var enabledDrawable: Int? = null
    var disabledDrawable: Int? = null

    fun setToggleState(enabled: Boolean) {
        val drawable = if (enabled) enabledDrawable else disabledDrawable
        if (drawable != null) {
            setImageResource(drawable)
        }
    }
}
