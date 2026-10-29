package me.magnum.melonds.ui.romdetails.model

import me.magnum.melonds.domain.model.emulator.LuaLayoutChoice
import me.magnum.melonds.impl.LuaScriptHeader

/**
 * What the Mods tab's scripts section needs besides the scripts in the ROM config.
 *
 * @param folderPath Where the linked script folder is, for showing. Null if no folder is linked
 * @param isFolderMissing The linked folder couldn't be read the last time it was checked (permission lost, folder moved, card removed). The
 * script list is kept as it was until the folder is picked again
 * @param isFolderEmpty The linked folder was read and has no `.lua` files at the top
 * @param areScriptsBlockedByHardcore RetroAchievements hardcore is on, which doesn't allow scripting, so no script runs
 * @param headers Each script's header (description and options), by file name. Scripts that couldn't be read aren't in it
 * @param dataSizes How many bytes each script has saved with melon.storage, by file name. Scripts that saved nothing aren't in it
 * @param layouts The player's layouts, which layout options offer. Null until they've been read
 */
data class RomLuaScriptsUiModel(
    val folderPath: String? = null,
    val isFolderMissing: Boolean = false,
    val isFolderEmpty: Boolean = false,
    val areScriptsBlockedByHardcore: Boolean = false,
    val headers: Map<String, LuaScriptHeader> = emptyMap(),
    val dataSizes: Map<String, Long> = emptyMap(),
    val layouts: List<LuaLayoutChoice>? = null,
)
