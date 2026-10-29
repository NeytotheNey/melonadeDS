package me.magnum.melonds.domain.model

import android.net.Uri

data class EmulatorConfiguration(
        val useCustomBios: Boolean,
        val showBootScreen: Boolean,
        val dsBios7Uri: Uri?,
        val dsBios9Uri: Uri?,
        val dsFirmwareUri: Uri?,
        val dsiBios7Uri: Uri?,
        val dsiBios9Uri: Uri?,
        val dsiFirmwareUri: Uri?,
        val dsiNandUri: Uri?,
        val internalDirectory: String,
        val fastForwardSpeedMultiplier: Float,
        val framePacingVsyncEnabled: Boolean,
        val framePacingBufferEnabled: Boolean,
        val rewindEnabled: Boolean,
        val rewindPeriodSeconds: Int,
        val rewindWindowSeconds: Int,
        val useJit: Boolean,
        val syncRtcToDevice: Boolean,
        // Puts the Lua scripts' drawings into savestate and rewind pictures
        val luaDrawingsInScreenshots: Boolean,
        val consoleType: ConsoleType,
        val soundEnabled: Boolean,
        val muteFastForwardAudio: Boolean,
        val audioInterpolation: AudioInterpolation,
        val audioBitrate: AudioBitrate,
        val volume: Int,
        val audioLatency: AudioLatency,
        val micSource: MicSource,
        val firmwareConfiguration: FirmwareConfiguration,
        val rendererConfiguration: RendererConfiguration
)