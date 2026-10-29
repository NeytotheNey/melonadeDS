package me.magnum.melonds.impl.debug

import android.os.Build
import android.util.Log
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.repositories.SettingsRepository

/**
 * Markers and configuration dumps for the debug log. Everything here is skipped while the debug log is off, which [DebugLogRecorder] controls.
 * Nothing private goes in: no folder names or URIs, no firmware nickname, message or birthday, and nothing from RetroAchievements but the
 * hardcore toggle.
 */
object DebugLog {
    const val TAG = "melonade-debug"

    @Volatile
    var isEnabled = false
        private set

    /**
     * Turns the markers and the native side (core messages, CPU heartbeat) on or off.
     */
    fun setEnabled(enabled: Boolean) {
        isEnabled = enabled
        MelonEmulator.setDebugLogEnabled(enabled)
    }

    /**
     * Logs a marker, such as a save state load or a pause. The message is only built while the debug log is on.
     */
    inline fun event(message: () -> String) {
        if (isEnabled) {
            Log.i(TAG, message())
        }
    }

    /**
     * Logs everything in [configuration] that affects emulation, plus the settings that are read elsewhere, one line per value.
     *
     * @param reason What is happening, e.g. "rom launch" or "configuration update"
     * @param rom The ROM being run, or null for the firmware
     * @param cheatCount Number of cheats passed to the core, or null if unchanged
     */
    fun logConfiguration(reason: String, rom: Rom?, configuration: EmulatorConfiguration, cheatCount: Int?, settingsRepository: SettingsRepository) {
        if (!isEnabled) {
            return
        }

        val lines = mutableListOf<Pair<String, Any?>>()
        if (rom != null) {
            lines.add("rom_name" to rom.name)
            lines.add("rom_file" to rom.fileName)
            lines.add("rom_ra_hash" to rom.retroAchievementsHash)
            lines.add("rom_dsiware" to rom.isDsiWareTitle)
            lines.add("rom_console_override" to rom.config.runtimeConsoleType.name)
            lines.add("rom_mic_override" to rom.config.runtimeMicSource.name)
            // Class names are obfuscated in release builds, so the slot types are spelled out
            val gbaSlot = when (rom.config.gbaSlotConfig) {
                RomGbaSlotConfig.None -> "none"
                is RomGbaSlotConfig.GbaRom -> "gba_rom"
                RomGbaSlotConfig.MemoryExpansion -> "memory_expansion"
                RomGbaSlotConfig.RumblePak -> "rumble_pak"
                RomGbaSlotConfig.MotionPakHomebrew -> "motion_pak_homebrew"
                RomGbaSlotConfig.MotionPakRetail -> "motion_pak_retail"
            }
            lines.add("rom_gba_slot" to gbaSlot)
        } else {
            lines.add("firmware_boot" to configuration.consoleType.name)
        }

        fun found(uri: Any?) = if (uri != null) "found" else "missing"

        lines.add("console_type" to configuration.consoleType.name)
        if (configuration.useCustomBios || configuration.consoleType == ConsoleType.DSi) {
            lines.add("bios" to "custom files")
            if (configuration.consoleType == ConsoleType.DS) {
                lines.add("ds_bios7" to found(configuration.dsBios7Uri))
                lines.add("ds_bios9" to found(configuration.dsBios9Uri))
                lines.add("ds_firmware" to found(configuration.dsFirmwareUri))
            } else {
                lines.add("dsi_bios7" to found(configuration.dsiBios7Uri))
                lines.add("dsi_bios9" to found(configuration.dsiBios9Uri))
                lines.add("dsi_firmware" to found(configuration.dsiFirmwareUri))
                lines.add("dsi_nand" to found(configuration.dsiNandUri))
            }
        } else {
            lines.add("bios" to "built-in FreeBIOS and generated firmware")
        }
        lines.add("show_boot_screen" to configuration.showBootScreen)
        lines.add("firmware_language" to configuration.firmwareConfiguration.language)
        lines.add("firmware_randomize_mac" to configuration.firmwareConfiguration.randomizeMacAddress)

        lines.add("jit" to configuration.useJit)
        lines.add("device_64bit" to Build.SUPPORTED_64_BIT_ABIS.isNotEmpty())

        val renderer = configuration.rendererConfiguration
        lines.add("renderer" to renderer.renderer.name)
        lines.add("resolution_scaling" to renderer.resolutionScaling)
        lines.add("video_filtering" to renderer.videoFiltering.name)
        lines.add("threaded_rendering" to renderer.threadedRendering)
        lines.add("threaded_2d_rendering" to renderer.threaded2dRendering)
        lines.add("ds_screen_colors" to renderer.dsScreenColors)
        lines.add("screen_shadow" to renderer.screenShadow)

        lines.add("sound_enabled" to configuration.soundEnabled)
        lines.add("volume" to configuration.volume)
        lines.add("audio_interpolation" to configuration.audioInterpolation.name)
        lines.add("audio_bitrate" to configuration.audioBitrate.name)
        lines.add("audio_latency" to configuration.audioLatency.name)
        lines.add("mute_fast_forward_audio" to configuration.muteFastForwardAudio)
        lines.add("mic_source" to configuration.micSource.name)

        lines.add("fast_forward_multiplier" to configuration.fastForwardSpeedMultiplier)
        lines.add("sync_rtc_to_device" to configuration.syncRtcToDevice)
        lines.add("rewind_enabled" to configuration.rewindEnabled)
        lines.add("rewind_period_s" to configuration.rewindPeriodSeconds)
        lines.add("rewind_window_s" to configuration.rewindWindowSeconds)
        lines.add("cheats_enabled" to settingsRepository.areCheatsEnabled())
        if (cheatCount != null) {
            lines.add("active_cheats" to cheatCount)
        }

        lines.add("frame_pacing_vsync" to configuration.framePacingVsyncEnabled)
        lines.add("frame_pacing_buffer" to configuration.framePacingBufferEnabled)
        lines.add("frame_pacing_lock_60hz" to settingsRepository.isFramePacingLock60HzEnabled())
        lines.add("sustained_performance_mode" to settingsRepository.isSustainedPerformanceModeEnabled())
        lines.add("retroachievements_hardcore" to settingsRepository.isRetroAchievementsHardcoreEnabled())

        Log.i(TAG, "--- $reason ---")
        lines.forEach { (key, value) ->
            Log.i(TAG, "config $key=$value")
        }
    }
}
