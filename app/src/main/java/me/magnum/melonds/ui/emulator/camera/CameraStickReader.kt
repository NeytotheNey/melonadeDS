package me.magnum.melonds.ui.emulator.camera

import android.view.MotionEvent
import kotlin.math.abs

/**
 * Reads the right stick's X (AXIS_Z) for the WideMelon camera, only from devices that report AXIS_Z and AXIS_RZ as a stick pair (both centered
 * ranges) and only once that stick has been seen resting in the middle, so a trigger on AXIS_Z that rests at -1 never pans. Main thread only.
 */
class CameraStickReader {

    // Devices whose right stick has been seen in the middle
    private val centeredDevices = mutableSetOf<Int>()

    /**
     * The right stick's X from [event], -1 to 1, or 0 if this device's AXIS_Z isn't a usable stick (yet)
     */
    fun readRightX(event: MotionEvent): Float {
        val device = event.device ?: return 0f
        val xRange = device.getMotionRange(MotionEvent.AXIS_Z, event.source)
        val yRange = device.getMotionRange(MotionEvent.AXIS_RZ, event.source)
        if (xRange == null || yRange == null || !isCenteredRange(xRange) || !isCenteredRange(yRange)) {
            return 0f
        }

        val x = event.getAxisValue(MotionEvent.AXIS_Z)
        val y = event.getAxisValue(MotionEvent.AXIS_RZ)
        if (event.deviceId !in centeredDevices) {
            if (abs(x) > CENTER_TOLERANCE || abs(y) > CENTER_TOLERANCE) {
                return 0f
            }
            centeredDevices.add(event.deviceId)
        }
        return x
    }

    private fun isCenteredRange(range: android.view.InputDevice.MotionRange): Boolean {
        return range.min <= -0.5f && range.max >= 0.5f
    }

    companion object {
        private const val CENTER_TOLERANCE = 0.2f
    }
}
