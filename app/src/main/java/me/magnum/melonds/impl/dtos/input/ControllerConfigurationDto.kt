package me.magnum.melonds.impl.dtos.input

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import me.magnum.melonds.domain.model.ControllerConfiguration
import me.magnum.melonds.impl.input.withDefaultCameraZoomKey

@Serializable
data class ControllerConfigurationDto(
    @SerialName("inputMapper") val inputMapper: List<InputConfigDto>,
) {

    companion object {
        fun fromControllerConfiguration(controllerConfiguration: ControllerConfiguration): ControllerConfigurationDto {
            val inputMapping = controllerConfiguration.inputMapper.map { InputConfigDto.fromInputConfig(it) }
            return ControllerConfigurationDto(inputMapping)
        }
    }

    /**
     * The stored mapping, without entries for inputs this version doesn't have. A configuration saved before the camera's zoom hotkey existed
     * gets its default key, unless that key is already mapped to something else
     */
    fun toControllerConfiguration(): ControllerConfiguration {
        return ControllerConfiguration(withDefaultCameraZoomKey(inputMapper.mapNotNull { it.toInputConfig() }))
    }
}