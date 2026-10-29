package me.magnum.melonds.ui.romdetails.model

/**
 * What the Mods tab's scripts section needs besides the scripts in the ROM config.
 *
 * @param folderPath Where the linked script folder is, for showing. Null if no folder is linked
 * @param isFolderMissing The linked folder couldn't be read the last time it was checked (permission lost, folder moved, card removed). The
 * script list is kept as it was until the folder is picked again
 * @param isFolderEmpty The linked folder was read and has no `.lua` files at the top
 * @param areScriptsBlockedByHardcore RetroAchievements hardcore is on, which doesn't allow scripting, so no script runs
 * @param descriptions Each script's description from its header, by file name. Scripts without one aren't in it
 */
data class RomLuaScriptsUiModel(
    val folderPath: String? = null,
    val isFolderMissing: Boolean = false,
    val isFolderEmpty: Boolean = false,
    val areScriptsBlockedByHardcore: Boolean = false,
    val descriptions: Map<String, String> = emptyMap(),
)
