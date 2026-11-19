package me.magnum.melonds.impl

import android.content.Context
import android.content.SharedPreferences
import android.content.SharedPreferences.OnSharedPreferenceChangeListener
import android.net.Uri
import android.util.Log
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.util.UUID
import kotlin.math.pow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.conflate
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.shareIn
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.decodeFromStream
import kotlinx.serialization.json.encodeToStream
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.common.uridelegates.UriHandler
import me.magnum.melonds.domain.model.AudioBitrate
import me.magnum.melonds.domain.model.AudioInterpolation
import me.magnum.melonds.domain.model.AudioLatency
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.ControllerConfiguration
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.FirmwareConfiguration
import me.magnum.melonds.domain.model.FpsCounterPosition
import me.magnum.melonds.domain.model.MacAddress
import me.magnum.melonds.domain.model.MicSource
import me.magnum.melonds.domain.model.RendererConfiguration
import me.magnum.melonds.domain.model.RomIconFiltering
import me.magnum.melonds.domain.model.RomListViewMode
import me.magnum.melonds.domain.model.SaveStateLocation
import me.magnum.melonds.domain.model.SizeUnit
import me.magnum.melonds.domain.model.SortingMode
import me.magnum.melonds.domain.model.SortingOrder
import me.magnum.melonds.domain.model.VideoFiltering
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.domain.model.camera.DSiCameraSourceType
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.input.SoftInputBehaviour
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.rewind.RewindWindowPosition
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.SettingsBackupFormat.CONTROLLER_FILE
import me.magnum.melonds.impl.dtos.input.ControllerConfigurationDto
import me.magnum.melonds.impl.emulator.SaveBackupNames
import me.magnum.melonds.impl.input.ControllerConfigurationFactory
import me.magnum.melonds.ui.Theme
import me.magnum.melonds.utils.enumValueOfIgnoreCase
import me.magnum.melonds.utils.enumValueOfIgnoreCaseOrNull
import me.magnum.melonds.utils.supportsJit

class SharedPreferencesSettingsRepository(
    private val context: Context,
    private val preferences: SharedPreferences,
    private val controllerConfigurationFactory: ControllerConfigurationFactory,
    private val json: Json,
    private val uriHandler: UriHandler,
    preferencesCoroutineScope: CoroutineScope,
) : SettingsRepository, OnSharedPreferenceChangeListener {

    companion object {
        private const val TAG = "SPSettingsRepository"
        // Same default as the theme preference in pref_appearance.xml
        private const val DEFAULT_THEME = "system"
    }

    @OptIn(ExperimentalSerializationApi::class)
    private val controllerConfiguration by lazy {
        val initialConfiguration = try {
            val configFile = File(context.filesDir, CONTROLLER_FILE)
            configFile.inputStream().use {
                val loadedConfiguration = json.decodeFromStream<ControllerConfigurationDto>(it)
                loadedConfiguration.toControllerConfiguration()
            }
        } catch (_: Exception) {
            controllerConfigurationFactory.buildDefaultControllerConfiguration()
        }

        MutableStateFlow(initialConfiguration)
    }
    private val preferenceSharedFlows = mutableMapOf<String, MutableSharedFlow<Unit>>()
    private val renderConfigurationFlow: SharedFlow<RendererConfiguration>

    init {
        preferences.registerOnSharedPreferenceChangeListener(this)
        setDefaultThemeIfRequired()
        setDefaultMacAddressIfRequired()
        moveColorDitheringIntoDeband()

        // combine() takes at most 5 typed flows, so the 4 look settings (filter, deband, colors and shadow), the 2 threading ones and the 6
        // GPU extras (hi-res capture, filtered 2D, edge marking, sharper rotated 2D, and precise depth with the floor seams fix as their own
        // pair) are grouped first. Compute's dithering follows deband (RendererConfiguration)
        val lookSettingsFlow = combine(
            getVideoFiltering(),
            isDebandEnabled(),
            isDsScreenColorsEnabled(),
            isScreenShadowEnabled(),
        ) { filtering, deband, dsScreenColors, screenShadow ->
            LookSettings(filtering, deband, dsScreenColors, screenShadow)
        }
        val threadingSettingsFlow = combine(
            isThreadedRenderingEnabled(),
            isThreaded2dRenderingEnabled(),
        ) { threadedRenderingEnabled, threaded2dRenderingEnabled ->
            Pair(threadedRenderingEnabled, threaded2dRenderingEnabled)
        }
        val accuracySettingsFlow = combine(
            isPreciseDepthEnabled(),
            isGlSeamsEnabled(),
        ) { preciseDepthEnabled, glSeamsEnabled ->
            Pair(preciseDepthEnabled, glSeamsEnabled)
        }
        val gpuExtrasFlow = combine(
            isHiresCaptureEnabled(),
            isFilter2dEnabled(),
            isEdgeMarkingEnabled(),
            isSharperRotated2dEnabled(),
            accuracySettingsFlow,
        ) { hiresCaptureEnabled, filter2dEnabled, edgeMarkingEnabled, sharperRotated2dEnabled, (preciseDepthEnabled, glSeamsEnabled) ->
            GpuExtras(hiresCaptureEnabled, filter2dEnabled, edgeMarkingEnabled, sharperRotated2dEnabled, preciseDepthEnabled, glSeamsEnabled)
        }
        renderConfigurationFlow = combine(
            getVideoRenderer(),
            lookSettingsFlow,
            threadingSettingsFlow,
            gpuExtrasFlow,
            getVideoInternalResolutionScaling(),
        ) { renderer, look, (threadedRenderingEnabled, threaded2dRenderingEnabled), gpuExtras, resolutionScaling ->
            RendererConfiguration(
                renderer, look.filtering, look.deband, look.dsScreenColors, look.screenShadow, threadedRenderingEnabled, threaded2dRenderingEnabled,
                gpuExtras.hiresCapture, gpuExtras.filter2d, resolutionScaling, gpuExtras.edgeMarking, gpuExtras.sharperRotated2d,
                gpuExtras.preciseDepth, gpuExtras.glSeams,
            )
        }.conflate().shareIn(preferencesCoroutineScope, SharingStarted.Lazily, replay = 1)
    }

    private fun setDefaultThemeIfRequired() {
        if (preferences.getString("theme", null) != null)
            return

        preferences.edit {
            putString("theme", DEFAULT_THEME)
        }
    }

    // "Smooth gradients" (compute's color dithering) was merged into deband: someone on Compute who had it on and deband off gets deband on,
    // so their dithering stays on. On the other renderers it did nothing, so deband stays as it was there. The old key is removed either way,
    // so this only does something once, also for a restored backup that still has it
    private fun moveColorDitheringIntoDeband() {
        if (!preferences.contains(PreferenceKeys.VIDEO_COLOR_DITHERING)) {
            return
        }

        val colorDithering = preferences.getBoolean(PreferenceKeys.VIDEO_COLOR_DITHERING, false)
        // As getVideoRenderer() reads it
        val isCompute = preferences.getString("video_renderer", "software") == "compute"
        preferences.edit {
            if (colorDithering && isCompute && !preferences.getBoolean(PreferenceKeys.VIDEO_DEBAND, false)) {
                putBoolean(PreferenceKeys.VIDEO_DEBAND, true)
            }
            remove(PreferenceKeys.VIDEO_COLOR_DITHERING)
        }
    }

    private fun setDefaultMacAddressIfRequired() {
        if (preferences.getString("internal_mac_address", null) != null) {
            return
        }

        val macAddress = MacAddress.randomDsAddress()
        preferences.edit {
            putString("internal_mac_address", macAddress.toString())
        }
    }

    override suspend fun getEmulatorConfiguration(): EmulatorConfiguration {
        val consoleType = getDefaultConsoleType()
        val useCustomBios = useCustomBios()
        val dsBiosDirUri = getDsBiosDirectory()
        val dsiBiosDirUri = getDsiBiosDirectory()

        // Ensure all BIOS dirs are set. DSi requires both dirs to be set
        if ((consoleType == ConsoleType.DS && useCustomBios && dsBiosDirUri == null) || (consoleType == ConsoleType.DSi && (dsBiosDirUri == null || dsiBiosDirUri == null)))
            throw IllegalStateException("BIOS directory not set")

        val dsDirDocument = dsBiosDirUri?.let {
            DocumentFile.fromTreeUri(context, it)
        }
        val dsiDirDocument = dsiBiosDirUri?.let {
            DocumentFile.fromTreeUri(context, it)
        }

        return EmulatorConfiguration(
            useCustomBios = useCustomBios(),
            showBootScreen = showBootScreen(),
            dsBios7Uri = dsDirDocument?.findFile("bios7.bin")?.uri,
            dsBios9Uri = dsDirDocument?.findFile("bios9.bin")?.uri,
            dsFirmwareUri = dsDirDocument?.findFile("firmware.bin")?.uri,
            dsiBios7Uri = dsiDirDocument?.findFile("bios7.bin")?.uri,
            dsiBios9Uri = dsiDirDocument?.findFile("bios9.bin")?.uri,
            dsiFirmwareUri = dsiDirDocument?.findFile("firmware.bin")?.uri,
            dsiNandUri = dsiDirDocument?.findFile("nand.bin")?.uri,
            internalDirectory = context.filesDir.absolutePath,
            cacheDirectory = context.cacheDir.absolutePath,
            pgoDumpDirectory = if (BuildConfig.PGO_GENERATE) (context.getExternalFilesDir(null) ?: context.filesDir).absolutePath else null,
            fastForwardSpeedMultiplier = getFastForwardSpeedMultiplier(),
            framePacingVsyncEnabled = isFramePacingVsyncEnabled(),
            framePacingBufferEnabled = isFramePacingBufferEnabled(),
            framePacingSmooth30FpsEnabled = isFramePacingSmooth30FpsEnabled(),
            rewindEnabled = isRewindEnabled(),
            rewindPeriodSeconds = getRewindPeriod(),
            rewindWindowSeconds = getRewindWindow(),
            useJit = isJitEnabled(),
            syncRtcToDevice = preferences.getBoolean(PreferenceKeys.SYNC_RTC_TO_DEVICE, true),
            luaDrawingsInScreenshots = preferences.getBoolean(PreferenceKeys.VIDEO_LUA_DRAWINGS_IN_STATE_PICTURES, false),
            // Per game, set from the ROM's config when one is launched
            wideViewWidth = 0,
            consoleType = consoleType,
            soundEnabled = isSoundEnabled(),
            muteFastForwardAudio = shouldMuteFastForwardAudio(),
            audioInterpolation = getAudioInterpolation(),
            audioBitrate = getAudioBitrate(),
            volume = getVolume(),
            audioLatency = AudioLatency.LOW,
            micSource = getMicSource(),
            firmwareConfiguration = getFirmwareConfiguration(),
            rendererConfiguration = renderConfigurationFlow.first(),
        )
    }

    override fun getTheme(): Theme {
        val themePreference = preferences.getString("theme", DEFAULT_THEME)!!
        return Theme.valueOf(themePreference.uppercase())
    }

    override fun isDynamicColorsEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.DYNAMIC_COLORS, true)
    }

    override fun getFastForwardSpeedMultiplier(): Float {
        val speedMultiplierPreference = preferences.getString("fast_forward_speed_multiplier", "-1")!!
        return speedMultiplierPreference.toFloat()
    }

    override fun isRewindEnabled(): Boolean {
        return preferences.getBoolean("enable_rewind", false)
    }

    override fun getRewindWindowPosition(): RewindWindowPosition {
        val positionPreference = preferences.getString("rewind_window_position", "bottom")!!
        return RewindWindowPosition.valueOf(positionPreference.uppercase())
    }

    override fun isSustainedPerformanceModeEnabled(): Boolean {
        return preferences.getBoolean("enable_sustained_performance", false)
    }

    override fun isFramePacingVsyncEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.FRAME_PACING_VSYNC, true)
    }

    override fun isFramePacingLock60HzEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.FRAME_PACING_LOCK_60HZ, true)
    }

    override fun isFramePacingBufferEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.FRAME_PACING_BUFFER, true)
    }

    override fun isFramePacingSmooth30FpsEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.FRAME_PACING_SMOOTH_30FPS, false)
    }

    override fun isPerformanceLoggingEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.ENABLE_PERF_LOGGING, false)
    }

    override fun getPerformanceLogDirectory(): Uri? {
        val dirPreference = preferences.getStringSet(PreferenceKeys.PERF_LOG_DIR, null)?.firstOrNull()
        return dirPreference?.toUri()
    }

    override fun isDebugLogEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.ENABLE_DEBUG_LOG, false)
    }

    override fun getDebugSwitches(): String {
        return preferences.getString(PreferenceKeys.DEBUG_SWITCHES, "") ?: ""
    }

    override fun getRomSearchDirectories(): Array<Uri> {
        val dirPreference = preferences.getStringSet("rom_search_dirs", emptySet())
        return dirPreference?.map { it.toUri() }?.toTypedArray() ?: emptyArray()
    }

    override fun clearRomSearchDirectories() {
        preferences.edit {
            putStringSet("rom_search_dirs", null)
        }
    }

    override fun getRomIconFiltering(): RomIconFiltering {
        val romIconFilteringPreference = preferences.getString("rom_icon_filtering", "none")!!
        return enumValueOfIgnoreCase(romIconFilteringPreference)
    }

    override fun getRomCacheMaxSize(): SizeUnit {
        // Default cache size step is 3, or 1GB
        val cacheSizeStepPreference = preferences.getInt("rom_cache_max_size", 3)
        // Cache size is 128MB * (cacheSizeStepPreference ^ 2)
        return SizeUnit.MB(128) * 2.toDouble().pow(cacheSizeStepPreference).toLong()
    }
    override fun getDefaultConsoleType(): ConsoleType {
        val consoleTypePreference = preferences.getString("console_type", "ds")!!
        return enumValueOfIgnoreCase(consoleTypePreference)
    }

    override fun getFirmwareConfiguration(): FirmwareConfiguration {
        val birthdayPreference = preferences.getString("firmware_settings_birthday", "01/01")!!
        val parts = birthdayPreference.split("/")
        val birthday = if (parts.size != 2) {
            Pair(1, 1)
        } else {
            val day = parts[0].toIntOrNull() ?: 1
            val month = parts[1].toIntOrNull() ?: 1
            Pair(day, month)
        }

        val useCustomBios = useCustomBios()
        var macAddress: String? = null
        val randomizeMacAddress = if (useCustomBios) {
            preferences.getBoolean("custom_randomize_mac_address", false)
        } else {
            var randomize = preferences.getBoolean("internal_randomize_mac_address", false)

            if (!randomize) {
                macAddress = preferences.getString("internal_mac_address", null)
                // If the MAC address is not defined, enable MAC address randomization
                if (macAddress == null) {
                    randomize = true
                }
            }
            randomize
        }

        return FirmwareConfiguration(
                preferences.getString("firmware_settings_nickname", "Player")!!,
                preferences.getString("firmware_settings_message", "Hello!")!!,
                preferences.getString("firmware_settings_language", "1")!!.toInt(),
                preferences.getInt("firmware_settings_colour", 0),
                birthday.second,
                birthday.first,
                randomizeMacAddress,
                macAddress
        )
    }

    override fun useCustomBios(): Boolean {
        return preferences.getBoolean("use_custom_bios", false)
    }

    override fun getDsBiosDirectory(): Uri? {
        val dirPreference = preferences.getStringSet("bios_dir", null)?.firstOrNull()
        return dirPreference?.toUri()
    }

    override fun getDsiBiosDirectory(): Uri? {
        val dirPreference = preferences.getStringSet("dsi_bios_dir", null)?.firstOrNull()
        return dirPreference?.toUri()
    }

    override fun showBootScreen(): Boolean {
        return preferences.getBoolean("show_bios", false)
    }

    override fun isJitEnabled(): Boolean {
        val defaultJitEnabled = supportsJit()
        return preferences.getBoolean("enable_jit", defaultJitEnabled)
    }

    override fun getVideoRenderer(): Flow<VideoRenderer> {
        return getOrCreatePreferenceSharedFlow("video_renderer") {
            val videoRendererPreference = preferences.getString("video_renderer", "software")!!
            VideoRenderer.valueOf(videoRendererPreference.uppercase())
        }
    }

    override fun getVideoInternalResolutionScaling(): Flow<Int> {
        return getOrCreatePreferenceSharedFlow("video_internal_resolution") {
            val internalResolutionPreference = preferences.getString("video_internal_resolution", "1")!!
            internalResolutionPreference.toIntOrNull() ?: 1
        }
    }

    override fun getVideoFiltering(): Flow<VideoFiltering> {
        return getOrCreatePreferenceSharedFlow("video_filtering") {
            val filteringPreference = preferences.getString("video_filtering", "none")!!
            VideoFiltering.valueOf(filteringPreference.uppercase())
        }
    }

    // The "DS screen colors" switch (video_ds_screen_colors) is gone from the settings, so it reads as off even where an old prefs file or a
    // backup still has it on. The stored value is left alone
    override fun isDsScreenColorsEnabled(): Flow<Boolean> {
        return flowOf(false)
    }

    override fun isDebandEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_DEBAND) {
            preferences.getBoolean(PreferenceKeys.VIDEO_DEBAND, false)
        }
    }

    override fun isScreenShadowEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_SCREEN_SHADOW) {
            preferences.getBoolean(PreferenceKeys.VIDEO_SCREEN_SHADOW, false)
        }
    }

    override fun isThreadedRenderingEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow("enable_threaded_rendering") {
            preferences.getBoolean("enable_threaded_rendering", true)
        }
    }

    override fun isThreaded2dRenderingEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_THREADED_2D) {
            preferences.getBoolean(PreferenceKeys.VIDEO_THREADED_2D, false)
        }
    }

    override fun isHiresCaptureEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_HIRES_CAPTURE) {
            preferences.getBoolean(PreferenceKeys.VIDEO_HIRES_CAPTURE, true)
        }
    }

    override fun isEdgeMarkingEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_EDGE_MARKING) {
            preferences.getBoolean(PreferenceKeys.VIDEO_EDGE_MARKING, true)
        }
    }

    override fun isFilter2dEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_FILTER_2D) {
            preferences.getBoolean(PreferenceKeys.VIDEO_FILTER_2D, false)
        }
    }

    override fun isSharperRotated2dEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_SHARPER_ROTATED_2D) {
            preferences.getBoolean(PreferenceKeys.VIDEO_SHARPER_ROTATED_2D, false)
        }
    }

    override fun isPreciseDepthEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_PRECISE_DEPTH) {
            preferences.getBoolean(PreferenceKeys.VIDEO_PRECISE_DEPTH, false)
        }
    }

    override fun isGlSeamsEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.VIDEO_GL_SEAMS) {
            preferences.getBoolean(PreferenceKeys.VIDEO_GL_SEAMS, false)
        }
    }

    override fun getFpsCounterPosition(): FpsCounterPosition {
        val fpsCounterPreference = preferences.getString("fps_counter_position", "hidden")!!
        return FpsCounterPosition.valueOf(fpsCounterPreference.uppercase())
    }

    override fun showStatusBar(): Boolean {
        return preferences.getBoolean(PreferenceKeys.SHOW_STATUS_BAR, false)
    }

    override fun getDSiCameraSource(): DSiCameraSourceType {
        val dsiCameraSource = preferences.getString("dsi_camera_source", "physical_cameras")!!
        return DSiCameraSourceType.valueOf(dsiCameraSource.uppercase())
    }

    override fun getDSiCameraStaticImage(): Uri? {
        val staticImagePreference = preferences.getStringSet("dsi_camera_static_image", null)?.firstOrNull()
        return staticImagePreference?.toUri()
    }

    override fun isSoundEnabled(): Boolean {
        return preferences.getBoolean("sound_enabled", true)
    }

    override fun shouldMuteFastForwardAudio(): Boolean {
        return preferences.getBoolean(PreferenceKeys.MUTE_FAST_FORWARD_AUDIO, true)
    }

    private fun getRewindPeriod(): Int {
        return preferences.getInt("rewind_period", 10)
    }

    private fun getRewindWindow(): Int {
        return preferences.getInt("rewind_window", 6) * 10
    }

    private fun getVolume(): Int {
        return preferences.getInt("volume", 256).coerceIn(0, 256)
    }

    private fun getAudioInterpolation(): AudioInterpolation {
        val interpolationPreference = preferences.getString("audio_interpolation", "none")!!
        return enumValueOfIgnoreCase(interpolationPreference)
    }

    private fun getAudioBitrate(): AudioBitrate {
        val bitratePreference = preferences.getString("audio_bitrate", "auto")!!
        return enumValueOfIgnoreCase(bitratePreference)
    }

    override fun getAudioLatency(): AudioLatency {
        val audioLatencyPreference = preferences.getString("audio_latency", "medium")!!
        return enumValueOfIgnoreCase(audioLatencyPreference)
    }

    override fun getMicSource(): MicSource {
        val micSourcePreference = preferences.getString("mic_source", "blow")!!
        return enumValueOfIgnoreCase(micSourcePreference)
    }

    override fun getRomSortingMode(): SortingMode {
        val sortingMode = preferences.getString("rom_sorting_mode", "alphabetically")!!
        return SortingMode.valueOf(sortingMode.uppercase())
    }

    override fun getRomListViewMode(): RomListViewMode {
        val viewModePreference = preferences.getString(PreferenceKeys.ROM_LIST_VIEW_MODE, "list")!!
        return enumValueOfIgnoreCaseOrNull<RomListViewMode>(viewModePreference) ?: RomListViewMode.LIST
    }

    override fun getRomSortingOrder(): SortingOrder {
        val sortingOrder = preferences.getString("rom_sorting_order", null)
        return if (sortingOrder == null)
            getRomSortingMode().defaultOrder
        else
            SortingOrder.valueOf(sortingOrder.uppercase())
    }

    override fun saveNextToRomFile(): Boolean {
        return preferences.getBoolean("use_rom_dir", true)
    }

    override fun getSaveFileDirectory(): Uri? {
        val dirPreference = preferences.getStringSet("sram_dir", null)?.firstOrNull()
        return dirPreference?.toUri()
    }

    override fun getSaveFileDirectory(rom: Rom): Uri {
        return if (!saveNextToRomFile() && getSaveFileDirectory() != null) {
            getSaveFileDirectory()!!
        } else {
            if (rom.parentTreeUri != null) {
                getRomParentDirectory(rom)
            } else {
                // We don't know the ROM's directory, so we can't save next to it. Put save file in an app folder
                val externalFilesDir = context.getExternalFilesDir(null)
                val saveFileDirectory = File(externalFilesDir, "saves")
                if (!saveFileDirectory.isDirectory && !saveFileDirectory.mkdirs()) {
                    throw Exception("Could not create internal save directory")
                }

                Uri.fromFile(saveFileDirectory)
            }
        }
    }

    override fun isSaveBackupOnGameStartEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.SAVE_BACKUP_ON_GAME_START, true)
    }

    override fun isSaveBackupOnStateLoadEnabled(): Boolean {
        return preferences.getBoolean(PreferenceKeys.SAVE_BACKUP_ON_STATE_LOAD, true)
    }

    override fun getSaveBackupsToKeep(): Int {
        return SaveBackupNames.parseKeepCount(preferences.getString(PreferenceKeys.SAVE_BACKUPS_TO_KEEP, "10"))
    }

    override fun getSaveStateLocation(rom: Rom): SaveStateLocation {
        val locationPreference = preferences.getString("save_state_location", "save_dir")!!
        return SaveStateLocation.valueOf(locationPreference.uppercase())
    }

    override fun getSaveStateDirectory(rom: Rom): Uri? {
        val saveStateLocation = getSaveStateLocation(rom)

        return when (saveStateLocation) {
            SaveStateLocation.SAVE_DIR -> getSaveFileDirectory(rom)
            SaveStateLocation.ROM_DIR -> getRomParentDirectory(rom)
            SaveStateLocation.INTERNAL_DIR -> {
                val saveStateDir = File(context.getExternalFilesDir(null), "savestates")
                if (!saveStateDir.isDirectory) {
                    saveStateDir.mkdirs()
                }
                DocumentFile.fromFile(saveStateDir).uri
            }
        }
    }

    private fun getRomParentDirectory(rom: Rom): Uri {
        return rom.parentTreeUri?.let {
            uriHandler.getUriTreeDocument(rom.parentTreeUri)?.uri
        } ?: throw Exception("Could not determine ROMs parent document")
    }

    override fun getControllerConfiguration(): ControllerConfiguration {
        return controllerConfiguration.value
    }

    override fun observeControllerConfiguration(): StateFlow<ControllerConfiguration> {
        return controllerConfiguration
    }

    override fun getSelectedLayoutId(): UUID {
        val id = preferences.getString("input_layout_id", null)
        return id?.let { UUID.fromString(it) } ?: LayoutConfiguration.DEFAULT_ID
    }

    override fun getSoftInputBehaviour(): Flow<SoftInputBehaviour> {
        return getOrCreatePreferenceSharedFlow("soft_input_behaviour") {
            val preference = preferences.getString("soft_input_behaviour", "hide_system_buttons_when_controller_connected")

            when (preference) {
                "always_visible" -> SoftInputBehaviour.ALWAYS_VISIBLE
                "hide_system_buttons_when_controller_connected" -> SoftInputBehaviour.HIDE_SYSTEM_BUTTONS_WHEN_CONTROLLERS_CONNECTED
                "hide_mapped_buttons_when_controller_connected" -> SoftInputBehaviour.HIDE_ALL_BUTTONS_ASSIGNED_TO_CONNECTED_CONTROLLERS
                "always_invisible" -> SoftInputBehaviour.ALWAYS_INVISIBLE
                else -> SoftInputBehaviour.HIDE_SYSTEM_BUTTONS_WHEN_CONTROLLERS_CONNECTED
            }
        }
    }

    override fun getControllerStyle(): Flow<ControllerStyle> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.INPUT_CONTROLLER_STYLE) {
            ControllerStyle.fromPreferenceValue(preferences.getString(PreferenceKeys.INPUT_CONTROLLER_STYLE, null))
        }
    }

    override fun isTouchHapticFeedbackEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow("input_touch_haptic_feedback_enabled") {
            preferences.getBoolean("input_touch_haptic_feedback_enabled", true)
        }
    }

    override fun getTouchHapticFeedbackStrength(): Int {
        val strength = preferences.getInt("input_touch_haptic_feedback_strength", 30)
        return strength.coerceIn(1, 100)
    }

    override fun getSoftInputOpacity(): Flow<Int> {
        return getOrCreatePreferenceSharedFlow("input_opacity") {
            preferences.getInt("input_opacity", 50)
        }
    }

    override fun isRetroAchievementsRichPresenceEnabled(): Boolean {
        return preferences.getBoolean("ra_rich_presence", true)
    }

    override fun isRetroAchievementsHardcoreEnabled(): Boolean {
        return preferences.getBoolean("ra_hardcore_enabled", false)
    }

    override fun areRetroAchievementsActiveChallengeIndicatorsEnabled(): Boolean {
        return preferences.getBoolean("ra_active_challenge_indicators", true)
    }

    override fun areRetroAchievementsProgressIndicatorsEnabled(): Boolean {
        return preferences.getBoolean("ra_progress_indicators", true)
    }

    override fun areRetroAchievementsLeaderboardIndicatorsEnabled(): Boolean {
        return preferences.getBoolean("ra_leaderboard_indicators", true)
    }

    override fun areCheatsEnabled(): Boolean {
        return preferences.getBoolean("cheats_enabled", false)
    }

    override fun observeRomSearchDirectories(): Flow<Array<Uri>> {
        return getOrCreatePreferenceSharedFlow("rom_search_dirs") {
            getRomSearchDirectories()
        }
    }

    override fun observeSelectedLayoutId(): Flow<UUID> {
        return getOrCreatePreferenceSharedFlow("input_layout_id") {
            getSelectedLayoutId()
        }
    }

    override fun observeDSiCameraSource(): Flow<DSiCameraSourceType> {
        return getOrCreatePreferenceSharedFlow("dsi_camera_source") {
            getDSiCameraSource()
        }
    }

    override fun observeDSiCameraStaticImage(): Flow<Uri?> {
        return getOrCreatePreferenceSharedFlow("dsi_camera_static_image") {
            getDSiCameraStaticImage()
        }
    }

    override fun setDsBiosDirectory(directoryUri: Uri) {
        preferences.edit {
            putStringSet("bios_dir", setOf(directoryUri.toString()))
        }
    }

    override fun setDsiBiosDirectory(directoryUri: Uri) {
        preferences.edit {
            putStringSet("dsi_bios_dir", setOf(directoryUri.toString()))
        }
    }

    override fun addRomSearchDirectory(directoryUri: Uri) {
        preferences.edit {
            putStringSet("rom_search_dirs", setOf(directoryUri.toString()))
        }
    }

    @OptIn(ExperimentalSerializationApi::class)
    override fun setControllerConfiguration(controllerConfiguration: ControllerConfiguration) {
        this.controllerConfiguration.value = controllerConfiguration

        try {
            val configFile = File(context.filesDir, CONTROLLER_FILE)
            val dto = ControllerConfigurationDto.fromControllerConfiguration(controllerConfiguration)
            configFile.outputStream().use {
                json.encodeToStream(dto, it)
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to save controller configuration", e)
        }
    }

    override fun setRomSortingMode(sortingMode: SortingMode) {
        preferences.edit {
            putString("rom_sorting_mode", sortingMode.toString().lowercase())
        }
    }

    override fun setRomSortingOrder(sortingOrder: SortingOrder) {
        preferences.edit {
            putString("rom_sorting_order", sortingOrder.toString().lowercase())
        }
    }

    override fun setRomListViewMode(viewMode: RomListViewMode) {
        preferences.edit {
            putString(PreferenceKeys.ROM_LIST_VIEW_MODE, viewMode.toString().lowercase())
        }
    }

    override fun setSelectedLayoutId(layoutId: UUID) {
        preferences.edit {
            putString("input_layout_id", layoutId.toString())
        }
    }

    override fun observeTheme(): Flow<Theme> {
        return getOrCreatePreferenceSharedFlow("theme") {
            getTheme()
        }
    }

    override fun observeDynamicColorsEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.DYNAMIC_COLORS) {
            isDynamicColorsEnabled()
        }
    }

    override fun observeDebugLogEnabled(): Flow<Boolean> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.ENABLE_DEBUG_LOG) {
            isDebugLogEnabled()
        }
    }

    override fun observeRomIconFiltering(): Flow<RomIconFiltering> {
        return getOrCreatePreferenceSharedFlow("rom_icon_filtering") {
            getRomIconFiltering()
        }
    }

    override fun observeRomListViewMode(): Flow<RomListViewMode> {
        return getOrCreatePreferenceSharedFlow(PreferenceKeys.ROM_LIST_VIEW_MODE) {
            getRomListViewMode()
        }
    }

    private fun <T> getOrCreatePreferenceSharedFlow(preference: String, mapper: () -> T): Flow<T> {
        val preferenceFlow = preferenceSharedFlows.getOrPut(preference) {
            MutableSharedFlow<Unit>(replay = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST).apply {
                // Immediately trigger an event to load the initial value
                tryEmit(Unit)
            }
        }

        return preferenceFlow.map { mapper() }
    }

    override fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String?) {
        preferenceSharedFlows[key]?.tryEmit(Unit)
    }

    override fun observeRenderConfiguration(): Flow<RendererConfiguration> {
        return renderConfigurationFlow
    }
}

// The look settings of a RendererConfiguration, grouped for combine()
private data class LookSettings(
    val filtering: VideoFiltering,
    val deband: Boolean,
    val dsScreenColors: Boolean,
    val screenShadow: Boolean,
)

// The 3D and 2D extras of a RendererConfiguration, grouped for combine(): all OpenGL and compute only, but precise depth, which every renderer
// has, and the floor seams fix, which is OpenGL only
private data class GpuExtras(
    val hiresCapture: Boolean,
    val filter2d: Boolean,
    val edgeMarking: Boolean,
    val sharperRotated2d: Boolean,
    val preciseDepth: Boolean,
    val glSeams: Boolean,
)
