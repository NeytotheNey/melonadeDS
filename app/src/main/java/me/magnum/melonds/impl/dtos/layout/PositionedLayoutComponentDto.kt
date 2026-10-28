package me.magnum.melonds.impl.dtos.layout

import com.google.gson.annotations.SerializedName
import me.magnum.melonds.domain.model.layout.PositionedLayoutComponent
import me.magnum.melonds.utils.enumValueOfIgnoreCase

data class PositionedLayoutComponentDto(
    @SerializedName("rect")
    val rect: RectDto,
    @SerializedName("component")
    val component: String,
    @SerializedName("alpha")
    val alpha: Float? = null,
    @SerializedName("onTop")
    val onTop: Boolean? = null,
    @SerializedName("spacing")
    val spacing: Float? = null,
) {

    companion object {
        fun fromModel(positionedLayoutComponent: PositionedLayoutComponent): PositionedLayoutComponentDto {
            return PositionedLayoutComponentDto(
                RectDto.fromModel(positionedLayoutComponent.rect),
                positionedLayoutComponent.component.name,
                positionedLayoutComponent.alpha,
                positionedLayoutComponent.onTop,
                positionedLayoutComponent.spacing,
            )
        }
    }

    fun toModel(): PositionedLayoutComponent {
        return PositionedLayoutComponent(
            rect.toModel(),
            enumValueOfIgnoreCase(component),
            alpha ?: 1f,
            onTop ?: false,
            spacing?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f,
        )
    }
}