package me.magnum.melonds.domain.model.emulator

/**
 * An image the scripts can load by [path], as unpremultiplied 0xAARRGGBB [pixels] or null with a [leftOutReason]; read by native code.
 */
class LuaScriptImage(
    val path: String,
    val width: Int,
    val height: Int,
    val pixels: IntArray?,
    val leftOutReason: String?,
)
