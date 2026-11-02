package me.magnum.melonds.domain.model.emulator

/**
 * One value of `melon.options`, typed the way the script sees it. Read by native code: [type] is one of the constants below. Booleans and
 * integers are in [integer], numbers in [number], and strings in [text] as UTF-8
 */
class LuaScriptOptionValue(
    val key: String,
    val type: Int,
    val integer: Long = 0,
    val number: Double = 0.0,
    val text: ByteArray? = null,
) {
    companion object {
        const val TYPE_BOOLEAN = 0
        const val TYPE_INTEGER = 1
        const val TYPE_NUMBER = 2
        const val TYPE_STRING = 3
        // A layout option: [integer] is 1 if the player picked a layout, 0 for "Don't change". Scripts see a boolean
        const val TYPE_LAYOUT = 4
    }
}
