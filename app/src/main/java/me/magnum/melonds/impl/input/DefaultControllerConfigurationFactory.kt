package me.magnum.melonds.impl.input

import android.view.KeyEvent
import android.view.MotionEvent
import me.magnum.melonds.domain.model.ControllerConfiguration
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.InputConfig

// The WideMelon camera's zoom hotkey by default: the right stick's click, as in PokeDaisy. Configs saved before it existed get it too, if it's free
// (withDefaultCameraZoomKey)
const val CAMERA_ZOOM_DEFAULT_KEY = KeyEvent.KEYCODE_BUTTON_THUMBR

/**
 * [configs], a stored mapping, with the camera's zoom hotkey on its default key if the mapping has no entry for the hotkey (it was saved before the
 * hotkey existed) and nothing else uses that key. An entry that leaves the hotkey unmapped is kept as it is
 */
fun withDefaultCameraZoomKey(configs: List<InputConfig>): List<InputConfig> {
    val hasCameraZoom = configs.any { it.input == Input.CAMERA_ZOOM }
    val isDefaultKeyTaken = configs.any { config ->
        listOf(config.assignment, config.altAssignment).any { (it as? InputConfig.Assignment.Key)?.keyCode == CAMERA_ZOOM_DEFAULT_KEY }
    }
    return if (hasCameraZoom || isDefaultKeyTaken) {
        configs
    } else {
        configs + InputConfig(Input.CAMERA_ZOOM, InputConfig.Assignment.Key(null, CAMERA_ZOOM_DEFAULT_KEY))
    }
}

class DefaultControllerConfigurationFactory : ControllerConfigurationFactory {

    override fun buildDefaultControllerConfiguration(): ControllerConfiguration {
        val inputList = listOf(
            InputConfig(Input.A, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_B)),
            InputConfig(Input.B, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_A)),
            InputConfig(Input.X, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_Y)),
            InputConfig(Input.Y, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_X)),
            InputConfig(Input.LEFT, InputConfig.Assignment.Axis(null, MotionEvent.AXIS_HAT_X, InputConfig.Assignment.Axis.Direction.NEGATIVE), InputConfig.Assignment.Axis(null, MotionEvent.AXIS_X, InputConfig.Assignment.Axis.Direction.NEGATIVE)),
            InputConfig(Input.RIGHT, InputConfig.Assignment.Axis(null, MotionEvent.AXIS_HAT_X, InputConfig.Assignment.Axis.Direction.POSITIVE), InputConfig.Assignment.Axis(null, MotionEvent.AXIS_X, InputConfig.Assignment.Axis.Direction.POSITIVE)),
            InputConfig(Input.UP, InputConfig.Assignment.Axis(null, MotionEvent.AXIS_HAT_Y, InputConfig.Assignment.Axis.Direction.NEGATIVE), InputConfig.Assignment.Axis(null, MotionEvent.AXIS_Y, InputConfig.Assignment.Axis.Direction.NEGATIVE)),
            InputConfig(Input.DOWN, InputConfig.Assignment.Axis(null, MotionEvent.AXIS_HAT_Y, InputConfig.Assignment.Axis.Direction.POSITIVE), InputConfig.Assignment.Axis(null, MotionEvent.AXIS_Y, InputConfig.Assignment.Axis.Direction.POSITIVE)),
            InputConfig(Input.L, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_L1)),
            InputConfig(Input.R, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_R1)),
            InputConfig(Input.START, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_START)),
            InputConfig(Input.SELECT, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_SELECT)),
            InputConfig(Input.PAUSE, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_MODE)),
            InputConfig(Input.CAMERA_ZOOM, InputConfig.Assignment.Key(null, CAMERA_ZOOM_DEFAULT_KEY)),
        )

        return ControllerConfiguration(inputList)
    }
}