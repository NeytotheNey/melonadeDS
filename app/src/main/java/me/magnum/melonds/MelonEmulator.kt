package me.magnum.melonds

import android.net.Uri
import me.magnum.melonds.common.camera.DSiCameraSource
import me.magnum.melonds.domain.model.Cheat
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.retroachievements.RASimpleAchievement
import me.magnum.melonds.domain.model.retroachievements.RASimpleLeaderboard
import me.magnum.melonds.domain.model.retroachievements.RASimpleRuntimeAchievement
import me.magnum.melonds.ui.emulator.render.FrameRenderCallback
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow
import java.nio.ByteBuffer

object MelonEmulator {
    const val PERF_FRAME_SAMPLE_FIELDS = 6
    const val PERF_FRAME_FLAG_FAST_FORWARD = 1L
    const val PERF_FRAME_FLAG_AFTER_PAUSE = 2L
    const val PERF_STATE_FLAG_PAUSED = 1
    const val PERF_STATE_FLAG_DS_SLEEP = 2

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

    external fun presentFrame(deadlineNs: Long, frameRenderCallback: FrameRenderCallback)

	external fun getFPS(): Float

    /**
     * Enables or disables per-frame performance capture on the emulator thread. Enabling it discards any pending samples. Must be called from the
     * same thread (or serialized context) that calls [drainPerformanceFrameSamples].
     */
    external fun setPerformanceCaptureEnabled(enabled: Boolean)

    /**
     * Copies pending frame samples into [buffer] and returns the number of samples copied. Each sample takes [PERF_FRAME_SAMPLE_FIELDS] entries:
     * frame end timestamp (ns, CLOCK_MONOTONIC), work duration (ns), sleep duration (ns), interval since the previous frame end (ns), emulated line
     * count and flags ([PERF_FRAME_FLAG_FAST_FORWARD], [PERF_FRAME_FLAG_AFTER_PAUSE]).
     */
    external fun drainPerformanceFrameSamples(buffer: LongArray): Int

    /**
     * Number of frame samples that were dropped because the capture buffer was full since capture was last enabled.
     */
    external fun getDroppedPerformanceFrameSampleCount(): Long

    /**
     * Current emulation state as [PERF_STATE_FLAG_PAUSED] and [PERF_STATE_FLAG_DS_SLEEP] flags. Used by the performance logger to tell periods
     * where no new frames are expected (emulator paused, DS lid closed) apart from real stalls.
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
}