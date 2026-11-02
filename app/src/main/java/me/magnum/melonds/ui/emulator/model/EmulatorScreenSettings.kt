package me.magnum.melonds.ui.emulator.model

import me.magnum.melonds.domain.model.FpsCounterPosition

/**
 * The settings the emulator screen's window, surfaces and FPS counter are set up from, read again whenever they may have changed.
 */
data class EmulatorScreenSettings(
    val isSustainedPerformanceModeEnabled: Boolean,
    val isFramePacingEnabled: Boolean,
    val isDisplayLock60HzEnabled: Boolean,
    val fpsCounterPosition: FpsCounterPosition,
    val showStatusBar: Boolean,
)
