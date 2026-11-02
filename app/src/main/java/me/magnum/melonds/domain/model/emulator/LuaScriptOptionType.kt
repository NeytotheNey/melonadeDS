package me.magnum.melonds.domain.model.emulator

/**
 * How a script sees an option's value in `melon.options`; [nativeId] must match the kOptionType constants in LuaScriptingJNI.cpp.
 */
enum class LuaScriptOptionType(val nativeId: Int) {
    BOOLEAN(0),
    INTEGER(1),
    NUMBER(2),
    STRING(3),
    // A layout option, seen by scripts as a boolean: 1 if the player picked a layout, 0 for "Don't change"
    LAYOUT(4),
}
