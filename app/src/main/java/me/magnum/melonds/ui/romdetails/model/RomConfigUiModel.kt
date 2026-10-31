package me.magnum.melonds.ui.romdetails.model

import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.rom.config.RomModsConfig
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeMicSource
import java.util.UUID

data class RomConfigUiModel(
    val runtimeConsoleType: RuntimeConsoleType = RuntimeConsoleType.DEFAULT,
    val runtimeMicSource: RuntimeMicSource = RuntimeMicSource.DEFAULT,
    val layoutId: UUID? = null,
    val layoutName: String? = null,
    val gbaSlotConfig: RomGbaSlotConfigUiModel = RomGbaSlotConfigUiModel(),
    val customName: String? = null,
    val mods: RomModsConfig = RomModsConfig(),
    // Widescreen only works with the OpenGL and Compute renderers, a global setting
    val isWidescreenSupported: Boolean = true,
    // The wide layouts (LayoutConfiguration.isWide), which the game can pick as its wide layout. Null until the layouts are read
    val wideLayouts: List<LayoutConfiguration>? = null,
    val luaScripts: RomLuaScriptsUiModel = RomLuaScriptsUiModel(),
)
