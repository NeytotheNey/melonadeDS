package me.magnum.melonds.domain.model.emulator

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
