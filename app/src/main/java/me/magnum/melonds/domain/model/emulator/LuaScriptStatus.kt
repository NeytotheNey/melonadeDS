package me.magnum.melonds.domain.model.emulator

/**
 * A Lua script loaded in the current session. [description] and [options] come from the script's header, and [lastError] is set if it was
 * switched off by an error. [optionValues] are the values it runs with, for every option in [options]
 */
data class LuaScriptStatus(
    val name: String,
    val description: String?,
    val isRunning: Boolean,
    val lastError: String?,
    val options: List<LuaScriptOption> = emptyList(),
    val optionValues: Map<String, String> = emptyMap(),
)
