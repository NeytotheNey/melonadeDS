package me.magnum.melonds.impl.dtos.layout

import com.google.gson.Gson
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.WidescreenAspect
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class LayoutConfigurationDtoTest {

    private val gson = Gson()

    private fun variant(width: Int) = UILayoutVariantDto(
        PointDto(width, 1000),
        InsetsDto(0, 0, 0, 0),
        "PORTRAIT",
        emptyList(),
        LayoutDisplayPairDto(LayoutDisplayDto(0, "BUILT_IN", width, 1000), null),
    )

    private fun screenLayout(components: List<PositionedLayoutComponentDto>?) = ScreenLayoutDto(null, "STRETCH", components)

    private val topScreen = PositionedLayoutComponentDto(RectDto(0, 0, 256, 192), "TOP_SCREEN")

    private fun layout(components: List<PositionedLayoutComponentDto>?, wideAspect: String? = null) =
        UILayoutDto(screenLayout(components), screenLayout(null), wideAspect)

    private fun configuration(entries: List<LayoutConfigurationDto.LayoutEntryDto>, wideAspect: String?) =
        LayoutConfigurationDto("8a3c5b0e-2f4d-4e61-9a8b-1c2d3e4f5a6b", "Wide", "CUSTOM", "FOLLOW_SYSTEM", false, 50, entries, wideAspect)

    private fun load(dto: LayoutConfigurationDto) = gson.fromJson(gson.toJson(dto), LayoutConfigurationDto::class.java).toModel()

    @Test
    fun legacyWideAspectIsCopiedToVariantsWithComponents() {
        val config = load(
            configuration(
                listOf(
                    LayoutConfigurationDto.LayoutEntryDto(variant(1000), layout(listOf(topScreen))),
                    // Only a background: stays a normal variant
                    LayoutConfigurationDto.LayoutEntryDto(variant(1100), layout(null)),
                    // Already has its own ratio, which wins
                    LayoutConfigurationDto.LayoutEntryDto(variant(1200), layout(listOf(topScreen), wideAspect = "21:9")),
                ),
                wideAspect = "16:10",
            )
        )

        val aspects = config.layoutVariants.entries.associate { it.key.uiSize.x to it.value.wideAspect }
        assertEquals(mapOf(1000 to WidescreenAspect.RATIO_16_10, 1100 to null, 1200 to WidescreenAspect.RATIO_21_9), aspects)
    }

    @Test
    fun unknownLegacyWideAspectLeavesVariantsNormal() {
        val config = load(configuration(listOf(LayoutConfigurationDto.LayoutEntryDto(variant(1000), layout(listOf(topScreen)))), wideAspect = "32:9"))

        assertNull(config.layoutVariants.values.single().wideAspect)
    }

    @Test
    fun legacyWideAspectIsNotSavedAgain() {
        val config = load(configuration(listOf(LayoutConfigurationDto.LayoutEntryDto(variant(1000), layout(listOf(topScreen)))), wideAspect = "16:10"))

        val saved = LayoutConfigurationDto.fromModel(config)

        assertNull(saved.wideAspect)
        assertEquals("16:10", saved.layoutVariants.single().layout.wideAspect)
        assertEquals(config, load(saved))
    }

    @Test
    fun spacingIsClampedOnLoadAndKeptThroughASave() {
        val spacings = listOf(-0.5f, 0.25f, 1.5f, Float.NaN, Float.POSITIVE_INFINITY, null)
        val components = spacings.map { PositionedLayoutComponentDto(RectDto(0, 0, 100, 100), "DPAD", spacing = it) }
        // Gson can't write NaN or infinity by default, so the DTO is converted directly here
        val config = configuration(listOf(LayoutConfigurationDto.LayoutEntryDto(variant(1000), layout(components))), wideAspect = null).toModel()

        val loaded = config.layoutVariants.values.single().mainScreenLayout.components!!
        assertEquals(listOf(0f, 0.25f, 1f, 0f, 0f, 0f), loaded.map { it.spacing })
        assertEquals(LayoutComponent.DPAD, loaded.first().component)

        assertEquals(config, load(LayoutConfigurationDto.fromModel(config)))
    }
}
