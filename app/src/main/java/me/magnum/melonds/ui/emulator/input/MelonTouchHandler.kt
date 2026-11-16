package me.magnum.melonds.ui.emulator.input

import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.ui.emulator.camera.WideCameraController

/**
 * @param wideCamera Told about the D-pad, which counts as the player moving for the WideMelon camera
 */
class MelonTouchHandler(private val wideCamera: WideCameraController? = null) : IInputListener {
    private var isLidClosed = false

    override fun onKeyPress(key: Input) {
        wideCamera?.onKey(key, true)
        if (key == Input.HINGE) {
            handleHingePress()
        } else {
            MelonEmulator.onInputDown(key)
        }
    }

    override fun onKeyReleased(key: Input) {
        wideCamera?.onKey(key, false)
        if (key != Input.HINGE) {
            MelonEmulator.onInputUp(key)
        }
        if (key == Input.TOUCHSCREEN) {
            MelonEmulator.onScreenRelease()
        }
    }

    override fun onTouch(point: Point) {
        MelonEmulator.onScreenTouch(point.x, point.y)
    }

    private fun handleHingePress() {
        isLidClosed = !isLidClosed
        if (isLidClosed) {
            MelonEmulator.onInputDown(Input.HINGE)
        } else {
            MelonEmulator.onInputUp(Input.HINGE)
        }
    }
}