package me.magnum.melonds.ui.emulator.model

/**
 * Requests from the game's Lua scripts, scripts stopping, and sessions starting and ending. They reach the screen in the order they happened,
 * which [LuaUiOverrides] relies on.
 */
sealed class LuaUiEvent {
    /**
     * A game or the firmware is about to start, before any of its scripts run. The screen shows the player's own values for it:
     * [screensSwapped] is the game's saved swap (always false for the firmware). They're the baselines of the session's script requests
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
