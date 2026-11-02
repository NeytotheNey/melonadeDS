package me.magnum.melonds.impl.diagnostics

/**
 * The logcat tags the app and its native code write under (MelonLog.h on the native side), listed in the debug log's header.
 */
internal object LogTags {
    const val DEBUG = "melonade-debug"
    const val CPU = "melonade-cpu"
    const val AUDIO = "melonade-audio"
    const val LUA = "melonade-lua"
    const val CORE = "melonade-core"

    val described = listOf(
        DEBUG to "markers, game configuration, fold and display changes",
        CPU to "heartbeat, exceptions, GPU state",
        AUDIO to "audio stream, dropouts, buffer fill",
        LUA to "scripts",
        CORE to "emulator core messages",
    )
}
