package me.magnum.melonds.domain.model.emulator

/**
 * A script sent to the emulator at launch. Read by native code, so the fields can't be renamed.
 *
 * @param savedData The script's melon.storage as native saved it last time, or null if it has none
 * @param dataGameKey Where the script's data is kept (see LuaScriptDataStore). Native sends it back with each storage snapshot
 * @param options The values of the options the script declares, checked against the declarations
 */
class LuaScriptSetup(
    val name: String,
    // Bytes, since modified UTF-8 can't carry everything a Lua file may contain
    val source: ByteArray,
    val savedData: ByteArray?,
    val dataGameKey: String,
    val options: Array<LuaScriptOptionValue>,
)

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

/**
 * A snapshot of a script's melon.storage that native wants saved. [data] is empty when the storage is, in which case the file can go. Made
 * by native code
 */
class LuaScriptDataSnapshot(
    val gameKey: String,
    val scriptName: String,
    val data: ByteArray,
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
