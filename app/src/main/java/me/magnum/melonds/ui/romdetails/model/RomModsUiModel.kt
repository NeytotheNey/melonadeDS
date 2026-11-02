package me.magnum.melonds.ui.romdetails.model

import me.magnum.melonds.domain.model.layout.LayoutConfiguration

/**
 * What the Mods tab's widescreen section needs besides the ROM config. Both are app-wide, not per ROM.
 *
 * @param isWidescreenSupported Widescreen only works with the OpenGL and Compute renderers, a global setting
 * @param wideLayouts The wide layouts (LayoutConfiguration.isWide), which the game can pick as its wide layout. Null until the layouts are read
 */
data class RomModsUiModel(
    val isWidescreenSupported: Boolean = true,
    val wideLayouts: List<LayoutConfiguration>? = null,
)
