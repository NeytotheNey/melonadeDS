package me.magnum.melonds.domain.repositories

import android.net.Uri
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import me.magnum.melonds.domain.model.*
import me.magnum.melonds.domain.model.camera.DSiCameraSourceType
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.input.SoftInputBehaviour
import me.magnum.melonds.domain.model.rewind.RewindWindowPosition
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.ui.Theme
import java.util.UUID

interface SettingsRepository {
    suspend fun getEmulatorConfiguration(): EmulatorConfiguration

    fun getTheme(): Theme
    fun isDynamicColorsEnabled(): Boolean
    fun getFastForwardSpeedMultiplier(): Float
    fun isRewindEnabled(): Boolean
    fun getRewindWindowPosition(): RewindWindowPosition
    fun isSustainedPerformanceModeEnabled(): Boolean
    fun isFramePacingVsyncEnabled(): Boolean
    fun isFramePacingLock60HzEnabled(): Boolean
    fun isFramePacingBufferEnabled(): Boolean
    fun isPerformanceLoggingEnabled(): Boolean
    fun getPerformanceLogDirectory(): Uri?
    fun isDebugLogEnabled(): Boolean

    fun getRomSearchDirectories(): Array<Uri>
    fun clearRomSearchDirectories()
    fun getRomIconFiltering(): RomIconFiltering
    fun getRomCacheMaxSize(): SizeUnit

    fun getDefaultConsoleType(): ConsoleType
    fun getFirmwareConfiguration(): FirmwareConfiguration
    fun useCustomBios(): Boolean
    fun getDsBiosDirectory(): Uri?
    fun getDsiBiosDirectory(): Uri?
    fun showBootScreen(): Boolean
    fun isJitEnabled(): Boolean

    fun getVideoRenderer(): Flow<VideoRenderer>
    fun getVideoInternalResolutionScaling(): Flow<Int>
    fun getVideoFiltering(): Flow<VideoFiltering>
    fun isDsScreenColorsEnabled(): Flow<Boolean>
    fun isScreenShadowEnabled(): Flow<Boolean>
    fun isThreadedRenderingEnabled(): Flow<Boolean>
    fun getFpsCounterPosition(): FpsCounterPosition
    fun showStatusBar(): Boolean
    fun getDSiCameraSource(): DSiCameraSourceType
    fun getDSiCameraStaticImage(): Uri?

    fun isSoundEnabled(): Boolean
    fun shouldMuteFastForwardAudio(): Boolean
    fun getAudioLatency(): AudioLatency
    fun getMicSource(): MicSource

    fun getRomSortingMode(): SortingMode
    fun getRomSortingOrder(): SortingOrder
    fun saveNextToRomFile(): Boolean
    fun getSaveFileDirectory(): Uri?
    fun getSaveFileDirectory(rom: Rom): Uri
    fun getSaveStateLocation(rom: Rom): SaveStateLocation
    fun getSaveStateDirectory(rom: Rom): Uri?

    fun getControllerConfiguration(): ControllerConfiguration
    fun observeControllerConfiguration(): StateFlow<ControllerConfiguration>
    fun getSelectedLayoutId(): UUID
    fun getSoftInputBehaviour(): Flow<SoftInputBehaviour>
    fun getControllerStyle(): Flow<ControllerStyle>
    fun isTouchHapticFeedbackEnabled(): Flow<Boolean>
    fun getTouchHapticFeedbackStrength(): Int
    fun getSoftInputOpacity(): Flow<Int>

    fun isRetroAchievementsRichPresenceEnabled(): Boolean
    fun isRetroAchievementsHardcoreEnabled(): Boolean
    fun areRetroAchievementsActiveChallengeIndicatorsEnabled(): Boolean
    fun areRetroAchievementsProgressIndicatorsEnabled(): Boolean
    fun areRetroAchievementsLeaderboardIndicatorsEnabled(): Boolean

    fun areCheatsEnabled(): Boolean

    fun observeTheme(): Flow<Theme>
    fun observeDynamicColorsEnabled(): Flow<Boolean>
    fun observeDebugLogEnabled(): Flow<Boolean>
    fun observeRomIconFiltering(): Flow<RomIconFiltering>
    fun observeRomSearchDirectories(): Flow<Array<Uri>>
    fun observeSelectedLayoutId(): Flow<UUID>
    fun observeDSiCameraSource(): Flow<DSiCameraSourceType>
    fun observeDSiCameraStaticImage(): Flow<Uri?>

    fun setDsBiosDirectory(directoryUri: Uri)
    fun setDsiBiosDirectory(directoryUri: Uri)
    fun addRomSearchDirectory(directoryUri: Uri)
    fun setControllerConfiguration(controllerConfiguration: ControllerConfiguration)
    fun setRomSortingMode(sortingMode: SortingMode)
    fun setRomSortingOrder(sortingOrder: SortingOrder)
    fun setSelectedLayoutId(layoutId: UUID)

    fun observeRenderConfiguration(): Flow<RendererConfiguration>
}
