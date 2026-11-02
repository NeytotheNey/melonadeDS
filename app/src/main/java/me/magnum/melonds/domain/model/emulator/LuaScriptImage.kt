package me.magnum.melonds.domain.model.emulator

/**
 * An image the session's scripts can load by its [path] in the script folder: [pixels] are [width] x [height] 0xAARRGGBB ints, not
 * premultiplied. [pixels] is null when it was left out, and [leftOutReason] then says why. Read by native code
 */
class LuaScriptImage(
    val path: String,
    val width: Int,
    val height: Int,
    val pixels: IntArray?,
    val leftOutReason: String?,
)
