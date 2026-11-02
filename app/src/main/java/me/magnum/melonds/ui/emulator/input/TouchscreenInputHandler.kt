package me.magnum.melonds.ui.emulator.input

import android.annotation.SuppressLint
import android.view.MotionEvent
import android.view.MotionEvent.PointerCoords
import android.view.View
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.domain.model.Rect

/**
 * @param getScreenArea Where the bottom screen's middle SCREEN_WIDTH columns are drawn in the view, or null for the whole view.
 */
class TouchscreenInputHandler(
    inputListener: IInputListener,
    private val getScreenArea: (View) -> Rect?,
) : BaseInputHandler(inputListener) {
    private val touchPoint: Point = Point()
    // The whole-view fallback, kept while the view's size doesn't change
    private var wholeViewArea = Rect(0, 0, 0, 0)

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouch(v: View, event: MotionEvent): Boolean {
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                inputListener.onKeyPress(Input.TOUCHSCREEN)
                inputListener.onTouch(normalizeTouchCoordinates(event, screenArea(v)))
            }
            MotionEvent.ACTION_MOVE -> {
                inputListener.onTouch(normalizeTouchCoordinates(event, screenArea(v)))
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                // The listener lets go of the screen too, so a release a layout change holds back doesn't let go of it
                inputListener.onKeyReleased(Input.TOUCHSCREEN)
            }
        }
        return true
    }

    private fun screenArea(v: View): Rect {
        getScreenArea(v)?.let { return it }
        if (wholeViewArea.width != v.width || wholeViewArea.height != v.height) {
            wholeViewArea = Rect(0, 0, v.width, v.height)
        }
        return wholeViewArea
    }

    private fun normalizeTouchCoordinates(event: MotionEvent, screenRect: Rect): Point {
        var averageTouchX = 0f
        var averageTouchY = 0f
        val pointerCoordinates = PointerCoords()

        // Average out touch positions. Even though the DS has a resistive touch screen, some games rely on the nuances
        // of this technology for some mechanics. Averaging out the coordinates of the touch position allows us to
        // simulate those nuances to some degree
        for (i in 0 until event.pointerCount) {
            event.getPointerCoords(i, pointerCoordinates)
            averageTouchX += pointerCoordinates.x
            averageTouchY += pointerCoordinates.y
        }
        averageTouchX /= event.pointerCount
        averageTouchY /= event.pointerCount

        // A touch beside the screen's middle (on the bars beside a fitted screen, or on a wide one's sides) counts as one on its nearest edge
        touchPoint.x = ((averageTouchX - screenRect.x) / screenRect.width * 256).toInt().coerceIn(0, 255)
        touchPoint.y = ((averageTouchY - screenRect.y) / screenRect.height * 192).toInt().coerceIn(0, 191)
        return touchPoint
    }
}