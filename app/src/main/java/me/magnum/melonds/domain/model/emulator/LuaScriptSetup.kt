package me.magnum.melonds.domain.model.emulator

/**
 * A script sent to the emulator at launch. Read by native code, so the fields can't be renamed. Later phases add fields here (storage,
 * options)
 */
class LuaScriptSetup(
    val name: String,
    // Bytes, since modified UTF-8 can't carry everything a Lua file may contain
    val source: ByteArray,
)

/**
 * A module the session's scripts can `require` by [name], from the file at [path] in the script folder. [source] is null when it was left
 * out, and [leftOutReason] then says why. Read by native code
 */
class LuaScriptModule(
    val name: String,
    val path: String,
    val source: ByteArray?,
    val leftOutReason: String?,
)

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
