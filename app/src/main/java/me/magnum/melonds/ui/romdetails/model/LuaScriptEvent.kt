package me.magnum.melonds.ui.romdetails.model

/**
 * Results of linking a script folder that the ROM details screen shows
 */
sealed class LuaScriptEvent {
    data object FolderPickFailed : LuaScriptEvent()
}
