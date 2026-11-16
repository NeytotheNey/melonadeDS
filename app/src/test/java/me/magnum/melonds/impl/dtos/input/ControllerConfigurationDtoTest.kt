package me.magnum.melonds.impl.dtos.input

import android.view.KeyEvent
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.InputConfig
import me.magnum.melonds.impl.input.withDefaultCameraZoomKey
import org.junit.Assert.assertEquals
import org.junit.Test

class ControllerConfigurationDtoTest {

    // As the app's (AppModule.provideJson)
    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }

    private val r3 = KeyEvent.KEYCODE_BUTTON_THUMBR

    private fun key(keyCode: Int) = InputConfigDto.AssignmentDto.Key(null, keyCode)

    // Saved and read back as text, the way the settings file is
    private fun load(vararg entries: InputConfigDto) =
        json.decodeFromString<ControllerConfigurationDto>(json.encodeToString(ControllerConfigurationDto(entries.toList()))).toControllerConfiguration()

    private fun assignmentOf(configuration: me.magnum.melonds.domain.model.ControllerConfiguration, input: Input) =
        configuration.inputMapper.first { it.input == input }.assignment

    @Test
    fun removedInputsAreDroppedAndTheRestIsKept() {
        val configuration = load(
            InputConfigDto("A", key(KeyEvent.KEYCODE_BUTTON_B)),
            InputConfigDto("CAMERA_ZOOM_IN", key(KeyEvent.KEYCODE_BUTTON_L2)),
            InputConfigDto("CAMERA_ZOOM_OUT", key(KeyEvent.KEYCODE_BUTTON_R2)),
            InputConfigDto("SOMETHING_NEWER", key(KeyEvent.KEYCODE_BUTTON_Z)),
            InputConfigDto("REWIND", key(KeyEvent.KEYCODE_BUTTON_L1)),
        )

        assertEquals(InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_B), assignmentOf(configuration, Input.A))
        assertEquals(InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_L1), assignmentOf(configuration, Input.REWIND))
        // Nothing points at the removed inputs' keys anymore
        assertEquals(null, configuration.keyToInput(KeyEvent.KEYCODE_BUTTON_L2))
        assertEquals(null, configuration.keyToInput(KeyEvent.KEYCODE_BUTTON_Z))
    }

    @Test
    fun aConfigFromBeforeTheZoomHotkeyGetsR3() {
        val configuration = load(InputConfigDto("A", key(KeyEvent.KEYCODE_BUTTON_B)))

        assertEquals(InputConfig.Assignment.Key(null, r3), assignmentOf(configuration, Input.CAMERA_ZOOM))
        assertEquals(Input.CAMERA_ZOOM, configuration.keyToInput(r3))
    }

    @Test
    fun r3MappedToSomethingElseIsLeftAlone() {
        val configuration = load(InputConfigDto("X", InputConfigDto.AssignmentDto.None, key(r3)))

        assertEquals(InputConfig.Assignment.None, assignmentOf(configuration, Input.CAMERA_ZOOM))
        assertEquals(Input.X, configuration.keyToInput(r3))
    }

    @Test
    fun aSavedZoomHotkeyIsKeptEvenWhenUnmapped() {
        val configuration = load(InputConfigDto("CAMERA_ZOOM", InputConfigDto.AssignmentDto.None))

        assertEquals(InputConfig.Assignment.None, assignmentOf(configuration, Input.CAMERA_ZOOM))
    }

    @Test
    fun theV33MigrationsRuleGivesR3OnlyWhenItsFree() {
        // Migration33to34 builds its list from the old file and then applies the same rule
        val migrated = withDefaultCameraZoomKey(listOf(InputConfig(Input.A, InputConfig.Assignment.Key(null, KeyEvent.KEYCODE_BUTTON_B))))
        assertEquals(InputConfig.Assignment.Key(null, r3), migrated.first { it.input == Input.CAMERA_ZOOM }.assignment)

        val taken = withDefaultCameraZoomKey(listOf(InputConfig(Input.Y, InputConfig.Assignment.Key(null, r3))))
        assertEquals(null, taken.firstOrNull { it.input == Input.CAMERA_ZOOM })
    }

    @Test
    fun savingKeepsTheSameNames() {
        val saved = ControllerConfigurationDto.fromControllerConfiguration(load(InputConfigDto("A", key(KeyEvent.KEYCODE_BUTTON_B))))

        assertEquals("A", saved.inputMapper.first().input)
        assertEquals(1, saved.inputMapper.count { it.input == "CAMERA_ZOOM" })
    }
}
