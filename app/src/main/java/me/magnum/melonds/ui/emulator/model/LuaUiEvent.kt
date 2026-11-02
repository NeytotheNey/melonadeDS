package me.magnum.melonds.ui.emulator.model

/**
 * Requests from the game's Lua scripts, scripts stopping, and sessions starting and ending, delivered in order since [LuaUiOverrides] relies on it.
 */
sealed class LuaUiEvent {
    /**
     * A game or the firmware is about to start, before any of its scripts run, with the game's saved swap as the scripts' baseline.
     */
    data class SessionStarted(val screensSwapped: Boolean) : LuaUiEvent()
    /**
     * [error] is set if an error switched the script off, and null if the player did from the pause menu
     */
    data class ScriptStopped(val scriptId: Int, val scriptName: String, val error: String?) : LuaUiEvent()
    /**
     * The player switched a stopped script back on, so its requests count again
     */
    data class ScriptStarted(val scriptId: Int) : LuaUiEvent()
    data class Request(val setting: LuaUiSetting, val scriptId: Int, val value: Boolean) : LuaUiEvent()
    /**
     * The session's scripts are gone, so whatever they changed is put back
     */
    data object SessionEnded : LuaUiEvent()
}
