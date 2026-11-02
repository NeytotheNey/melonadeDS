package me.magnum.melonds.domain.model.emulator

/**
 * A snapshot of a script's melon.storage to save, made by native code; empty [data] means delete the file.
 */
class LuaScriptDataSnapshot(
    val gameKey: String,
    val scriptName: String,
    val data: ByteArray,
)
