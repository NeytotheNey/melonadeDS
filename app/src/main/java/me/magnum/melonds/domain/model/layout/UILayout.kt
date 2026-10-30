package me.magnum.melonds.domain.model.layout

data class UILayout(
    val mainScreenLayout: ScreenLayout,
    val secondaryScreenLayout: ScreenLayout,
    // Set on a widescreen variant: its top screen is placed for this ratio, and the game's 3D view is this wide while it's shown (with
    // widescreen on). Null for a normal 4:3 top screen
    val wideAspect: WidescreenAspect? = null,
)
