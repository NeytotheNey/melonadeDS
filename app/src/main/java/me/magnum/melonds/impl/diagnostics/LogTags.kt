package me.magnum.melonds.impl.diagnostics

/**
 * The logcat tags listed in the debug log's header; the native side writes under the same names (MelonLog.h), plus melonade and melonade-gl.
 */
internal object LogTags {
    const val DEBUG = "melonade-debug"
    const val CPU = "melonade-cpu"
    const val AUDIO = "melonade-audio"
    const val LUA = "melonade-lua"
    const val CORE = "melonade-core"
    const val SVG = "melonade-svg"

    val described = listOf(
        DEBUG to "markers, game configuration, fold and display changes",
        CPU to "heartbeat, exceptions, GPU state",
        AUDIO to "audio stream, dropouts, buffer fill",
        LUA to "scripts",
        CORE to "emulator core messages",
        SVG to "SVG background load timings",
    )
}
