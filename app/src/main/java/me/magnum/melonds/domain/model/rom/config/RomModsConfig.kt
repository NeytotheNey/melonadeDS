package me.magnum.melonds.domain.model.rom.config

import android.net.Uri
import me.magnum.melonds.domain.model.MAX_WIDE_VIEW_WIDTH
import me.magnum.melonds.domain.model.MIN_WIDE_VIEW_WIDTH
import java.util.UUID

data class RomModsConfig(
    // Width of the top screen's 3D view in pixels. Null means widescreen is off
    val wideViewWidth: Int? = null,
    // Tree URI of the folder the game's scripts are read from, picked in the Mods tab. Null if none is linked
    val luaScriptFolder: Uri? = null,
    // The scripts found in that folder, kept in step with it by LuaScriptFolderSync
    val luaScripts: List<RomLuaScript> = emptyList(),
    // The wide layout (LayoutConfiguration.isWide) that the game uses while widescreen is on. Null means automatic: the
    // normal layout with its top screen widened, at wideViewWidth
    val wideLayoutId: UUID? = null,
) {
    // wideViewWidth if the core takes it. Anything else counts as off (null), both at launch and in the Mods tab
    val validWideViewWidth: Int? get() = wideViewWidth?.takeIf { it in MIN_WIDE_VIEW_WIDTH..MAX_WIDE_VIEW_WIDTH && it % 2 == 0 }
}
