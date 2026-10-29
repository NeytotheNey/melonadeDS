package me.magnum.melonds.domain.model.rom.config

/**
 * A script in the game's linked folder. [options] are the values the player picked for the options the script declares (see
 * LuaScriptHeader), by option key, as text. Keys the script doesn't declare (anymore) are ignored, and a missing or bad value means the
 * option's default
 */
data class RomLuaScript(
    val fileName: String,
    val enabled: Boolean,
    val options: Map<String, String> = emptyMap(),
)
