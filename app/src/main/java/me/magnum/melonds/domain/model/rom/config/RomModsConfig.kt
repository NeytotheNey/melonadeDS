package me.magnum.melonds.domain.model.rom.config

import android.net.Uri

data class RomModsConfig(
    // Width of the top screen's 3D view in pixels. Null means widescreen is off
    val wideViewWidth: Int? = null,
    // Tree URI of the folder the game's scripts are read from, picked in the Mods tab. Null if none is linked
    val luaScriptFolder: Uri? = null,
    // The scripts found in that folder, kept in step with it by LuaScriptFolderSync
    val luaScripts: List<RomLuaScript> = emptyList(),
)
