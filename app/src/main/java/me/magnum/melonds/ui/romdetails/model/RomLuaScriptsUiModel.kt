package me.magnum.melonds.ui.romdetails.model

/**
 * What the Mods → Scripts section needs besides the scripts in the ROM config.
 *
 * @param isScriptFolderKnown False until the ROM header has been read, since the game code in it names the script folder. Adding and
 * deleting scripts waits for it
 * @param areScriptsBlockedByHardcore RetroAchievements hardcore is on, which doesn't allow scripting, so no script runs
 * @param missingScripts Scripts in the config whose file isn't in app storage, after a settings restore for example
 * @param descriptions Each script's description from its header, by file name. Scripts without one aren't in it
 */
data class RomLuaScriptsUiModel(
    val isScriptFolderKnown: Boolean = false,
    val areScriptsBlockedByHardcore: Boolean = false,
    val missingScripts: Set<String> = emptySet(),
    val descriptions: Map<String, String> = emptyMap(),
)
