package me.magnum.melonds.impl.dtos.input

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.InputConfig

@Serializable
data class InputConfigDto(
    // Input's name, kept as text so a name this version doesn't know (one that was removed, like the camera's old CAMERA_ZOOM_IN) only drops its
    // own entry instead of failing the whole configuration
    @SerialName("input") val input: String,
    @SerialName("assignment") val assignment: AssignmentDto,
    @SerialName("altAssignment") val altAssignment: AssignmentDto = AssignmentDto.None,
) {

    @Serializable
    sealed class AssignmentDto {
        @SerialName("deviceId") abstract val deviceId: Int?

        @Serializable
        @SerialName("none")
        data object None : AssignmentDto() {
            override val deviceId: Int? = null
        }

        @Serializable
        @SerialName("key")
        class Key(
            override val deviceId: Int?,
            @SerialName("keyCode") val keyCode: Int,
        ) : AssignmentDto()

        @Serializable
        @SerialName("axis")
        class Axis(
            override val deviceId: Int?,
            @SerialName("axisCode") val axisCode: Int,
            @SerialName("direction") val direction: InputConfig.Assignment.Axis.Direction,
        ) : AssignmentDto()
    }

    companion object {
        fun fromInputConfig(inputConfig: InputConfig): InputConfigDto {
            return InputConfigDto(
                input = inputConfig.input.name,
                assignment = when (inputConfig.assignment) {
                    is InputConfig.Assignment.None -> AssignmentDto.None
                    is InputConfig.Assignment.Key -> AssignmentDto.Key(inputConfig.assignment.deviceId, inputConfig.assignment.keyCode)
                    is InputConfig.Assignment.Axis -> AssignmentDto.Axis(inputConfig.assignment.deviceId, inputConfig.assignment.axisCode, inputConfig.assignment.direction)
                },
                altAssignment = when (inputConfig.altAssignment) {
                    is InputConfig.Assignment.None -> AssignmentDto.None
                    is InputConfig.Assignment.Key -> AssignmentDto.Key(inputConfig.altAssignment.deviceId, inputConfig.altAssignment.keyCode)
                    is InputConfig.Assignment.Axis -> AssignmentDto.Axis(inputConfig.altAssignment.deviceId, inputConfig.altAssignment.axisCode, inputConfig.altAssignment.direction)
                }
            )
        }
    }

    /**
     * The entry as an InputConfig, or null if its input is one this version doesn't have
     */
    fun toInputConfig(): InputConfig? {
        val knownInput = Input.entries.firstOrNull { it.name == input } ?: return null
        return InputConfig(
            input = knownInput,
            assignment = when (assignment) {
                is AssignmentDto.None -> InputConfig.Assignment.None
                is AssignmentDto.Key -> InputConfig.Assignment.Key(assignment.deviceId, assignment.keyCode)
                is AssignmentDto.Axis -> InputConfig.Assignment.Axis(assignment.deviceId, assignment.axisCode, assignment.direction)
            },
            altAssignment = when (altAssignment) {
                is AssignmentDto.None -> InputConfig.Assignment.None
                is AssignmentDto.Key -> InputConfig.Assignment.Key(altAssignment.deviceId, altAssignment.keyCode)
                is AssignmentDto.Axis -> InputConfig.Assignment.Axis(altAssignment.deviceId, altAssignment.axisCode, altAssignment.direction)
            }
        )
    }
}