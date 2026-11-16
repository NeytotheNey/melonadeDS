package me.magnum.melonds.ui.emulator.camera

import android.view.MotionEvent
import kotlin.math.abs

/**
 * Reads the right stick (AXIS_Z for X, AXIS_RZ for Y) for the WideMelon camera, only from devices that report AXIS_Z and AXIS_RZ as a stick pair
 * (both centered ranges) and only once that stick has been seen resting in the middle, so a trigger on AXIS_Z that rests at -1 never pans. Main
 * thread only.
 */
class CameraStickReader {

    // Devices whose right stick has been seen in the middle
    private val centeredDevices = mutableSetOf<Int>()

    /**
     * The right stick's X and Y from the last [readRight], -1 to 1 (right and down are positive), or 0 if the device's stick isn't usable (yet)
     */
    var rightX = 0f
        private set
    var rightY = 0f
        private set

    /**
     * Reads the right stick from [event] into [rightX] and [rightY]
     */
    fun readRight(event: MotionEvent) {
        rightX = 0f
        rightY = 0f
        val device = event.device ?: return
        val xRange = device.getMotionRange(MotionEvent.AXIS_Z, event.source)
        val yRange = device.getMotionRange(MotionEvent.AXIS_RZ, event.source)
        if (xRange == null || yRange == null || !isCenteredRange(xRange) || !isCenteredRange(yRange)) {
            return
        }

        val x = event.getAxisValue(MotionEvent.AXIS_Z)
        val y = event.getAxisValue(MotionEvent.AXIS_RZ)
        if (event.deviceId !in centeredDevices) {
            if (abs(x) > CENTER_TOLERANCE || abs(y) > CENTER_TOLERANCE) {
                return
            }
            centeredDevices.add(event.deviceId)
        }
        rightX = x
        rightY = y
    }

    private fun isCenteredRange(range: android.view.InputDevice.MotionRange): Boolean {
        return range.min <= -0.5f && range.max >= 0.5f
    }

    companion object {
        private const val CENTER_TOLERANCE = 0.2f
    }
}
