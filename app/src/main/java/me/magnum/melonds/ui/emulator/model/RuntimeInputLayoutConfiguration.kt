package me.magnum.melonds.ui.emulator.model

import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.input.SoftInputBehaviour
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.UILayout

data class RuntimeInputLayoutConfiguration(
    val softInputBehaviour: SoftInputBehaviour,
    val controllerStyle: ControllerStyle,
    val softInputOpacity: Int,
    val isHapticFeedbackEnabled: Boolean,
    val layoutOrientation: LayoutConfiguration.LayoutOrientation,
    val layout: UILayout,
    // LayoutConfiguration.isWide, whose top screen fits the frame at its own aspect instead of widening it automatically
    val isWideLayout: Boolean = false,
)