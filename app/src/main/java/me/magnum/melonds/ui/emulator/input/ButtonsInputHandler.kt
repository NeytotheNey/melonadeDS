package me.magnum.melonds.ui.emulator.input

import me.magnum.melonds.common.vibration.TouchVibrator
import me.magnum.melonds.domain.model.Input

class ButtonsInputHandler(inputListener: IInputListener, enableHapticFeedback: Boolean, touchVibrator: TouchVibrator, spacing: Float) : MultiButtonInputHandler(inputListener, enableHapticFeedback, touchVibrator, spacing) {
    override fun getTopInput() = Input.X
    override fun getLeftInput() = Input.Y
    override fun getBottomInput() = Input.B
    override fun getRightInput() = Input.A
}