package me.magnum.melonds.domain.model.rom.config

data class RomModsConfig(
    // Width of the top screen's 3D view in pixels. Null means widescreen is off
    val wideViewWidth: Int? = null,
    val luaScripts: List<RomLuaScript> = emptyList(),
)
