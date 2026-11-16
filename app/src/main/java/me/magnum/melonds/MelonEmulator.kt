package me.magnum.melonds

import android.net.Uri
import me.magnum.melonds.common.camera.DSiCameraSource
import me.magnum.melonds.domain.model.Cheat
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.emulator.LuaScriptDataSnapshot
import me.magnum.melonds.domain.model.emulator.LuaScriptImage
import me.magnum.melonds.domain.model.emulator.LuaScriptModule
import me.magnum.melonds.domain.model.emulator.LuaScriptOptionValue
import me.magnum.melonds.domain.model.emulator.LuaScriptSetup
import me.magnum.melonds.domain.model.retroachievements.RASimpleAchievement
import me.magnum.melonds.domain.model.retroachievements.RASimpleLeaderboard
import me.magnum.melonds.domain.model.retroachievements.RASimpleRuntimeAchievement
import me.magnum.melonds.ui.emulator.render.FrameRenderCallback
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow
import java.nio.ByteBuffer

object MelonEmulator {
    // The perf frame sample layout and the flags must match PERF_FRAME_* and PERF_STATE_FLAG_* in MelonDSAndroidJNI.cpp (checked in init)
    const val PERF_FRAME_SAMPLE_FIELDS = 10
    const val PERF_FRAME_FLAG_FAST_FORWARD = 1L
    const val PERF_FRAME_FLAG_AFTER_PAUSE = 2L
    const val PERF_FRAME_FLAG_PACING_LOCKED = 4L
    const val PERF_FRAME_FLAG_AUDIO_BUFFER_FULL = 8L
    // Locked frames also store their refresh divisor (how many display refreshes each frame lasts) in these bits of the flags
    const val PERF_FRAME_PACING_DIVISOR_SHIFT = 8
    const val PERF_FRAME_PACING_DIVISOR_MASK = 0xFFL
    const val PERF_STATE_FLAG_PAUSED = 1
    const val PERF_STATE_FLAG_DS_SLEEP = 2
    const val PERF_STATE_FLAG_FAST_FORWARD = 4
    // The layout of getAudioDiagnosticsTotals() must match AudioDiagnostics::Total in AudioDiagnostics.h (checked in init)
    const val AUDIO_TOTAL_CALLBACKS = 0
    const val AUDIO_TOTAL_CALLBACK_FRAMES = 1
    const val AUDIO_TOTAL_UNDERRUN_CALLBACKS = 2
    const val AUDIO_TOTAL_SILENCE_FRAMES = 3
    const val AUDIO_TOTAL_MAX_CALLBACK_GAP_NS = 4
    const val AUDIO_TOTAL_MAX_CALLBACK_FRAMES = 5
    const val AUDIO_TOTAL_XRUNS = 6
    const val AUDIO_TOTAL_STREAM_OPENS = 7
    const val AUDIO_TOTAL_STREAM_ERRORS = 8
    const val AUDIO_TOTAL_FAST_FORWARD_UNDERRUN_CALLBACKS = 9
    const val AUDIO_TOTAL_FAST_FORWARD_SILENCE_FRAMES = 10
    // The AAudio buffer size now and when the stream opened (frames, -1 if there never was a stream), and how often it grew after an xrun
    const val AUDIO_TOTAL_BUFFER_SIZE = 11
    const val AUDIO_TOTAL_BUFFER_SIZE_AT_OPEN = 12
    const val AUDIO_TOTAL_BUFFER_GROWS = 13
    // Silence played while priming (after a start, or after the buffer ran dry), not counted as underruns, and how often it ran dry
    const val AUDIO_TOTAL_PRIME_WAIT_FRAMES = 14
    const val AUDIO_TOTAL_REPRIMES = 15
    // Trims the callback made when priming ended, and the frames they dropped
    const val AUDIO_TOTAL_PRIME_TRIMS = 16
    const val AUDIO_TOTAL_PRIME_TRIM_FRAMES = 17
    // The four priming counts since the emulation session's audio was set up, so including the startup prime before a capture began
    const val AUDIO_TOTAL_SESSION_PRIME_WAIT_FRAMES = 18
    const val AUDIO_TOTAL_SESSION_REPRIMES = 19
    const val AUDIO_TOTAL_SESSION_PRIME_TRIMS = 20
    const val AUDIO_TOTAL_SESSION_PRIME_TRIM_FRAMES = 21
    const val AUDIO_TOTAL_COUNT = 22
    const val AUDIO_MAX_STREAM_EVENTS = 32

    init {
        // The layouts above are mirrored by hand, so a mismatch fails here instead of producing garbled logs
        check(getPerformanceFrameSampleFieldCount() == PERF_FRAME_SAMPLE_FIELDS) { "PERF_FRAME_SAMPLE_FIELDS doesn't match MelonDSAndroidJNI.cpp" }
        check(getAudioDiagnosticsTotalCount() == AUDIO_TOTAL_COUNT) { "AUDIO_TOTAL_COUNT doesn't match AudioDiagnostics.h" }
    }

    enum class LoadResult(val isTerminal: Boolean) {
        SUCCESS(false),
        SUCCESS_GBA_FAILED(false),
        NDS_FAILED(true),
        BIOS_FAILED(true)
    }

    enum class FirmwareLoadResult {
        SUCCESS,
        BIOS9_MISSING,
        BIOS9_BAD,
        BIOS7_MISSING,
        BIOS7_BAD,
        FIRMWARE_MISSING,
        FIRMWARE_BAD,
        FIRMWARE_NOT_BOOTABLE,
        DSI_BIOS9_MISSING,
        DSI_BIOS9_BAD,
        DSI_BIOS7_MISSING,
        DSI_BIOS7_BAD,
        DSI_NAND_MISSING,
        DSI_NAND_BAD
    }

    enum class GbaSlotType {
        NONE,
        GBA_ROM,
        RUMBLE_PAK,
        MEMORY_EXPANSION,
        MOTION_PAK_HOMEBREW,
        MOTION_PAK_RETAIL,
    }

	external fun setupEmulator(
        emulatorConfiguration: EmulatorConfiguration,
        dsiCameraSource: DSiCameraSource?,
        screenshotBuffer: ByteBuffer,
    )

    external fun setupCheats(cheats: Array<Cheat>)

    external fun setupAchievements(achievements: Array<RASimpleAchievement>, leaderboards: Array<RASimpleLeaderboard>, richPresenceScript: String?)

    external fun unloadRetroAchievementsData()

    external fun getRichPresenceStatus(): String?

    external fun getRuntimeAchievements(): Array<RASimpleRuntimeAchievement>

    /**
     * Gives the emulator the game's Lua scripts with the [modules] and [images] they share; call after [setupEmulator] and before
     * [startEmulation].
     */
    external fun setupLuaScripts(scripts: Array<LuaScriptSetup>, modules: Array<LuaScriptModule>, images: Array<LuaScriptImage>, areStateLoadsAllowed: Boolean)

    /**
     * Switches off the script at [scriptId] (its position in the list given to [setupLuaScripts]) before the next frame runs.
     */
    external fun disableLuaScript(scriptId: Int)

    /**
     * Switches a stopped script back on before the next frame, loading it again and calling its on_load().
     */
    external fun enableLuaScript(scriptId: Int)

    /**
     * Restarts (or starts) the script at [scriptId] with new [options] before the next frame, keeping its storage.
     */
    external fun restartLuaScript(scriptId: Int, options: Array<LuaScriptOptionValue>)

    /**
     * Takes the scripts' storage snapshots that are waiting to be saved, also after [stopEmulation].
     */
    external fun drainLuaData(): Array<LuaScriptDataSnapshot>?

    /**
     * Controller sticks for Lua scripts, from -1 to 1 (down and right are positive).
     */
    external fun updateLuaSticks(leftX: Float, leftY: Float, rightX: Float, rightY: Float)

    /**
     * The WideMelon camera's window for melon.wide.camera(): its offset from center in DS pixels and its zoom (1 = native).
     */
    external fun updateWideCamera(x: Float, zoom: Float)

	fun loadRom(romUri: Uri, sramUri: Uri, gbaSlotType: GbaSlotType, gbaRomUri: Uri?, gbaSramUri: Uri?): LoadResult {
        val loadResult = loadRomInternal(romUri.toString(), sramUri.toString(), gbaSlotType.ordinal, gbaRomUri?.toString(), gbaSramUri?.toString())
        return when (loadResult) {
            0 -> LoadResult.SUCCESS
            1 -> LoadResult.SUCCESS_GBA_FAILED
            2 -> LoadResult.NDS_FAILED
            3 -> LoadResult.BIOS_FAILED
            else -> throw RuntimeException("Unknown load result")
        }
    }

    fun bootFirmware(): FirmwareLoadResult {
        val loadResult = bootFirmwareInternal()
        return FirmwareLoadResult.entries[loadResult]
    }

    private external fun loadRomInternal(romPath: String, sramPath: String, gbaSlotType: Int, gbaRomPath: String?, gbaSramPath: String?): Int

    private external fun bootFirmwareInternal(): Int

	external fun startEmulation()

    /**
     * Presents the frame for the vsync at [vsyncTimeNs] (CLOCK_MONOTONIC), waiting until [deadlineNs] (if above 0) for a due frame before
     * repeating the previous one.
     */
    external fun presentFrame(deadlineNs: Long, vsyncTimeNs: Long, frameRenderCallback: FrameRenderCallback)

    /**
     * Publishes a display vsync timestamp (CLOCK_MONOTONIC) for frame pacing to lock to; call it from the vsync callback, always on the same
     * thread.
     */
    external fun onDisplayVsync(vsyncTimeNs: Long)

	external fun getFPS(): Float

    /**
     * Enables or disables per-frame performance capture; call it from the same thread that calls [drainPerformanceFrameSamples].
     */
    external fun setPerformanceCaptureEnabled(enabled: Boolean)

    /**
     * Turns the native side of the debug log on or off: core messages under the melonade-core tag, the CPU heartbeat and the exception checks.
     */
    external fun setDebugLogEnabled(enabled: Boolean)

    /**
     * Debug builds only (a no-op in release builds): the debug.melonade.* switches for the next game, as [DebugSwitches.Result.toNativeString]
     * gives them. Call it before [setupEmulator]; an empty string clears the last ones.
     */
    external fun setDebugSwitches(switches: String)

    /**
     * Debug builds only (empty in release builds): the switches the running game read that had a value, and where each came from (see
     * DebugProperties::describeActive).
     */
    external fun getActiveDebugSwitches(): String

    /**
     * Copies pending frame samples into [buffer] and returns how many, each [PERF_FRAME_SAMPLE_FIELDS] entries in this order:
     * frame end timestamp (ns, CLOCK_MONOTONIC), work duration (ns), sleep duration (ns), interval since the previous frame end (ns), emulated line
     * count, flags ([PERF_FRAME_FLAG_FAST_FORWARD], [PERF_FRAME_FLAG_AFTER_PAUSE], [PERF_FRAME_FLAG_PACING_LOCKED], the refresh divisor at
     * [PERF_FRAME_PACING_DIVISOR_SHIFT] and [PERF_FRAME_FLAG_AUDIO_BUFFER_FULL]), the audio buffer fill after the frame's sleep (frames), the
     * frames the audio callback filled with silence or repeats since the previous sample, how far the audio output skew is from its base
     * (ppm, set by the callback's drift control), and the speed paced fast-forward really reached (thousandths, -1 if unknown or not
     * fast-forwarding).
     */
    external fun drainPerformanceFrameSamples(buffer: LongArray): Int

    /**
     * Number of frame samples that were dropped because the capture buffer was full since capture was last enabled.
     */
    external fun getDroppedPerformanceFrameSampleCount(): Long

    private external fun getPerformanceFrameSampleFieldCount(): Int

    private external fun getAudioDiagnosticsTotalCount(): Int

    /**
     * Fills [totals] ([AUDIO_TOTAL_COUNT] entries, see AUDIO_TOTAL_*) with the audio output counters, counted only while the debug log or a
     * performance capture is on.
     */
    external fun getAudioDiagnosticsTotals(totals: LongArray)

    /**
     * The audio output stream's facts as space-separated key=value pairs, or null if no stream is open.
     */
    external fun getAudioStreamInfo(): String?

    /**
     * Takes the audio stream events recorded since the performance capture started into [timestamps], [names] and [details], returning how many.
     */
    external fun drainAudioStreamEvents(timestamps: LongArray, names: Array<String?>, details: Array<String?>): Int

    /**
     * Current emulation state as [PERF_STATE_FLAG_PAUSED], [PERF_STATE_FLAG_DS_SLEEP] and [PERF_STATE_FLAG_FAST_FORWARD] flags.
     */
    external fun getPerformanceEmulationStateFlags(): Int

	external fun pauseEmulation()

	external fun resumeEmulation()

    external fun resetEmulation()

	external fun stopEmulation()

    fun saveState(path: Uri): Boolean {
        return saveStateInternal(path.toString())
    }

    private external fun saveStateInternal(path: String): Boolean

    fun loadState(path: Uri): Boolean {
        return loadStateInternal(path.toString())
    }

    private external fun loadStateInternal(path: String): Boolean

    external fun loadRewindState(rewindSaveState: RewindSaveState): Boolean

    external fun getRewindWindow(): RewindWindow

	external fun onScreenTouch(x: Int, y: Int)

	external fun onScreenRelease()

	fun onInputDown(input: Input) {
        onKeyPress(input.keyCode)
    }

	fun onInputUp(input: Input) {
        onKeyRelease(input.keyCode)
    }

    private external fun onKeyPress(key: Int)

    private external fun onKeyRelease(key: Int)

    external fun takeScreenshot(): Boolean

    external fun setFastForwardEnabled(enabled: Boolean)

    external fun setMicrophoneEnabled(enabled: Boolean)

    external fun updateEmulatorConfiguration(emulatorConfiguration: EmulatorConfiguration)

    external fun updateMotionData(ax: Float, ay: Float, az: Float, rx: Float, ry: Float, rz: Float)

    /**
     * Sets the top screen's 3D view width before the next frame (like EmulatorConfiguration.wideViewWidth, 0 for the normal width).
     */
    external fun setWideViewWidth(width: Int)

    /**
     * Sets how many times the native size the screens are shown at, at most, over every surface (0 when unknown), for the output cap
     * (GPU::OutputCap, on by default, debug.melonade.outcap=0 turns it off), which caps the frame's scale by it. Only call it when the value
     * changes; the emulator applies it once it holds for a while.
     */
    external fun setOutputScaleNeed(need: Int)

    /**
     * Sets whether any surface debands the frame (its renderer configuration's deband), for the output cap: with deband on it may also cap
     * at a scale that doesn't divide the internal one (8x to 6x, GPU::OutputCapDeband; debug.melonade.outcap=3 always does, 5 never). Only
     * call it when the value changes; the emulator applies it along with the need, once both hold for a while.
     */
    external fun setOutputDeband(deband: Boolean)
}