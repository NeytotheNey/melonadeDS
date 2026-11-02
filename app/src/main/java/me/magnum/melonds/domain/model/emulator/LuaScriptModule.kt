package me.magnum.melonds.domain.model.emulator

/**
 * A module the scripts can `require` by [name], with its [source] or null and a [leftOutReason]; read by native code.
 */
class LuaScriptModule(
    val name: String,
    val path: String,
    val source: ByteArray?,
    val leftOutReason: String?,
)
