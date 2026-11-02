package me.magnum.melonds.ui.romdetails.model

import me.magnum.melonds.domain.model.layout.LayoutConfiguration

/**
 * What the Mods tab's widescreen section needs besides the ROM config, both app-wide rather than per ROM.
 *
 * @param isWidescreenSupported Widescreen only works with the OpenGL and Compute renderers, a global setting
 * @param wideLayouts The wide layouts (LayoutConfiguration.isWide) the game can pick as its wide layout, or null until the layouts are read
 */
data class RomModsUiModel(
    val isWidescreenSupported: Boolean = true,
    val wideLayouts: List<LayoutConfiguration>? = null,
)
