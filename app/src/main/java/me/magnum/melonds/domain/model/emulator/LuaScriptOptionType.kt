package me.magnum.melonds.domain.model.emulator

/**
 * How a script sees an option's value in `melon.options`. [nativeId] is what native reads from [LuaScriptOptionValue.type], paired with the
 * kOptionType constants in LuaScriptingJNI.cpp
 */
enum class LuaScriptOptionType(val nativeId: Int) {
    BOOLEAN(0),
    INTEGER(1),
    NUMBER(2),
    STRING(3),
    // A layout option: the value is 1 if the player picked a layout, 0 for "Don't change". Scripts see a boolean
    LAYOUT(4),
}
