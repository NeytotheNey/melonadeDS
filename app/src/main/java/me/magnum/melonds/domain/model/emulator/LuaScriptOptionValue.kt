package me.magnum.melonds.domain.model.emulator

/**
 * One value of `melon.options`, typed the way the script sees it; read by native code, so the fields can't be renamed.
 */
class LuaScriptOptionValue(
    val key: String,
    val optionType: LuaScriptOptionType,
    val integer: Long = 0,
    val number: Double = 0.0,
    val text: ByteArray? = null,
) {
    // The type as native reads it
    val type: Int = optionType.nativeId
}
