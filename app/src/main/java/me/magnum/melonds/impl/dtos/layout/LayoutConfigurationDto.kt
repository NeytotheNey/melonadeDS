package me.magnum.melonds.impl.dtos.layout

import com.google.gson.annotations.SerializedName
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.WidescreenAspect
import me.magnum.melonds.utils.enumValueOfIgnoreCase
import java.util.UUID

data class LayoutConfigurationDto(
    @SerializedName("id")
    val id: String?,
    @SerializedName("name")
    val name: String?,
    @SerializedName("type")
    val type: String,
    @SerializedName("orientation")
    val orientation: String,
    @SerializedName("useCustomOpacity")
    val useCustomOpacity: Boolean,
    @SerializedName("opacity")
    val opacity: Int,
    @SerializedName("layoutVariants")
    val layoutVariants: List<LayoutEntryDto>,
    // Only read: the ratio (WidescreenAspect.key) of the layouts saved while it was one per layout. It's now one per variant
    // (UILayoutDto.wideAspect), so it's copied to the saved variants on load and no longer saved
    @SerializedName("wideAspect")
    val wideAspect: String? = null,
) {

    companion object {
        fun fromModel(layoutConfiguration: LayoutConfiguration): LayoutConfigurationDto {
            return LayoutConfigurationDto(
                layoutConfiguration.id?.toString(),
                layoutConfiguration.name,
                layoutConfiguration.type.name,
                layoutConfiguration.orientation.name,
                layoutConfiguration.useCustomOpacity,
                layoutConfiguration.opacity,
                layoutConfiguration.layoutVariants.map {
                    LayoutEntryDto(UILayoutVariantDto.fromModel(it.key), UILayoutDto.fromModel(it.value))
                },
                null,
            )
        }
    }

    fun toModel(): LayoutConfiguration {
        val legacyWideAspect = WidescreenAspect.fromKey(wideAspect)
        return LayoutConfiguration(
            id?.let { UUID.fromString(it) },
            name,
            enumValueOfIgnoreCase(type),
            enumValueOfIgnoreCase(orientation),
            useCustomOpacity,
            opacity,
            layoutVariants.associate {
                val layout = it.layout.toModel()
                // Only variants with screens placed were widened. Background-only entries keep coming from the default layout
                val hasComponents = layout.mainScreenLayout.components != null || layout.secondaryScreenLayout.components != null
                it.variant.toModel() to if (layout.wideAspect == null && legacyWideAspect != null && hasComponents) layout.copy(wideAspect = legacyWideAspect) else layout
            },
        )
    }

    data class LayoutEntryDto(
        @SerializedName("variant")
        val variant: UILayoutVariantDto,
        @SerializedName("layout")
        val layout: UILayoutDto,
    )
}