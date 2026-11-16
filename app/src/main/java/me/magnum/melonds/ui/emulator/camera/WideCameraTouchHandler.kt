package me.magnum.melonds.ui.emulator.camera

import android.annotation.SuppressLint
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/**
 * Drags and pinches on the DS top screen's view for the WideMelon camera: one finger drags the picture, two pinch it (and drag it by their
 * middle). Moves are reported as a fraction of the drawn picture's width ([pictureWidthProvider], in the view's pixels, null to use the view's
 * width), so the picture follows the finger.
 */
class WideCameraTouchHandler(val controller: WideCameraController, private val pictureWidthProvider: () -> Float?) : View.OnTouchListener {

    private var lastX = 0f
    private var lastSpan = 0f
    private var lastPointerCount = 0

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouch(view: View, event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                controller.setTouching(true)
                startGesture(event)
            }
            // A finger went down or up: start from where the fingers are now, so nothing jumps
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_POINTER_UP -> startGesture(event)
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount != lastPointerCount) {
                    startGesture(event)
                } else {
                    val pictureWidth = pictureWidthProvider()?.takeIf { it > 0f } ?: view.width.toFloat()
                    val x = getFocusX(event)
                    val span = getSpan(event)
                    val pinchScale = if (lastSpan > 0f && span > 0f) span / lastSpan else 1f
                    if (pictureWidth > 0f) {
                        controller.onTouchMove((x - lastX) / pictureWidth, pinchScale)
                    }
                    lastX = x
                    lastSpan = span
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> controller.setTouching(false)
        }
        return true
    }

    private fun startGesture(event: MotionEvent) {
        lastX = getFocusX(event)
        lastSpan = getSpan(event)
        lastPointerCount = getActivePointers(event).size
    }

    // A lifting pointer is still in its ACTION_POINTER_UP event, so it's left out here
    private fun getActivePointers(event: MotionEvent): List<Int> {
        val liftedIndex = if (event.actionMasked == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
        return (0 until event.pointerCount).filter { it != liftedIndex }
    }

    private fun getFocusX(event: MotionEvent): Float {
        val pointers = getActivePointers(event)
        return if (pointers.isEmpty()) 0f else pointers.sumOf { event.getX(it).toDouble() }.toFloat() / pointers.size
    }

    // Distance between the first two fingers, 0 with fewer
    private fun getSpan(event: MotionEvent): Float {
        val pointers = getActivePointers(event)
        if (pointers.size < 2) {
            return 0f
        }
        return hypot(event.getX(pointers[1]) - event.getX(pointers[0]), event.getY(pointers[1]) - event.getY(pointers[0]))
    }
}
