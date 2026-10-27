package me.magnum.melonds.ui.common.view

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.PorterDuff
import android.graphics.PorterDuffColorFilter
import android.util.AttributeSet
import androidx.appcompat.widget.AppCompatImageView

/**
 * Image button that sinks a little and darkens while pressed. The image shrinks towards its bottom edge so that nothing gets clipped.
 */
open class PressableImageView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : AppCompatImageView(context, attrs) {

    override fun setPressed(pressed: Boolean) {
        val changed = pressed != isPressed
        super.setPressed(pressed)
        if (changed) {
            if (pressed) setColorFilter(PRESSED_COLOR_FILTER) else clearColorFilter()
            invalidate()
        }
    }

    override fun onDraw(canvas: Canvas) {
        if (isPressed) {
            val save = canvas.save()
            canvas.scale(PRESSED_SCALE, PRESSED_SCALE, width / 2f, height.toFloat())
            super.onDraw(canvas)
            canvas.restoreToCount(save)
        } else {
            super.onDraw(canvas)
        }
    }

    private companion object {
        const val PRESSED_SCALE = 0.94f
        val PRESSED_COLOR_FILTER = PorterDuffColorFilter(Color.rgb(190, 190, 190), PorterDuff.Mode.MULTIPLY)
    }
}
