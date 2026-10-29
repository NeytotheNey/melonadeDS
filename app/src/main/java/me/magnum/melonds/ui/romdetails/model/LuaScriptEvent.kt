package me.magnum.melonds.ui.romdetails.model

import android.net.Uri

/**
 * Results of adding or deleting a Lua script that the ROM details screen shows
 */
sealed class LuaScriptEvent {
    data class ConfirmReplace(val uri: Uri, val fileName: String) : LuaScriptEvent()
    data object NotLuaScript : LuaScriptEvent()
    data object TooLarge : LuaScriptEvent()
    data object AddFailed : LuaScriptEvent()
    data object DeleteFailed : LuaScriptEvent()
}
