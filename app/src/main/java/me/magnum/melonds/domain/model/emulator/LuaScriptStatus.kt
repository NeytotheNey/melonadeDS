package me.magnum.melonds.domain.model.emulator

/**
 * A Lua script loaded in the current session. [description] comes from the script's header, and [lastError] is set if it was switched off by
 * an error
 */
data class LuaScriptStatus(
    val name: String,
    val description: String?,
    val isRunning: Boolean,
    val lastError: String?,
)
