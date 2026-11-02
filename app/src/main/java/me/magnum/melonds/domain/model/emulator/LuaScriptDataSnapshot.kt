package me.magnum.melonds.domain.model.emulator

/**
 * A snapshot of a script's melon.storage that native wants saved. [data] is empty when the storage is, in which case the file can go. Made
 * by native code
 */
class LuaScriptDataSnapshot(
    val gameKey: String,
    val scriptName: String,
    val data: ByteArray,
)
