package me.magnum.melonds.domain.model.emulator

/**
 * A session script's header data, running state, last error and option values.
 */
data class LuaScriptStatus(
    val name: String,
    val description: String?,
    val isRunning: Boolean,
    val lastError: String?,
    val options: List<LuaScriptOption> = emptyList(),
    val optionValues: Map<String, String> = emptyMap(),
)
