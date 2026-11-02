package me.magnum.melonds.ui.common

import android.view.View
import android.widget.FrameLayout
import androidx.core.view.updateLayoutParams
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.domain.model.Rect

class LayoutComponentView(val view: View, val aspectRatio: Float, val component: LayoutComponent) {
    var baseAlpha: Float = 1f
        set(value) {
            field = value
            applyAlpha()
        }

    var onTop: Boolean = false

    /**
     * See [me.magnum.melonds.domain.model.layout.PositionedLayoutComponent.spacing]; setting it does not redraw the view.
     */
    var spacing: Float = 0f

    fun setPosition(position: Point) {
        view.updateLayoutParams<FrameLayout.LayoutParams> {
            leftMargin = position.x
            topMargin = position.y
        }
    }

    fun setPositionAndSize(position: Point, width: Int, height: Int) {
        view.updateLayoutParams<FrameLayout.LayoutParams> {
            this.width = width
            this.height = height
            leftMargin = position.x
            topMargin = position.y
        }
    }

    fun getWidth(): Int {
        return view.width
    }

    fun getHeight(): Int {
        return view.height
    }

    fun getRect(): Rect {
        return Rect(
                view.x.toInt(),
                view.y.toInt(),
                view.width,
                view.height
        )
    }

    fun setHighlighted(value: Boolean) {
        view.isSelected = value
    }

    private fun applyAlpha() {
        view.alpha = baseAlpha
    }
}