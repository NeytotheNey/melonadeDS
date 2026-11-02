package me.magnum.melonds.ui.emulator.input

import android.view.MotionEvent
import android.view.View
import me.magnum.melonds.common.vibration.TouchVibrator
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.ui.common.view.PressedInputsView
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Four buttons with overlapping circular hit areas; [spacing] (0..1) narrows the band in which a touch presses two of them.
 */
abstract class MultiButtonInputHandler(
    inputListener: IInputListener,
    enableHapticFeedback: Boolean,
    touchVibrator: TouchVibrator,
    private val spacing: Float,
) : FeedbackInputHandler(inputListener, enableHapticFeedback, touchVibrator) {
    private var areDimensionsInitialized = false
    private val buttonCircles = mutableListOf<ButtonCircle>()
    private var dualPressThreshold = Float.MAX_VALUE
    private val pressedInputs = mutableListOf<Input>()
    private val newPressedInputs = mutableListOf<Input>()
    private val trackedPointerIds = mutableSetOf<Int>()
    // Reusable input list to avoid memory allocations
    private val tempInputList = mutableListOf<Input>()

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        if (!areDimensionsInitialized) {
            initDimensions(v.width, v.height)
            areDimensionsInitialized = true
        }

        newPressedInputs.clear()

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val downPointerIndex = event.actionIndex
                val downX = event.getX(downPointerIndex)
                val downY = event.getY(downPointerIndex)
                if (isWithinViewBounds(v, downX, downY)) {
                    trackedPointerIds.add(event.getPointerId(downPointerIndex))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                trackedPointerIds.remove(event.getPointerId(event.actionIndex))
            }
            MotionEvent.ACTION_CANCEL -> {
                trackedPointerIds.clear()
            }
        }

        for (pointerIndex in 0 until event.pointerCount) {
            if (event.getPointerId(pointerIndex) !in trackedPointerIds) {
                continue
            }

            val pointerX = event.getX(pointerIndex)
            val pointerY = event.getY(pointerIndex)
            for (circleIndex in buttonCircles.indices) {
                val circle = buttonCircles[circleIndex]
                if (circle.input !in newPressedInputs && circle.containsPoint(pointerX, pointerY) && !isMuchCloserToOtherButton(circleIndex, pointerX, pointerY)) {
                    newPressedInputs.add(circle.input)
                }
            }
        }

        tempInputList.clear()
        pressedInputs.filterNotTo(tempInputList) {
            it in newPressedInputs
        }.forEach {
            inputListener.onKeyReleased(it)
        }

        var pressedInputsChanged = false
        if (tempInputList.isNotEmpty()) {
            performHapticFeedback(v, HapticFeedbackType.KEY_RELEASE)
            pressedInputsChanged = true
        }

        tempInputList.clear()
        newPressedInputs.filterNotTo(tempInputList) {
            it in pressedInputs
        }.forEach {
            inputListener.onKeyPress(it)
        }

        if (tempInputList.isNotEmpty()) {
            performHapticFeedback(v, HapticFeedbackType.KEY_PRESS)
            pressedInputsChanged = true
        }

        pressedInputs.clear()
        pressedInputs.addAll(newPressedInputs)

        if (pressedInputsChanged) {
            (v as? PressedInputsView)?.setPressedInputs(pressedInputs)
        }

        return true
    }

    private fun initDimensions(viewWidth: Int, viewHeight: Int) {
        val radiusSquared = (viewWidth * 256f / 512f).pow(2)
        val pointToLocal: (Float, Float) -> Point = { x, y ->
            Point().apply {
                this.x = (viewWidth * x / 512f).toInt()
                this.y = (viewHeight * y / 512f).toInt()
            }
        }

        // Each circle is placed on the side of each button, near the edge of the whole image. This allows for a margin of error and the circles also intersect, allowing
        // multiple buttons to be pressed at the same time
        buttonCircles.add(ButtonCircle(pointToLocal(512f + 36f, 256f), radiusSquared, getRightInput()))
        buttonCircles.add(ButtonCircle(pointToLocal(256f, 512f + 36f), radiusSquared, getBottomInput()))
        buttonCircles.add(ButtonCircle(pointToLocal(256f, -36f), radiusSquared, getTopInput()))
        buttonCircles.add(ButtonCircle(pointToLocal(-36f, 256f), radiusSquared, getLeftInput()))

        if (spacing > 0f) {
            // Max distance difference inside an overlap is 2 * radius - center distance; opposite circles never overlap
            val radius = sqrt(radiusSquared)
            val maxDistanceDifference = 2f * radius - buttonCircles[0].distanceTo(buttonCircles[1].center.x.toFloat(), buttonCircles[1].center.y.toFloat())
            dualPressThreshold = maxDistanceDifference * (1f - 0.8f * spacing)
        }
    }

    /**
     * Whether the point is also inside another button's circle and is closer to that button's center by more than [dualPressThreshold].
     */
    private fun isMuchCloserToOtherButton(circleIndex: Int, pointX: Float, pointY: Float): Boolean {
        if (dualPressThreshold == Float.MAX_VALUE) {
            return false
        }

        val distance = buttonCircles[circleIndex].distanceTo(pointX, pointY)
        for (otherIndex in buttonCircles.indices) {
            val other = buttonCircles[otherIndex]
            if (otherIndex != circleIndex && other.containsPoint(pointX, pointY) && distance - other.distanceTo(pointX, pointY) > dualPressThreshold) {
                return true
            }
        }
        return false
    }

    private fun isWithinViewBounds(view: View, x: Float, y: Float): Boolean {
        return x >= 0f && x < view.width && y >= 0f && y < view.height
    }

    private data class ButtonCircle(val center: Point, val radiusSquared: Float, val input: Input) {
        fun containsPoint(pointX: Float, pointY: Float): Boolean {
            return (pointX - center.x).pow(2) + (pointY - center.y).pow(2) <= radiusSquared
        }

        fun distanceTo(pointX: Float, pointY: Float): Float {
            return sqrt((pointX - center.x).pow(2) + (pointY - center.y).pow(2))
        }
    }

    abstract fun getTopInput(): Input
    abstract fun getLeftInput(): Input
    abstract fun getBottomInput(): Input
    abstract fun getRightInput(): Input
}