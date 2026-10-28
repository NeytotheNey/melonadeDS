package me.magnum.melonds.extensions

import android.view.Display
import me.magnum.melonds.domain.model.EMULATOR_TARGET_FPS
import kotlin.math.abs

private const val TARGET_REFRESH_RATE = EMULATOR_TARGET_FPS.toFloat()
// Accept modes within 0.5% of 60Hz (e.g. 59.94Hz)
private const val TARGET_REFRESH_RATE_TOLERANCE = TARGET_REFRESH_RATE * 0.005f

/**
 * Returns the display's modes with the same resolution as the current mode. These are the refresh rates the display can switch to without changing
 * resolution.
 */
fun Display.getModesAtCurrentResolution(): List<Display.Mode> {
    val currentMode = mode
    return supportedModes.filter {
        it.physicalWidth == currentMode.physicalWidth && it.physicalHeight == currentMode.physicalHeight
    }
}

/**
 * Returns the display's 60Hz mode with the same resolution as the current mode, or null if there isn't one.
 */
fun Display.find60HzMode(): Display.Mode? {
    return getModesAtCurrentResolution()
        .filter { abs(it.refreshRate - TARGET_REFRESH_RATE) <= TARGET_REFRESH_RATE_TOLERANCE }
        .minByOrNull { abs(it.refreshRate - TARGET_REFRESH_RATE) }
}
