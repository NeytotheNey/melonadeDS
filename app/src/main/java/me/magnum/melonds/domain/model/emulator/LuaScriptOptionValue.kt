package me.magnum.melonds.domain.model.emulator

/**
 * One value of `melon.options`, typed the way the script sees it. Read by native code, so the fields can't be renamed. Booleans and
 * integers are in [integer], numbers in [number], and strings in [text] as UTF-8
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
