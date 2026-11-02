package me.magnum.melonds.domain.model.layout

data class UILayout(
    val mainScreenLayout: ScreenLayout,
    val secondaryScreenLayout: ScreenLayout,
    // Widescreen ratio of this variant, or null for a normal 4:3 top screen
    val wideAspect: WidescreenAspect? = null,
)
