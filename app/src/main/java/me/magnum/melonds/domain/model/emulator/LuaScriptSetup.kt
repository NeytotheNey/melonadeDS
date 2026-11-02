package me.magnum.melonds.domain.model.emulator

/**
 * A script sent to the emulator at launch; read by native code, so the fields can't be renamed.
 *
 * @param savedData The script's melon.storage as native saved it last time, or null if it has none
 * @param dataGameKey Where the script's data is kept (see LuaScriptDataStore), sent back with each storage snapshot
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
