package me.magnum.melonds.ui.emulator.model

/**
 * Requests from the game's Lua scripts, and scripts being switched off after an error
 */
sealed class LuaUiEvent {
    data class ScriptStopped(val scriptName: String, val error: String) : LuaUiEvent()
    data class SwapScreens(val swapped: Boolean) : LuaUiEvent()
    data class ShowControls(val visible: Boolean) : LuaUiEvent()
    data class FastForward(val enabled: Boolean) : LuaUiEvent()
}
