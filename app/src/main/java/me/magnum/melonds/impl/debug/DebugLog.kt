package me.magnum.melonds.impl.debug

import android.util.Log
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.utils.supportsJit

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
        if (enabled) {
            synchronized(currentStates) {
                currentStates.forEach { (key, value) -> Log.i(TAG, "$key: $value") }
            }
        }
    }

    /**
     * Logs a marker, such as a save state load or a pause. The message is only built while the debug log is on.
     */
    inline fun event(message: () -> String) {
        if (isEnabled) {
            Log.i(TAG, message())
        }
    }

    // Last value reported through stateChange() for each key, kept while the debug log is off so that a new recording starts with them
    private val currentStates = LinkedHashMap<String, String>()

    /**
     * Logs "[key]: [value]" if [value] differs from the last value reported for [key], such as the fold posture or the display in use.
     * Returns whether it differed. [me.magnum.melonds.impl.perf.PerformanceLogger.logStateChange] is the entry point, so that both logs see every change.
     */
    fun stateChange(key: String, value: String): Boolean {
        synchronized(currentStates) {
            if (currentStates.put(key, value) == value) {
                return false
            }
            if (isEnabled) {
                Log.i(TAG, "$key: $value")
            }
            return true
        }
    }

    /**
     * The last value reported for each key, in the order the keys were first reported.
     */
    fun currentStates(): Map<String, String> {
        synchronized(currentStates) {
            return LinkedHashMap(currentStates)
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

        lines.add("device_64bit" to supportsJit())
        // The launched configuration copies these from the settings repository, so they're the values the performance log reports too
        lines.addAll(DiagnosticsInfo.settings(configuration.rendererConfiguration, settingsRepository))

        lines.add("volume" to configuration.volume)
        lines.add("audio_interpolation" to configuration.audioInterpolation.name)
        lines.add("audio_bitrate" to configuration.audioBitrate.name)
        lines.add("audio_latency" to configuration.audioLatency.name)
        lines.add("mute_fast_forward_audio" to configuration.muteFastForwardAudio)
        lines.add("mic_source" to configuration.micSource.name)

        lines.add("sync_rtc_to_device" to configuration.syncRtcToDevice)
        lines.add("lua_drawings_in_screenshots" to configuration.luaDrawingsInScreenshots)
        lines.add("wide_view_width" to configuration.wideViewWidth)
        lines.add("rewind_period_s" to configuration.rewindPeriodSeconds)
        lines.add("rewind_window_s" to configuration.rewindWindowSeconds)
        lines.add("cheats_enabled" to settingsRepository.areCheatsEnabled())
        if (cheatCount != null) {
            lines.add("active_cheats" to cheatCount)
        }

        lines.add("retroachievements_hardcore" to settingsRepository.isRetroAchievementsHardcoreEnabled())

        Log.i(TAG, "--- $reason ---")
        lines.forEach { (key, value) ->
            Log.i(TAG, "config $key=$value")
        }
    }
}
