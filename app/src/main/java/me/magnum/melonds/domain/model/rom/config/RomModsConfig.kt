package me.magnum.melonds.domain.model.rom.config

import android.net.Uri
import me.magnum.melonds.domain.model.MAX_WIDE_VIEW_WIDTH
import me.magnum.melonds.domain.model.MIN_WIDE_VIEW_WIDTH
import java.util.UUID

data class RomModsConfig(
    // Width of the top screen's 3D view in pixels, or null when widescreen is off
    val wideViewWidth: Int? = null,
    // Tree URI of the folder the game's scripts are read from, or null if none is linked
    val luaScriptFolder: Uri? = null,
    // The scripts found in that folder, kept in step with it by LuaScriptFolderSync
    val luaScripts: List<RomLuaScript> = emptyList(),
    // Wide layout used while widescreen is on, or null for automatic
    val wideLayoutId: UUID? = null,
) {
    // wideViewWidth if the core takes it, otherwise null (off)
    val validWideViewWidth: Int? get() = wideViewWidth?.takeIf { it in MIN_WIDE_VIEW_WIDTH..MAX_WIDE_VIEW_WIDTH && it % 2 == 0 }
}
