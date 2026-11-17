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
        // Private storage Android may clear, for the compute renderer's program cache
        val cacheDirectory: String,
        // Where -PnativePgo=generate builds write their .profraw files (tools/pgo/README.md): the external files dir, which adb reads without run-as,
        // so non-debuggable release builds can be collected too. Null in every other build
        val pgoDumpDirectory: String? = null,
        val fastForwardSpeedMultiplier: Float,
        val framePacingVsyncEnabled: Boolean,
        val framePacingBufferEnabled: Boolean,
        val framePacingSmooth30FpsEnabled: Boolean,
        val rewindEnabled: Boolean,
        val rewindPeriodSeconds: Int,
        val rewindWindowSeconds: Int,
        val useJit: Boolean,
        val syncRtcToDevice: Boolean,
        // Puts the Lua scripts' drawings into savestate and rewind pictures
        val luaDrawingsInScreenshots: Boolean,
        // Top-screen 3D view width at launch in DS pixels, or 0; changed in play via EmulatorManager.setWideView, not by config updates
        val wideViewWidth: Int,
        // The WideMelon camera's 3D rows above and below the top screen at launch (WidescreenLaunch.getRenderMargin), or 0; changed with the width
        val wideViewMargin: Int = 0,
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