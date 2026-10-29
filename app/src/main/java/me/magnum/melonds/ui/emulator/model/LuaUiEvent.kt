package me.magnum.melonds.ui.emulator.model

import me.magnum.melonds.ui.emulator.LuaUiSetting

/**
 * Requests from the game's Lua scripts, and scripts stopping. They reach the screen in the order they happened, which [LuaUiOverrides]
 * relies on.
 */
sealed class LuaUiEvent {
    /**
     * [error] is set if an error switched the script off, and null if the player did from the pause menu
     */
    data class ScriptStopped(val scriptId: Int, val scriptName: String, val error: String?) : LuaUiEvent()
    data class Request(val setting: LuaUiSetting, val scriptId: Int, val value: Boolean) : LuaUiEvent()
    /**
     * The session's scripts are gone, so whatever they changed is put back
     */
    data object SessionEnded : LuaUiEvent()
}
