package me.magnum.melonds.domain.model.rom.config

/**
 * A script in the game's folder with the player's option values by key; unknown keys are ignored.
 */
data class RomLuaScript(
    val fileName: String,
    val enabled: Boolean,
    val options: Map<String, String> = emptyMap(),
)
