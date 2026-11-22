package me.magnum.melonds.ui.emulator

import android.net.Uri
import android.util.Log
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.shareIn
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.common.romprocessors.RomFileProcessorFactory
import me.magnum.melonds.common.runtime.ScreenshotFrameBufferProvider
import me.magnum.melonds.domain.model.Cheat
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.RomInfo
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.SaveStateSlot
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.domain.model.emulator.EmulatorEvent
import me.magnum.melonds.domain.model.emulator.EmulatorSessionUpdateAction
import me.magnum.melonds.domain.model.emulator.FirmwareLaunchResult
import me.magnum.melonds.domain.model.emulator.LuaLayoutChoice
import me.magnum.melonds.domain.model.emulator.RomLaunchResult
import me.magnum.melonds.domain.model.emulator.toLuaLayoutChoices
import me.magnum.melonds.domain.model.layout.BackgroundMode
import me.magnum.melonds.domain.model.layout.Insets
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.LayoutDisplayPair
import me.magnum.melonds.domain.model.layout.ScreenFold
import me.magnum.melonds.domain.model.camera.WideCameraRequest
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
import me.magnum.melonds.domain.model.render.Shade
import me.magnum.melonds.domain.model.render.Sun
import me.magnum.melonds.domain.model.render.TiltShift
import me.magnum.melonds.ui.emulator.camera.WideCameraSettings
import me.magnum.melonds.ui.emulator.camera.WideCameraState
import me.magnum.melonds.domain.model.retroachievements.GameAchievementData
import me.magnum.melonds.domain.model.retroachievements.RAEvent
import me.magnum.melonds.domain.model.retroachievements.RASimpleAchievement
import me.magnum.melonds.domain.model.retroachievements.RASimpleLeaderboard
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.WidescreenLaunch
import me.magnum.melonds.domain.model.ui.Orientation
import me.magnum.melonds.domain.repositories.BackgroundRepository
import me.magnum.melonds.domain.repositories.CheatsRepository
import me.magnum.melonds.domain.repositories.LayoutsRepository
import me.magnum.melonds.domain.repositories.RetroAchievementsRepository
import me.magnum.melonds.domain.repositories.RomsRepository
import me.magnum.melonds.domain.repositories.SaveStatesRepository
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.domain.services.EmulatorManager
import me.magnum.melonds.impl.LuaScriptFolderSync
import me.magnum.melonds.impl.LuaScriptStorage
import me.magnum.melonds.impl.diagnostics.DebugLog
import me.magnum.melonds.impl.diagnostics.PerformanceLogger
import me.magnum.melonds.impl.emulator.EmulatorSession
import me.magnum.melonds.impl.layout.UILayoutProvider
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncScheduler
import me.magnum.melonds.ui.emulator.component.RetroAchievementsSubmissionHandler
import me.magnum.melonds.ui.emulator.firmware.FirmwarePauseMenuOption
import me.magnum.melonds.ui.emulator.model.EmulatorScreenSettings
import me.magnum.melonds.ui.emulator.model.EmulatorState
import me.magnum.melonds.ui.emulator.model.EmulatorUiEvent
import me.magnum.melonds.ui.emulator.model.LaunchArgs
import me.magnum.melonds.ui.emulator.model.LuaUiEvent
import me.magnum.melonds.ui.emulator.model.LuaUiSetting
import me.magnum.melonds.ui.emulator.model.PauseMenu
import me.magnum.melonds.ui.emulator.model.RAEventUi
import me.magnum.melonds.ui.emulator.model.RAIntegrationEvent
import me.magnum.melonds.ui.emulator.model.RumbleEvent
import me.magnum.melonds.ui.emulator.model.RuntimeInputLayoutConfiguration
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration
import me.magnum.melonds.ui.emulator.model.ToastEvent
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rom.RomPauseMenuOption
import me.magnum.melonds.utils.EventSharedFlow
import me.magnum.rcheevosapi.exception.UserTokenExpiredException
import me.magnum.rcheevosapi.model.RAUserAuth
import java.util.UUID
import javax.inject.Inject
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.EmptyCoroutineContext
import kotlin.math.roundToInt
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds

@OptIn(ExperimentalCoroutinesApi::class)
@HiltViewModel
class EmulatorViewModel @Inject constructor(
    private val settingsRepository: SettingsRepository,
    private val romsRepository: RomsRepository,
    private val cheatsRepository: CheatsRepository,
    private val retroAchievementsRepository: RetroAchievementsRepository,
    private val romFileProcessorFactory: RomFileProcessorFactory,
    private val layoutsRepository: LayoutsRepository,
    private val backgroundsRepository: BackgroundRepository,
    private val saveStatesRepository: SaveStatesRepository,
    private val screenshotFrameBufferProvider: ScreenshotFrameBufferProvider,
    private val uiLayoutProvider: UILayoutProvider,
    private val emulatorManager: EmulatorManager,
    private val emulatorSession: EmulatorSession,
    private val retroAchievementsSubmissionHandler: RetroAchievementsSubmissionHandler,
    private val performanceLogger: PerformanceLogger,
    private val luaScriptStorage: LuaScriptStorage,
    private val romMetadataSyncScheduler: RomMetadataSyncScheduler,
    savedStateHandle: SavedStateHandle,
) : ViewModel() {

    private val sessionCoroutineScope = EmulatorSessionCoroutineScope()
    private var raSessionJob: Job? = null

    private val _emulatorState = MutableStateFlow<EmulatorState>(EmulatorState.Uninitialized)
    val emulatorState = _emulatorState.asStateFlow()

    private val _layout = MutableStateFlow<LayoutConfiguration?>(null)

    private val _currentLayout = uiLayoutProvider.currentLayout.shareIn(viewModelScope, SharingStarted.Lazily)

    private val _runtimeLayout = MutableStateFlow<RuntimeInputLayoutConfiguration?>(null)
    val runtimeLayout = _runtimeLayout.asStateFlow()

    val controllerConfiguration = settingsRepository.observeControllerConfiguration()

    private val _runtimeRendererConfiguration = MutableStateFlow<RuntimeRendererConfiguration?>(null)
    val runtimeRendererConfiguration = _runtimeRendererConfiguration.asStateFlow()

    private val _mainScreenBackground = MutableStateFlow(RuntimeBackground.None)
    val mainScreenBackground = _mainScreenBackground.asStateFlow()

    private val _secondaryScreenBackground = MutableStateFlow(RuntimeBackground.None)
    val secondaryScreenBackground = _secondaryScreenBackground.asStateFlow()

    private val _rumbleEvent = MutableSharedFlow<RumbleEvent>(extraBufferCapacity = 100, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val rumbleEvent = _rumbleEvent.asSharedFlow()

    private val _achievementsEvent = MutableSharedFlow<RAEventUi>(extraBufferCapacity = 100, onBufferOverflow = BufferOverflow.SUSPEND)
    val achievementsEvent = _achievementsEvent.asSharedFlow()

    private val _currentFps = MutableStateFlow<Int?>(null)
    val currentFps = _currentFps.asStateFlow()

    private val _toastEvent = EventSharedFlow<ToastEvent>()
    val toastEvent = _toastEvent.asSharedFlow()

    private val _raIntegrationEvent = Channel<RAIntegrationEvent>(Channel.UNLIMITED)
    val integrationEvent = _raIntegrationEvent.receiveAsFlow()

    val pendingSubmissionsSummary = retroAchievementsSubmissionHandler.getPendingSubmissionsSummaryFlow()

    private val _uiEvent = EventSharedFlow<EmulatorUiEvent>()
    val uiEvent = _uiEvent.asSharedFlow()

    // Unbounded, since a script's on_load can send several requests at once; events are tagged with their session so stale ones are dropped
    private val _luaUiEvent = Channel<Pair<Int, LuaUiEvent>>(Channel.UNLIMITED)
    // SessionEnded is never dropped, since it undoes what the ended session's scripts did
    val luaUiEvent = _luaUiEvent.receiveAsFlow()
        .filter { (session, event) -> session == luaUiEventSession || event is LuaUiEvent.SessionEnded }
        .map { it.second }
    // Changed on the main thread whenever a session starts or stops, so nothing matches it while no session runs
    private var luaUiEventSession = 0

    /**
     * Script UI overrides, kept in the VM to survive activity recreation; only the screen uses it, as it handles [luaUiEvent] in order.
     */
    val luaUiOverrides = LuaUiOverrides()

    /**
     * Current swap state as shown, restored after recreation.
     */
    var areScreensSwapped = false

    // Layout a script asked for, or null for the player's; main thread only
    private val luaLayoutRequests = LuaLayoutRequests()
    private val _luaLayoutId = MutableStateFlow<UUID?>(null)
    // The ids of the player's layouts, or null until they're read; main thread only
    private var luaExistingLayoutIds: Set<UUID>? = null

    // Blur scripts asked for with melon.screen.tilt_shift(), or null for none; main thread only
    private val luaTiltShiftRequests = LuaTiltShiftRequests()
    private val _luaTiltShift = MutableStateFlow<TiltShift?>(null)
    val luaTiltShift = _luaTiltShift.asStateFlow()

    // Bloom and vignette scripts asked for with melon.screen.bloom() and vignette(), or null for neither; main thread only
    private val luaBloomRequests = LuaBloomRequests()
    private val _luaBloom = MutableStateFlow<Bloom?>(null)
    val luaBloom = _luaBloom.asStateFlow()

    // The depth of field scripts asked for with melon.screen.depth_of_field(), or null for none; main thread only
    private val luaDepthOfFieldRequests = LuaDepthOfFieldRequests()
    private val _luaDepthOfField = MutableStateFlow<DepthOfField?>(null)
    val luaDepthOfField = _luaDepthOfField.asStateFlow()

    // The shading scripts asked for with melon.screen.shade(), or null for none; main thread only
    private val luaShadeRequests = LuaShadeRequests()
    private val _luaShade = MutableStateFlow<Shade?>(null)
    val luaShade = _luaShade.asStateFlow()

    // The sun shadows scripts asked for with melon.screen.sun(), or null for none; main thread only
    private val luaSunRequests = LuaSunRequests()
    private val _luaSun = MutableStateFlow<Sun?>(null)
    val luaSun = _luaSun.asStateFlow()

    // What scripts ask of the WideMelon camera with melon.wide; main thread only
    private val luaWideCameraRequests = LuaWideCameraRequests()
    private val _luaWideCamera = MutableStateFlow(WideCameraRequest.NONE)
    val luaWideCamera = _luaWideCamera.asStateFlow()

    // The WideMelon camera's state: this session's number, its settings for the shown layout (null while it's off), and the zoom it starts at
    // (the game's remembered Camera Zoom step, updated with each pick); main thread only
    private val _wideCameraState = MutableStateFlow(WideCameraState(session = 0, settings = null, startZoom = 1f))
    val wideCameraState = _wideCameraState.asStateFlow()
    // The ROM the camera's picks are remembered for, the one this camera session launched; main thread only
    private var wideCameraRom: Rom? = null
    // The zoom the core renders the camera's window for (WideCameraController's onRenderZoomNeeded): the remembered start zoom at launch, then
    // whatever the camera asks for. The render width follows it (WidescreenLaunch.getRenderWidth)
    private val wideCameraRenderZoom = MutableStateFlow(1f)

    init {
        // Here and in onCleared() rather than in the activity, since this outlives a recreated activity and every way into a game goes through here
        romMetadataSyncScheduler.onGameStarted()

        viewModelScope.launch {
            _layout.filterNotNull().collect {
                uiLayoutProvider.setCurrentLayoutConfiguration(it)
            }
        }

        val launchArgs = LaunchArgs.fromSavedStateHandle(savedStateHandle)
        if (launchArgs != null) {
            launchEmulator(launchArgs)
        } else {
            _uiEvent.tryEmit(EmulatorUiEvent.CloseEmulator)
        }
    }

    fun relaunchWithNewArgs(args: LaunchArgs) {
        if (_emulatorState.value.isRunning()) {
            stopEmulator()
        }
        launchEmulator(args)
    }

    fun onRomLaunchValidated(rom: Rom) {
        viewModelScope.launch {
            launchRom(rom)
        }
    }

    fun onFirmwareLaunchValidated(consoleType: ConsoleType) {
        viewModelScope.launch {
            launchFirmware(consoleType)
        }
    }

    private fun launchEmulator(args: LaunchArgs) {
        when (args) {
            is LaunchArgs.RomObject -> loadRom(args.rom)
            is LaunchArgs.RomUri -> loadRom(args.uri)
            is LaunchArgs.RomPath -> loadRom(args.path)
            is LaunchArgs.Firmware -> _emulatorState.value = EmulatorState.ValidatingFirmware(args.consoleType)
        }
    }

    private fun loadRom(rom: Rom) {
        viewModelScope.launch {
            resetEmulatorState(EmulatorState.LoadingRom)
            sessionCoroutineScope.launch {
                _emulatorState.value = EmulatorState.ValidatingRom(rom)
            }
        }
    }

    private fun loadRom(romUri: Uri) {
        viewModelScope.launch {
            resetEmulatorState(EmulatorState.LoadingRom)
            sessionCoroutineScope.launch {
                val rom = romsRepository.getRomAtUri(romUri)
                if (rom != null) {
                    _emulatorState.value = EmulatorState.ValidatingRom(rom)
                } else {
                    _emulatorState.value = EmulatorState.RomNotFoundError(romUri.toString())
                }
            }
        }
    }

    private fun loadRom(romPath: String) {
        viewModelScope.launch {
            resetEmulatorState(EmulatorState.LoadingRom)
            sessionCoroutineScope.launch {
                val rom = romsRepository.getRomAtPath(romPath)
                if (rom != null) {
                    _emulatorState.value = EmulatorState.ValidatingRom(rom)
                } else {
                    _emulatorState.value = EmulatorState.RomNotFoundError(romPath)
                }
            }
        }
    }

    private suspend fun launchRom(rom: Rom) = coroutineScope {
        startEmulatorSession(EmulatorSession.SessionType.RomSession(rom))
        startObservingMainScreenBackground()
        startObservingSecondaryScreenBackground()
        startObservingRuntimeInputLayoutConfiguration()
        startObservingRendererConfiguration()
        startObservingEmulatorEvents()
        startObservingAchievementEvents()
        val widescreenLaunch = getWidescreenLaunch(rom)
        // The game starts as wide as its remembered zoom needs, so one that starts zoomed out isn't resized right after boot
        val launchRenderZoom = wideCameraRenderZoom.value
        startObservingLayoutForRom(rom, widescreenLaunch.wideLayoutId)
        startObservingWideViewWidth(widescreenLaunch, launchRenderZoom)
        startRetroAchievementsSession(rom)

        val romInfo = getRomInfo(rom)
        val cheats = romInfo?.let { getRomEnabledCheats(it) } ?: emptyList()
        val syncedLuaScripts = getSyncedLuaScripts(rom)
        val luaScripts = if (emulatorSession.areLuaScriptsAllowed()) syncedLuaScripts.filter { it.enabled } else emptyList()
        // Before the scripts run, so their requests come after it
        val screensSwapped = (getLibraryRom(rom) ?: rom).config.screensSwapped
        sendLuaUiEvent(LuaUiEvent.SessionStarted(screensSwapped))
        val result = emulatorManager.loadRom(
            rom,
            cheats,
            luaScripts,
            rom.config.mods.luaScriptFolder,
            emulatorSession.areSaveStateLoadsAllowed(),
            // With the camera zoomed out the core renders wider (and taller) than the layout shows
            widescreenLaunch.getRenderWidth(widescreenLaunch.viewWidth, launchRenderZoom),
            widescreenLaunch.getRenderMargin(widescreenLaunch.viewWidth, launchRenderZoom),
        )
        when (result) {
            is RomLaunchResult.LaunchFailedRomNotFound,
            is RomLaunchResult.LaunchFailedRomNotSupported,
            is RomLaunchResult.LaunchFailedSramProblem,
            is RomLaunchResult.LaunchFailed -> {
                _emulatorState.value = EmulatorState.RomLoadError
            }
            is RomLaunchResult.LaunchFailedRomFolderUnreadable -> {
                _emulatorState.value = EmulatorState.RomFolderUnreadableError
            }
            is RomLaunchResult.LaunchSuccessful -> {
                if (!result.isGbaLoadSuccessful) {
                    _toastEvent.tryEmit(ToastEvent.GbaLoadFailed)
                }
                _emulatorState.value = EmulatorState.RunningRom(rom)
                startTrackingFps()
                updatePerformanceLoggingSession(_emulatorState.value)
            }
        }
    }

    private fun launchFirmware(consoleType: ConsoleType) {
        viewModelScope.launch {
            resetEmulatorState(EmulatorState.LoadingFirmware)
            startEmulatorSession(EmulatorSession.SessionType.FirmwareSession(consoleType))
            // The firmware has no saved swap, so it starts unswapped instead of keeping the last game's
            sendLuaUiEvent(LuaUiEvent.SessionStarted(screensSwapped = false))
            sessionCoroutineScope.launch {
                startObservingMainScreenBackground()
                startObservingSecondaryScreenBackground()
                startObservingRuntimeInputLayoutConfiguration()
                startObservingRendererConfiguration()
                startObservingLayoutForFirmware()
                startObservingEmulatorEvents()

                val result = emulatorManager.loadFirmware(consoleType)
                when (result) {
                    is FirmwareLaunchResult.LaunchFailed -> {
                        _emulatorState.value = EmulatorState.FirmwareLoadError(result.reason)
                    }
                    FirmwareLaunchResult.LaunchSuccessful -> {
                        _emulatorState.value = EmulatorState.RunningFirmware(consoleType)
                        startTrackingFps()
                        updatePerformanceLoggingSession(_emulatorState.value)
                    }
                }
            }
        }
    }

    fun setSystemOrientation(orientation: Orientation) {
        uiLayoutProvider.updateCurrentOrientation(orientation)
    }

    fun setUiSize(width: Int, height: Int) {
        uiLayoutProvider.updateUiSize(width, height)
    }

    fun setUiInsets(insets: Insets) {
        uiLayoutProvider.updateUiInsets(insets)
    }

    fun setScreenFolds(folds: List<ScreenFold>) {
        uiLayoutProvider.updateFolds(folds)
    }

    fun setConnectedDisplays(displays: LayoutDisplayPair) {
        uiLayoutProvider.updateDisplays(displays)
    }

    fun onSettingsChanged() {
        val currentState = _emulatorState.value
        updatePerformanceLoggingSession(currentState)
        sessionCoroutineScope.launch {
            val sessionUpdateActions = emulatorSession.updateRetroAchievementsSettings(
                retroAchievementsRepository.isUserAuthenticated(),
                settingsRepository.isRetroAchievementsHardcoreEnabled(),
            )

            when (currentState) {
                is EmulatorState.RunningRom -> emulatorManager.updateRomEmulatorConfiguration(currentState.rom)
                is EmulatorState.RunningFirmware -> emulatorManager.updateFirmwareEmulatorConfiguration(currentState.console)
                else -> {
                    // Do nothing
                }
            }

            dispatchSessionUpdateActions(sessionUpdateActions)
        }
    }

    /**
     * Starts or stops performance logging for the running ROM or firmware when the setting is toggled while the emulator is running.
     */
    private fun updatePerformanceLoggingSession(currentState: EmulatorState) {
        val sessionName = getPerformanceLogSessionName(currentState) ?: return
        if (settingsRepository.isPerformanceLoggingEnabled()) {
            if (!performanceLogger.isSessionActive()) {
                performanceLogger.startSession(sessionName)
            }
        } else {
            performanceLogger.stopSession()
        }
    }

    private fun getPerformanceLogSessionName(state: EmulatorState): String? {
        return when (state) {
            is EmulatorState.RunningRom -> state.rom.name
            is EmulatorState.RunningFirmware -> "firmware_${state.console.name}"
            else -> null
        }
    }

    fun onCheatsChanged() {
        val rom = (_emulatorState.value as? EmulatorState.RunningRom)?.rom ?: return

        sessionCoroutineScope.launch {
            getRomInfo(rom)?.let {
                val cheats = getRomEnabledCheats(it)
                emulatorManager.updateCheats(cheats)
            }
        }
    }

    fun pauseEmulator(showPauseMenu: Boolean) {
        sessionCoroutineScope.launch {
            emulatorManager.pauseEmulator()
            if (showPauseMenu) {
                val pauseOptions = when (_emulatorState.value) {
                    is EmulatorState.RunningRom -> {
                        RomPauseMenuOption.entries.filter {
                            filterRomPauseMenuOption(it)
                        }
                    }
                    is EmulatorState.RunningFirmware -> {
                        FirmwarePauseMenuOption.entries
                    }
                    else -> null
                }

                if (pauseOptions != null) {
                    _uiEvent.emit(EmulatorUiEvent.ShowPauseMenu(PauseMenu(pauseOptions)))
                }
            }
        }
    }

    fun resumeEmulator() {
        sessionCoroutineScope.launch {
            emulatorManager.resumeEmulator()
        }
    }

    fun resetEmulator() {
        if (_emulatorState.value.isRunning()) {
            sessionCoroutineScope.launch {
                emulatorManager.resetEmulator()
                _achievementsEvent.emit(RAEventUi.Reset)
            }
        }
    }

    fun exitEmulator(force: Boolean = false) {
        if (!force && retroAchievementsSubmissionHandler.hasPendingSubmissions()) {
            _uiEvent.tryEmit(EmulatorUiEvent.ShowPendingSubmissionsDialog)
            retroAchievementsSubmissionHandler.retrySubmissionsImmediately()
        } else {
            stopEmulatorAndExit()
        }
    }

    private fun stopEmulator() {
        endSession()
        viewModelScope.launch {
            _achievementsEvent.emit(RAEventUi.Reset)
        }
        screenshotFrameBufferProvider.clearBuffer()
    }

    private fun stopEmulatorAndExit() {
        endSession()
        _uiEvent.tryEmit(EmulatorUiEvent.CloseEmulator)
    }

    /**
     * Ends the session's Lua UI events and performance log, then stops its jobs and the native instance
     */
    private fun endSession() {
        endSessionTracking()
        // Stop session jobs (RA heartbeat, FPS) so they don't outlive the native instance
        sessionCoroutineScope.cancel()
        emulatorManager.stopEmulator()
    }

    private fun endSessionTracking() {
        endLuaUiSession()
        performanceLogger.stopSession()
    }

    fun onPauseMenuOptionSelected(option: PauseMenuOption) {
        when (option) {
            is RomPauseMenuOption -> {
                when (option) {
                    RomPauseMenuOption.SETTINGS -> _uiEvent.tryEmit(EmulatorUiEvent.OpenScreen.SettingsScreen)
                    RomPauseMenuOption.SAVE_STATE -> {
                        (_emulatorState.value as? EmulatorState.RunningRom)?.let {
                            val saveStateSlots = getRomSaveStateSlots(it.rom)
                            _uiEvent.tryEmit(EmulatorUiEvent.ShowRomSaveStates(saveStateSlots, EmulatorUiEvent.ShowRomSaveStates.Reason.SAVING))
                        }
                    }
                    RomPauseMenuOption.LOAD_STATE -> {
                        (_emulatorState.value as? EmulatorState.RunningRom)?.let {
                            val saveStateSlots = getRomSaveStateSlots(it.rom)
                            _uiEvent.tryEmit(EmulatorUiEvent.ShowRomSaveStates(saveStateSlots, EmulatorUiEvent.ShowRomSaveStates.Reason.LOADING))
                        }
                    }
                    RomPauseMenuOption.REWIND -> {
                        sessionCoroutineScope.launch {
                            val rewindWindow = emulatorManager.getRewindWindow()
                            _uiEvent.emit(EmulatorUiEvent.ShowRewindWindow(rewindWindow, settingsRepository.getRewindWindowPosition()))
                        }
                    }
                    RomPauseMenuOption.CHEATS -> {
                        (_emulatorState.value as? EmulatorState.RunningRom)?.let {
                            sessionCoroutineScope.launch {
                                getRomInfo(it.rom)?.let { romInfo ->
                                    _uiEvent.tryEmit(EmulatorUiEvent.OpenScreen.CheatsScreen(romInfo))
                                }
                            }
                        }
                    }
                    RomPauseMenuOption.MODS -> _uiEvent.tryEmit(EmulatorUiEvent.ShowLuaScripts(emulatorManager.getLuaScripts()))
                    RomPauseMenuOption.VIEW_ACHIEVEMENTS -> _uiEvent.tryEmit(EmulatorUiEvent.ShowAchievementList)
                    RomPauseMenuOption.SWAP_SCREENS -> swapScreensFromPauseMenu()
                    RomPauseMenuOption.RESET -> resetEmulator()
                    RomPauseMenuOption.EXIT -> exitEmulator(force = false)
                }
            }
            is FirmwarePauseMenuOption -> {
                when (option) {
                    FirmwarePauseMenuOption.SETTINGS -> _uiEvent.tryEmit(EmulatorUiEvent.OpenScreen.SettingsScreen)
                    FirmwarePauseMenuOption.SWAP_SCREENS -> swapScreensFromPauseMenu()
                    FirmwarePauseMenuOption.RESET -> resetEmulator()
                    FirmwarePauseMenuOption.EXIT -> {
                        stopEmulatorAndExit()
                    }
                }
            }
        }
    }

    /**
     * The player swapped the screens by hand, so the game (not the firmware) starts that way next time.
     */
    fun onPlayerSwappedScreens(screensSwapped: Boolean) {
        val rom = (_emulatorState.value as? EmulatorState.RunningRom)?.rom ?: return
        viewModelScope.launch {
            // The config may have changed since launch, so the newest one is updated
            val currentRom = getLibraryRom(rom) ?: return@launch
            if (currentRom.config.screensSwapped != screensSwapped) {
                romsRepository.updateRomConfig(currentRom, currentRom.config.copy(screensSwapped = screensSwapped))
            }
        }
    }

    private fun swapScreensFromPauseMenu() {
        _uiEvent.tryEmit(EmulatorUiEvent.SwapScreens)
        // Like a reset, it's done straight from the menu, so the game goes on
        resumeEmulator()
    }

    fun onOpenRewind() {
        if (!settingsRepository.isRewindEnabled()) {
            _toastEvent.tryEmit(ToastEvent.RewindNotEnabled)
            return
        }

        if (!emulatorSession.areSaveStateLoadsAllowed()) {
            _toastEvent.tryEmit(ToastEvent.RewindNotAvailableWhileRAHardcoreModeEnabled)
            return
        }

        sessionCoroutineScope.launch {
            emulatorManager.pauseEmulator()
            val rewindWindow = emulatorManager.getRewindWindow()
            _uiEvent.emit(EmulatorUiEvent.ShowRewindWindow(rewindWindow, settingsRepository.getRewindWindowPosition()))
        }
    }

    fun rewindToState(rewindSaveState: RewindSaveState) {
        sessionCoroutineScope.launch {
            emulatorManager.loadRewindState(rewindSaveState)
        }
    }

    // The slot picker closes with dismiss(), not cancel, so these resume the game on every outcome or it stays paused with no menu
    fun saveStateToSlot(slot: SaveStateSlot) {
        sessionCoroutineScope.launch {
            (_emulatorState.value as? EmulatorState.RunningRom)?.let {
                if (!saveRomState(it.rom, slot)) {
                    _toastEvent.emit(ToastEvent.StateSaveFailed)
                }
            }
            emulatorManager.resumeEmulator()
        }
    }

    fun loadStateFromSlot(slot: SaveStateSlot) {
        sessionCoroutineScope.launch {
            if (!slot.exists) {
                _toastEvent.emit(ToastEvent.StateStateDoesNotExist)
            } else {
                (_emulatorState.value as? EmulatorState.RunningRom)?.let {
                    if (!loadRomState(it.rom, slot)) {
                        _toastEvent.emit(ToastEvent.StateLoadFailed)
                    }
                }
            }
            emulatorManager.resumeEmulator()
        }
    }

    fun doQuickSave() {
        val currentState = _emulatorState.value
        when (currentState) {
            is EmulatorState.RunningRom -> {
                sessionCoroutineScope.launch {
                    emulatorManager.pauseEmulator()
                    val quickSlot = saveStatesRepository.getRomQuickSaveStateSlot(currentState.rom)
                    if (saveRomState(currentState.rom, quickSlot)) {
                        _toastEvent.emit(ToastEvent.QuickSaveSuccessful)
                    } else {
                        _toastEvent.emit(ToastEvent.StateSaveFailed)
                    }
                    emulatorManager.resumeEmulator()
                }
            }
            is EmulatorState.RunningFirmware -> {
                _toastEvent.tryEmit(ToastEvent.CannotSaveStateWhenRunningFirmware)
            }
            else -> {
                // Do nothing
            }
        }
    }

    fun doQuickLoad() {
        val currentState = _emulatorState.value
        when (currentState) {
            is EmulatorState.RunningRom -> {
                if (emulatorSession.areSaveStateLoadsAllowed()) {
                    sessionCoroutineScope.launch {
                        emulatorManager.pauseEmulator()
                        val quickSlot = saveStatesRepository.getRomQuickSaveStateSlot(currentState.rom)
                        if (loadRomState(currentState.rom, quickSlot)) {
                            _toastEvent.emit(ToastEvent.QuickLoadSuccessful)
                        } else if (quickSlot.exists) {
                            _toastEvent.emit(ToastEvent.StateLoadFailed)
                        }
                        emulatorManager.resumeEmulator()
                    }
                } else {
                    _toastEvent.tryEmit(ToastEvent.CannotUseSaveStatesWhenRAHardcoreIsEnabled)
                }
            }
            is EmulatorState.RunningFirmware -> {
                _toastEvent.tryEmit(ToastEvent.CannotLoadStateWhenRunningFirmware)
            }
            else -> {
                // Do nothing
            }
        }
    }

    fun deleteSaveStateSlot(slot: SaveStateSlot): List<SaveStateSlot>? {
        return (_emulatorState.value as? EmulatorState.RunningRom)?.let {
            saveStatesRepository.deleteRomSaveState(it.rom, slot)
            getRomSaveStateSlots(it.rom)
        }
    }

    private suspend fun saveRomState(rom: Rom, slot: SaveStateSlot): Boolean {
        val slotUri = getRomSaveStateUriOrNull(rom, slot) ?: return false
        if (!emulatorManager.saveState(slotUri)) {
            return false
        }

        // Capture and store the screenshot asynchronously
        sessionCoroutineScope.launch {
            // Delete old screenshot immediately
            saveStatesRepository.deleteRomSaveStateScreenshot(rom, slot)
            val screenshotCaptured = emulatorManager.takeScreenshot()

            if (screenshotCaptured) {
                val screenshot = screenshotFrameBufferProvider.getScreenshot()
                saveStatesRepository.setRomSaveStateScreenshot(rom, slot, screenshot)
            }
        }

        return true
    }

    private suspend fun loadRomState(rom: Rom, slot: SaveStateSlot): Boolean {
        if (!slot.exists) {
            return false
        }

        val slotUri = getRomSaveStateUriOrNull(rom, slot) ?: return false
        val success = emulatorManager.loadState(slotUri)
        if (success) {
            _achievementsEvent.emit(RAEventUi.Reset)
        }

        return success
    }

    /**
     * The slot's file, or null when its folder can't be reached or the file can't be made (the repository throws for those), so a failed
     * save or load shows its toast and resumes the game instead of crashing the session coroutine.
     */
    private fun getRomSaveStateUriOrNull(rom: Rom, slot: SaveStateSlot): Uri? {
        return try {
            saveStatesRepository.getRomSaveStateUri(rom, slot)
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not get the file of save state slot ${slot.slot}", e)
            null
        }
    }

    private fun startObservingRuntimeInputLayoutConfiguration() {
        sessionCoroutineScope.launch {
            combine(
                _layout,
                _currentLayout,
                combine(settingsRepository.getSoftInputBehaviour(), settingsRepository.getControllerStyle(), ::Pair),
                settingsRepository.isTouchHapticFeedbackEnabled(),
                settingsRepository.getSoftInputOpacity(),
            ) { layoutConfiguration, variant, (softInputBehaviour, controllerStyle), isHapticFeedbackEnabled, inputOpacity ->
                val layout = variant?.second
                if (layoutConfiguration == null || layout == null) {
                    null
                } else {
                    val opacity = if (layoutConfiguration.useCustomOpacity) {
                        layoutConfiguration.opacity
                    } else {
                        inputOpacity
                    }

                    RuntimeInputLayoutConfiguration(
                        softInputBehaviour = softInputBehaviour,
                        controllerStyle = controllerStyle,
                        softInputOpacity = opacity,
                        isHapticFeedbackEnabled = isHapticFeedbackEnabled,
                        layoutOrientation = layoutConfiguration.orientation,
                        layout = layout,
                        isWideLayout = layoutConfiguration.isWide,
                    )
                }
            }.collect(_runtimeLayout)
        }
    }

    private fun resetEmulatorState(newState: EmulatorState) {
        endSessionTracking()
        sessionCoroutineScope.notifyNewSessionStarted()
        emulatorSession.reset()
        raSessionJob = null
        _currentFps.value = null
        _emulatorState.value = newState
        _mainScreenBackground.value = RuntimeBackground.None
        _secondaryScreenBackground.value = RuntimeBackground.None
        _layout.value = null
        // A new session: the camera starts over, and picks still on their way from the last one are dropped
        _wideCameraState.value = WideCameraState(session = _wideCameraState.value.session + 1, settings = null, startZoom = 1f)
        wideCameraRom = null
        wideCameraRenderZoom.value = 1f
    }

    private fun startObservingEmulatorEvents() {
        sessionCoroutineScope.launch {
            emulatorManager.emulatorEvents.collect {
                when (it) {
                    is EmulatorEvent.RumbleStart -> _rumbleEvent.tryEmit(RumbleEvent.RumbleStart(it.duration))
                    EmulatorEvent.RumbleStop -> _rumbleEvent.tryEmit(RumbleEvent.RumbleStop)
                    is EmulatorEvent.LuaScriptStopped -> sendLuaUiEvent(LuaUiEvent.ScriptStopped(it.scriptId, it.scriptName, it.error))
                    is EmulatorEvent.LuaSwapScreens -> sendLuaUiEvent(LuaUiEvent.Request(LuaUiSetting.SWAP_SCREENS, it.scriptId, it.swapped))
                    is EmulatorEvent.LuaShowControls -> sendLuaUiEvent(LuaUiEvent.Request(LuaUiSetting.SHOW_CONTROLS, it.scriptId, it.visible))
                    is EmulatorEvent.LuaFastForward -> sendLuaUiEvent(LuaUiEvent.Request(LuaUiSetting.FAST_FORWARD, it.scriptId, it.enabled))
                    is EmulatorEvent.LuaSetLayout -> onLuaLayoutRequest(it.scriptId, it.optionKey)
                    is EmulatorEvent.LuaTiltShift -> _luaTiltShift.value = luaTiltShiftRequests.onScriptRequest(it.scriptId, it.tiltShift)
                    is EmulatorEvent.LuaBloom -> _luaBloom.value = luaBloomRequests.onScriptRequest(it.scriptId, it.bloom)
                    is EmulatorEvent.LuaDepthOfField -> _luaDepthOfField.value = luaDepthOfFieldRequests.onScriptRequest(it.scriptId, it.depthOfField)
                    is EmulatorEvent.LuaShade -> _luaShade.value = luaShadeRequests.onScriptRequest(it.scriptId, it.shade)
                    is EmulatorEvent.LuaSun -> _luaSun.value = luaSunRequests.onScriptRequest(it.scriptId, it.sun)
                    is EmulatorEvent.LuaWideCamera -> _luaWideCamera.value = luaWideCameraRequests.onScriptRequest(it.scriptId, it.request)
                    // Like after the player's load, the achievements' primed and progress indicators start over
                    EmulatorEvent.LuaStateLoaded -> _achievementsEvent.tryEmit(RAEventUi.Reset)
                    is EmulatorEvent.Stop -> {
                        when (it.reason) {
                            EmulatorEvent.Stop.Reason.GBAModeNotSupported -> _toastEvent.tryEmit(ToastEvent.GbaModeNotSupported)
                            EmulatorEvent.Stop.Reason.BadExceptionRegion -> _toastEvent.tryEmit(ToastEvent.InternalError)
                            EmulatorEvent.Stop.Reason.PowerOff -> { /* no-op */ }
                        }
                        stopEmulatorAndExit()
                    }
                }
            }
        }
    }

    private fun startObservingAchievementEvents() {
        sessionCoroutineScope.launch {
            emulatorManager.observeRetroAchievementEvents().collect {
                when (it) {
                    is RAEvent.OnAchievementPrimed -> onAchievementPrimed(it.achievementId)
                    is RAEvent.OnAchievementUnPrimed -> onAchievementUnPrimed(it.achievementId)
                    is RAEvent.OnAchievementTriggered -> onAchievementTriggered(it.achievementId)
                    is RAEvent.OnAchievementProgressUpdated -> onAchievementProgressUpdated(it)
                    is RAEvent.OnLeaderboardAttemptStarted -> onLeaderboardAttemptStarted(it)
                    is RAEvent.OnLeaderboardAttemptUpdated -> onLeaderboardAttemptUpdated(it)
                    is RAEvent.OnLeaderboardAttemptCompleted -> onLeaderboardAttemptCompleted(it)
                    is RAEvent.OnLeaderboardAttemptCancelled -> onLeaderboardAttemptCancelled(it)
                }
            }
        }
    }

    private fun startObservingMainScreenBackground() {
        sessionCoroutineScope.launch {
            combine(_currentLayout, ensureEmulatorIsRunning()) { variant, _ ->
                val layout = variant?.second
                if (layout == null) {
                    RuntimeBackground.None
                } else {
                    loadBackground(layout.mainScreenLayout.backgroundId, layout.mainScreenLayout.backgroundMode)
                }
            }.collect(_mainScreenBackground)
        }
    }

    private fun startObservingSecondaryScreenBackground() {
        sessionCoroutineScope.launch {
            combine(_currentLayout, ensureEmulatorIsRunning()) { variant, _ ->
                val layout = variant?.second
                if (layout == null) {
                    RuntimeBackground.None
                } else {
                    loadBackground(layout.secondaryScreenLayout.backgroundId, layout.secondaryScreenLayout.backgroundMode)
                }
            }.collect(_secondaryScreenBackground)
        }
    }

    /**
     * @param wideLayoutId The wide layout used in place of the ROM's own while widescreen is on.
     */
    private fun startObservingLayoutForRom(rom: Rom, wideLayoutId: UUID?) {
        val romLayoutId = rom.config.layoutId
        val normalLayoutFlow = if (romLayoutId == null) {
            getGlobalLayoutFlow()
        } else {
            // Load and observe ROM layout but switch to global layout if the ROM layout stops existing
            layoutsRepository.observeLayout(romLayoutId)
                .onCompletion {
                    emitAll(getGlobalLayoutFlow())
                }
        }
        val playerLayoutFlow = wideLayoutId?.let {
            layoutsRepository.observeLayout(it)
                .onCompletion { cause ->
                    if (cause == null) {
                        emitAll(normalLayoutFlow)
                    }
                }
        } ?: normalLayoutFlow

        // The player picking another layout by hand wins over the scripts, like a screen swap does
        val romLayoutFlow = if (wideLayoutId == null) {
            romLayoutId?.let { layoutsRepository.observeLayout(it) }
        } else {
            layoutsRepository.observeLayout(wideLayoutId).onCompletion { cause ->
                if (cause == null && romLayoutId != null) {
                    emitAll(layoutsRepository.observeLayout(romLayoutId))
                }
            }
        }
        sessionCoroutineScope.launch {
            getPlayerLayoutPicks(romLayoutFlow, settingsRepository.observeSelectedLayoutId()).collect {
                _luaLayoutId.value = luaLayoutRequests.onPlayerChanged()
            }
        }

        // The layouts that exist, so a script asking for a deleted one changes nothing
        luaExistingLayoutIds = null
        sessionCoroutineScope.launch {
            layoutsRepository.getLayouts().collect { layouts ->
                luaExistingLayoutIds = layouts.mapNotNull { it.id }.toSet()
            }
        }

        // A script's layout while one holds it, or the player's if it doesn't exist
        val layoutFlow = _luaLayoutId.flatMapLatest { scriptLayoutId ->
            if (scriptLayoutId == null) {
                playerLayoutFlow
            } else {
                layoutsRepository.observeLayout(scriptLayoutId)
                    .onCompletion { cause ->
                        if (cause == null) {
                            emitAll(playerLayoutFlow)
                        }
                    }
            }
        }

        sessionCoroutineScope.launch {
            combine(layoutFlow, ensureEmulatorIsRunning()) { layout, _ ->
                layout
            }.collect(_layout)
        }
    }

    private fun startObservingRendererConfiguration() {
        sessionCoroutineScope.launch {
            settingsRepository.observeRenderConfiguration().collectLatest {
                _runtimeRendererConfiguration.value = RuntimeRendererConfiguration(
                    it.videoFiltering,
                    it.deband,
                    it.dsScreenColors,
                    it.screenShadow,
                    it.resolutionScaling,
                )
            }
        }
    }

    private fun startObservingLayoutForFirmware() {
        _layout.value = null

        sessionCoroutineScope.launch {
            combine(getGlobalLayoutFlow(), ensureEmulatorIsRunning()) { layout, _ ->
                layout
            }.collect(_layout)
        }
    }

    private suspend fun loadBackground(backgroundId: UUID?, mode: BackgroundMode): RuntimeBackground {
        return if (backgroundId == null) {
            RuntimeBackground(null, mode)
        } else {
            val background = backgroundsRepository.getBackground(backgroundId)
            RuntimeBackground(background, mode)
        }
    }

    /**
     * Sends the game the width the shown layout asks for whenever it changes (wider, with the camera's margin, as the camera zooms out), in wide
     * sessions only, and keeps the camera's settings in step with the shown width. [launchRenderZoom] is the render zoom the game was launched
     * with, so the first layout doesn't resize it again.
     */
    private fun startObservingWideViewWidth(widescreenLaunch: WidescreenLaunch, launchRenderZoom: Float) {
        if (!widescreenLaunch.isWideSession) {
            return
        }

        sessionCoroutineScope.launch {
            var currentWidth = widescreenLaunch.getRenderWidth(widescreenLaunch.viewWidth, launchRenderZoom)
            var currentMargin = widescreenLaunch.getRenderMargin(widescreenLaunch.viewWidth, launchRenderZoom)
            _wideCameraState.update { it.copy(settings = getWideCameraSettings(widescreenLaunch, widescreenLaunch.viewWidth)) }
            // The provider can still hold the previous session's layout, so only one matching the session's counts
            val shownLayouts = combine(_layout, uiLayoutProvider.currentLayoutWithConfiguration) { sessionLayout, providerLayout ->
                providerLayout?.takeIf { sessionLayout != null && it.first == sessionLayout }
            }
            combine(shownLayouts, wideCameraRenderZoom) { shownLayout, renderZoom -> shownLayout?.let { it to renderZoom } }.collect {
                val (shownLayout, renderZoom) = it ?: return@collect
                val (layoutConfiguration, layout) = shownLayout
                val showWidth = widescreenLaunch.getViewWidth(layoutConfiguration, layout)
                val settings = getWideCameraSettings(widescreenLaunch, showWidth)
                if (settings == null) {
                    // The camera comes back on at its start zoom, so the render zoom waits there instead of resizing to the old one first
                    wideCameraRenderZoom.value = _wideCameraState.value.startZoom
                }
                val width = widescreenLaunch.getRenderWidth(showWidth, renderZoom)
                val margin = widescreenLaunch.getRenderMargin(showWidth, renderZoom)
                if (width != currentWidth || margin != currentMargin) {
                    currentWidth = width
                    currentMargin = margin
                    emulatorManager.setWideView(width, margin)
                }
                _wideCameraState.update { it.copy(settings = settings) }
            }
        }
    }

    /**
     * The camera's settings for a layout shown [showWidth] wide, or null while it's off, the shown variant isn't wide, or the max zoom-out is no
     * wider than the shown width (no room to zoom out or pan, so the camera would do nothing). The room comes from the camera's widest view, not
     * the width the core renders at now, which is the shown width at 1x
     */
    private fun getWideCameraSettings(widescreenLaunch: WidescreenLaunch, showWidth: Int): WideCameraSettings? {
        val maxWidth = widescreenLaunch.getMaxRenderWidth(showWidth)
        val hasRoom = showWidth > 0 && maxWidth > showWidth
        return if (widescreenLaunch.isCameraOn && hasRoom) {
            WideCameraSettings(showWidth, widescreenLaunch.cameraReturnsToCenter, widescreenLaunch.cameraZoomsInOnText, maxWidth)
        } else {
            null
        }
    }

    /**
     * The camera in camera session [session] needs the core to render for [zoom] (a hotkey step, see WideCameraController): the render width
     * follows it. One from an earlier session (it was on its way during a ROM switch) is dropped
     */
    fun onWideCameraRenderZoomNeeded(session: Int, zoom: Float) {
        if (session != _wideCameraState.value.session) {
            return
        }
        wideCameraRenderZoom.value = zoom
    }

    /**
     * The player picked [zoom] with the Camera Zoom hotkey in camera session [session]: the camera starts there the next time it comes on, in
     * this session or the next. A pick from an earlier session (it was on its way during a ROM switch) is dropped
     */
    fun onWideCameraZoomPicked(session: Int, zoom: Float) {
        if (session != _wideCameraState.value.session) {
            return
        }
        _wideCameraState.update { it.copy(startZoom = zoom) }
        val rom = wideCameraRom ?: return
        viewModelScope.launch {
            // The config may have changed since launch, so the newest one is updated
            val currentRom = getLibraryRom(rom) ?: return@launch
            val mods = currentRom.config.mods
            if (mods.wideCameraZoom != zoom) {
                romsRepository.updateRomConfig(currentRom, currentRom.config.copy(mods = mods.copy(wideCameraZoom = zoom)))
            }
        }
    }

    /**
     * Whether the game can be wide, its wide layout and the width it starts at, decided once per launch.
     */
    private suspend fun getWidescreenLaunch(rom: Rom): WidescreenLaunch {
        val mods = rom.config.mods
        // The software renderer only does the normal width
        val isWideRenderer = settingsRepository.getVideoRenderer().first() != VideoRenderer.SOFTWARE
        val wideLayout = mods.wideLayoutId?.let { layoutsRepository.getLayout(it) }
        // The camera's remembered zoom from the newest config, since the last session may have picked one after this rom was read
        val startZoom = (getLibraryRom(rom) ?: rom).config.mods.validWideCameraZoom
        _wideCameraState.update { it.copy(startZoom = startZoom) }
        wideCameraRenderZoom.value = startZoom
        wideCameraRom = rom
        return WidescreenLaunch.resolve(mods, wideLayout, isWideRenderer)
    }

    private fun getGlobalLayoutFlow(): Flow<LayoutConfiguration> {
        return settingsRepository.observeSelectedLayoutId()
            .flatMapLatest {
                layoutsRepository.observeLayout(it)
                    .onCompletion {
                        emitAll(layoutsRepository.observeLayout(LayoutConfiguration.DEFAULT_ID))
                    }
            }
    }

    /**
     * The ROM's script list brought in step with its linked folder (see [LuaScriptFolderSync]), saved to the ROM config if it changed.
     */
    private suspend fun getSyncedLuaScripts(rom: Rom): List<RomLuaScript> {
        // The ROM given may be an older copy whose config would put back stale values, so the newest one is synced and saved
        val libraryRom = getLibraryRom(rom)
        val config = (libraryRom ?: rom).config
        val mods = config.mods
        val folderScriptNames = withContext(Dispatchers.IO) {
            mods.luaScriptFolder?.let { luaScriptStorage.listScripts(it) }?.map { it.name }
        }
        val syncedScripts = LuaScriptFolderSync.sync(mods.luaScripts, mods.luaScriptFolder != null, folderScriptNames)
        if (libraryRom != null && syncedScripts != mods.luaScripts) {
            romsRepository.updateRomConfig(libraryRom, config.copy(mods = mods.copy(luaScripts = syncedScripts)))
        }
        return syncedScripts
    }

    /**
     * The library's own copy of [rom], which has the newest config, or null for a ROM opened from outside the library.
     */
    private suspend fun getLibraryRom(rom: Rom): Rom? {
        return romsRepository.getRoms().first().find { it.hasSameFileAsRom(rom) }
    }

    private suspend fun getRomInfo(rom: Rom): RomInfo? = withContext(Dispatchers.IO) {
        val fileRomProcessor = romFileProcessorFactory.getFileRomProcessorForDocument(rom.uri)
        fileRomProcessor?.getRomInfo(rom)
    }

    private fun getRomSaveStateSlots(rom: Rom): List<SaveStateSlot> {
        return saveStatesRepository.getRomSaveStates(rom)
    }

    /**
     * Whether a Lua script is running, cheap enough to call for every motion event.
     */
    fun areLuaScriptsRunning(): Boolean {
        return emulatorManager.areLuaScriptsRunning()
    }

    /**
     * Switches a script off for this session only, from the pause menu.
     */
    fun disableLuaScript(scriptId: Int) {
        emulatorManager.disableLuaScript(scriptId)
        // Native doesn't send a stop event for this one, so the screen is told here to undo what the script changed
        val scriptName = emulatorManager.getLuaScript(scriptId)?.name ?: return
        sendLuaUiEvent(LuaUiEvent.ScriptStopped(scriptId, scriptName, error = null))
    }

    /**
     * Switches a stopped script back on for this session only, from the pause menu.
     *
     * @return false if it already runs.
     */
    fun enableLuaScript(scriptId: Int): Boolean {
        if (!emulatorManager.enableLuaScript(scriptId)) {
            return false
        }
        // Sent before anything the script asks for in on_load(), which only runs once the emulator resumes
        sendLuaUiEvent(LuaUiEvent.ScriptStarted(scriptId))
        return true
    }

    /**
     * Saves new option values from the pause menu to the ROM config and restarts (or starts) the script with them.
     *
     * @return false if the script can't run.
     */
    fun setLuaScriptOptions(scriptId: Int, optionValues: Map<String, String>): Boolean {
        val script = emulatorManager.getLuaScript(scriptId) ?: return false
        val wasRunning = script.isRunning
        if (!emulatorManager.restartLuaScript(scriptId, optionValues)) {
            return false
        }
        // Native sends no stop event for a restart, so what the script changed is undone here, as if it was switched off and on again
        if (wasRunning) {
            sendLuaUiEvent(LuaUiEvent.ScriptStopped(scriptId, script.name, error = null))
        }
        sendLuaUiEvent(LuaUiEvent.ScriptStarted(scriptId))

        val rom = (_emulatorState.value as? EmulatorState.RunningRom)?.rom ?: return true
        val savedValues = emulatorManager.getLuaScript(scriptId)?.optionValues ?: optionValues
        viewModelScope.launch {
            // The config may have changed since launch (the script list is synced then), so the newest one is updated
            val currentRom = getLibraryRom(rom) ?: return@launch
            val mods = currentRom.config.mods
            val luaScripts = mods.luaScripts.map {
                if (it.fileName == script.name) it.copy(options = it.options + savedValues) else it
            }
            if (luaScripts != mods.luaScripts) {
                romsRepository.updateRomConfig(currentRom, currentRom.config.copy(mods = mods.copy(luaScripts = luaScripts)))
            }
        }
        return true
    }

    /**
     * Ends the current session's Lua UI events, dropping anything still queued from it.
     */
    private fun endLuaUiSession() {
        luaUiEventSession++
        sendLuaUiEvent(LuaUiEvent.SessionEnded)
    }

    /**
     * Sends a Lua UI event to the screen, tagged with the current session, and keeps the scripts' layout and blur requests up to date with it
     */
    private fun sendLuaUiEvent(event: LuaUiEvent) {
        when (event) {
            is LuaUiEvent.ScriptStopped -> {
                _luaLayoutId.value = luaLayoutRequests.onScriptStopped(event.scriptId)
                _luaTiltShift.value = luaTiltShiftRequests.onScriptStopped(event.scriptId)
                _luaBloom.value = luaBloomRequests.onScriptStopped(event.scriptId)
                _luaDepthOfField.value = luaDepthOfFieldRequests.onScriptStopped(event.scriptId)
                _luaShade.value = luaShadeRequests.onScriptStopped(event.scriptId)
                _luaSun.value = luaSunRequests.onScriptStopped(event.scriptId)
                _luaWideCamera.value = luaWideCameraRequests.onScriptStopped(event.scriptId)
            }
            is LuaUiEvent.ScriptStarted -> {
                luaLayoutRequests.onScriptStarted(event.scriptId)
                luaTiltShiftRequests.onScriptStarted(event.scriptId)
                luaBloomRequests.onScriptStarted(event.scriptId)
                luaDepthOfFieldRequests.onScriptStarted(event.scriptId)
                luaShadeRequests.onScriptStarted(event.scriptId)
                luaSunRequests.onScriptStarted(event.scriptId)
                luaWideCameraRequests.onScriptStarted(event.scriptId)
            }
            LuaUiEvent.SessionEnded -> {
                _luaLayoutId.value = luaLayoutRequests.onSessionEnded()
                _luaTiltShift.value = luaTiltShiftRequests.onSessionEnded()
                _luaBloom.value = luaBloomRequests.onSessionEnded()
                _luaDepthOfField.value = luaDepthOfFieldRequests.onSessionEnded()
                _luaShade.value = luaShadeRequests.onSessionEnded()
                _luaSun.value = luaSunRequests.onSessionEnded()
                _luaWideCamera.value = luaWideCameraRequests.onSessionEnded()
            }
            is LuaUiEvent.SessionStarted,
            is LuaUiEvent.Request -> { }
        }
        _luaUiEvent.trySend(luaUiEventSession to event)
    }

    /**
     * A script asked for the layout its option [optionKey] names, or for the player's layout back (null).
     */
    private fun onLuaLayoutRequest(scriptId: Int, optionKey: String?) {
        val script = emulatorManager.getLuaScript(scriptId) ?: return
        // Until the layouts are read, every layout is taken to exist; a missing one is caught when it's shown
        val existingLayoutIds = luaExistingLayoutIds
        val request = getLuaLayoutRequest(script.options, script.optionValues, optionKey) { existingLayoutIds?.contains(it) ?: true }
        _luaLayoutId.value = luaLayoutRequests.onScriptRequest(scriptId, request)
    }

    /**
     * The player's layouts, for the layout options in the pause menu's script options dialog
     */
    fun getLuaLayoutChoices(): Flow<List<LuaLayoutChoice>> {
        return layoutsRepository.getLayouts().map { it.toLuaLayoutChoices() }
    }

    fun getScreenSettings(): EmulatorScreenSettings {
        return EmulatorScreenSettings(
            isSustainedPerformanceModeEnabled = settingsRepository.isSustainedPerformanceModeEnabled(),
            isFramePacingEnabled = settingsRepository.isFramePacingVsyncEnabled(),
            isDisplayLock60HzEnabled = settingsRepository.isFramePacingLock60HzEnabled(),
            fpsCounterPosition = settingsRepository.getFpsCounterPosition(),
        )
    }

    fun showStatusBar(): Boolean {
        return settingsRepository.showStatusBar()
    }

    private suspend fun getRomEnabledCheats(romInfo: RomInfo): List<Cheat> {
        if (!settingsRepository.areCheatsEnabled() || !emulatorSession.areCheatsEnabled()) {
            return emptyList()
        }

        return cheatsRepository.getRomEnabledCheats(romInfo)
    }

    private suspend fun getRomAchievementData(rom: Rom): GameAchievementData {
        val userAuth = retroAchievementsRepository.getUserAuthentication()
        when (userAuth) {
            is RAUserAuth.Authenticated -> { /* no-op */ }
            is RAUserAuth.AuthenticationExpired -> return GameAchievementData.withDisabledRetroAchievementsIntegration(GameAchievementData.IntegrationStatus.DISABLED_LOGIN_EXPIRED)
            null -> return GameAchievementData.withDisabledRetroAchievementsIntegration(GameAchievementData.IntegrationStatus.DISABLED_NOT_LOGGED_IN)
        }

        return retroAchievementsRepository.getUserGameData(rom.retroAchievementsHash, emulatorSession.isRetroAchievementsHardcoreModeEnabled).fold(
            onSuccess = { userGameData ->
                val gameSummary = retroAchievementsRepository.getGameSummary(rom.retroAchievementsHash)

                if (userGameData != null) {
                    val achievements = userGameData.sets.flatMap { it.achievements }
                    val leaderboards = userGameData.sets.flatMap { it.leaderboards }
                    val hasLeaderboards = leaderboards.isNotEmpty() && emulatorSession.areLeaderboardsEnabled()

                    if (achievements.isEmpty() && !hasLeaderboards) {
                        GameAchievementData.withLimitedRetroAchievementsIntegration(
                            richPresencePatch = gameSummary?.richPresencePatch,
                            icon = gameSummary?.icon,
                        )
                    } else {
                        val lockedAchievements = achievements.filter { !it.isUnlocked }.map { RASimpleAchievement(it.achievement.id, it.achievement.memoryAddress) }
                        val leaderboards = if (hasLeaderboards) {
                            leaderboards.map { RASimpleLeaderboard(it.id, it.mem, it.format) }
                        } else {
                            emptyList()
                        }

                        GameAchievementData.withFullRetroAchievementsIntegration(
                            lockedAchievements = lockedAchievements,
                            leaderboards = leaderboards,
                            totalAchievementCount = achievements.size,
                            richPresencePatch = gameSummary?.richPresencePatch,
                            icon = gameSummary?.icon,
                        )
                    }
                } else {
                    GameAchievementData.withDisabledRetroAchievementsIntegration(
                        status = GameAchievementData.IntegrationStatus.DISABLED_GAME_NOT_FOUND,
                        icon = gameSummary?.icon,
                    )
                }
            },
            onFailure = {
                // Maybe we have the game summary cached. Could allow the icon to be displayed, which looks better
                val gameSummary = retroAchievementsRepository.getGameSummary(rom.retroAchievementsHash)
                GameAchievementData.withDisabledRetroAchievementsIntegration(GameAchievementData.IntegrationStatus.DISABLED_LOAD_ERROR, gameSummary?.icon)
            }
        )
    }

    private fun onAchievementTriggered(achievementId: Long) {
        sessionCoroutineScope.launch {
            retroAchievementsRepository.getAchievement(achievementId).onSuccess { achievement ->
                if (achievement != null) {
                    val isHardcoreModeEnabled = emulatorSession.isRetroAchievementsHardcoreModeEnabled
                    retroAchievementsSubmissionHandler.addPendingAchievementSubmission(achievement, isHardcoreModeEnabled)
                }
            }
        }
    }

    private fun onAchievementPrimed(achievementId: Long) {
        if (settingsRepository.areRetroAchievementsActiveChallengeIndicatorsEnabled()) {
            sessionCoroutineScope.launch {
                retroAchievementsRepository.getAchievement(achievementId).onSuccess { achievement ->
                    if (achievement != null) {
                        _achievementsEvent.emit(RAEventUi.AchievementPrimed(achievement))
                    }
                }
            }
        }
    }

    private fun onAchievementUnPrimed(achievementId: Long) {
        sessionCoroutineScope.launch {
            retroAchievementsRepository.getAchievement(achievementId).onSuccess { achievement ->
                if (achievement != null) {
                    _achievementsEvent.emit(RAEventUi.AchievementUnPrimed(achievement))
                }
            }
        }
    }

    private fun onAchievementProgressUpdated(progressEvent: RAEvent.OnAchievementProgressUpdated) {
        if (settingsRepository.areRetroAchievementsProgressIndicatorsEnabled()) {
            sessionCoroutineScope.launch {
                retroAchievementsRepository.getAchievement(progressEvent.achievementId).onSuccess { achievement ->
                    if (achievement != null) {
                        _achievementsEvent.emit(RAEventUi.AchievementProgressUpdated(achievement, progressEvent.current, progressEvent.target, progressEvent.progress))
                    }
                }
            }
        }
    }

    private fun onLeaderboardAttemptStarted(startEvent: RAEvent.OnLeaderboardAttemptStarted) {
        sessionCoroutineScope.launch {
            if (settingsRepository.areRetroAchievementsLeaderboardIndicatorsEnabled()) {
                val leaderboard = retroAchievementsRepository.getLeaderboard(startEvent.leaderboardId)
                if (leaderboard != null) {
                    val setSummary = retroAchievementsRepository.getAchievementSetSummary(leaderboard.setId)
                    if (setSummary != null) {
                        _achievementsEvent.emit(RAEventUi.LeaderboardAttemptStarted(leaderboard, setSummary.iconUrl))
                    }
                }
            }
        }
    }

    private fun onLeaderboardAttemptUpdated(updateEvent: RAEvent.OnLeaderboardAttemptUpdated) {
        sessionCoroutineScope.launch {
            if (settingsRepository.areRetroAchievementsLeaderboardIndicatorsEnabled()) {
                _achievementsEvent.emit(RAEventUi.LeaderboardAttemptUpdated(updateEvent.leaderboardId, updateEvent.formattedValue))
            }
        }
    }

    private fun onLeaderboardAttemptCompleted(completionEvent: RAEvent.OnLeaderboardAttemptCompleted) {
        sessionCoroutineScope.launch {
            retroAchievementsRepository.getLeaderboard(completionEvent.leaderboardId)?.let { leaderboard ->
                retroAchievementsSubmissionHandler.addPendingLeaderboardSubmission(leaderboard, completionEvent.value, completionEvent.formattedValue)
            }
        }
    }

    private fun onLeaderboardAttemptCancelled(cancelEvent: RAEvent.OnLeaderboardAttemptCancelled) {
        sessionCoroutineScope.launch {
            _achievementsEvent.emit(RAEventUi.LeaderboardAttemptCancelled(cancelEvent.leaderboardId))
        }
    }

    private fun startRetroAchievementsSession(rom: Rom) {
        sessionCoroutineScope.launch {
            val achievementData = getRomAchievementData(rom)
            emulatorSession.updateRetroAchievementsIntegrationStatus(achievementData.retroAchievementsIntegrationStatus)
            if (!achievementData.isRetroAchievementsIntegrationEnabled) {
                if (achievementData.retroAchievementsIntegrationStatus == GameAchievementData.IntegrationStatus.DISABLED_LOAD_ERROR) {
                    _raIntegrationEvent.trySend(RAIntegrationEvent.Failed(achievementData.icon))
                } else if (achievementData.retroAchievementsIntegrationStatus == GameAchievementData.IntegrationStatus.DISABLED_LOGIN_EXPIRED) {
                    _raIntegrationEvent.trySend(RAIntegrationEvent.LoginExpired(achievementData.icon))
                }

                return@launch
            }

            raSessionJob = launch {
                // Wait until the emulator has actually started
                ensureEmulatorIsRunning().firstOrNull()

                val isHardcoreModeEnabled = emulatorSession.isRetroAchievementsHardcoreModeEnabled
                val startResult = retroAchievementsRepository.startSession(rom.retroAchievementsHash, isHardcoreModeEnabled)
                if (startResult.isFailure) {
                    if (startResult.exceptionOrNull() is UserTokenExpiredException) {
                        _raIntegrationEvent.trySend(RAIntegrationEvent.LoginExpired(achievementData.icon))
                    } else {
                        _raIntegrationEvent.trySend(RAIntegrationEvent.Failed(achievementData.icon))
                    }
                } else {
                    launch {
                        retroAchievementsSubmissionHandler.startEmulatorSession().collect(_achievementsEvent)
                    }

                    emulatorManager.setupRetroAchievements(achievementData)
                    if (achievementData.hasAchievements) {
                        _raIntegrationEvent.trySend(
                            RAIntegrationEvent.Loaded(
                                icon = achievementData.icon,
                                unlockedAchievements = achievementData.unlockedAchievementCount,
                                totalAchievements = achievementData.totalAchievementCount,
                            )
                        )
                    } else {
                        _raIntegrationEvent.trySend(RAIntegrationEvent.LoadedNoAchievements(achievementData.icon))
                    }

                    delay(30.seconds)
                    while (isActive) {
                        // TODO: Should we pause the session if the app goes to background? If so, how?
                        val richPresenceDescription = MelonEmulator.getRichPresenceStatus()
                        retroAchievementsRepository.sendSessionHeartbeat(rom.retroAchievementsHash, isHardcoreModeEnabled, richPresenceDescription)
                        delay(2.minutes)
                    }
                }
            }
        }
    }

    private fun startTrackingFps() {
        sessionCoroutineScope.launch {
            while (isActive) {
                delay(1.seconds)
                _currentFps.value = emulatorManager.getFps().roundToInt()
            }
        }
    }

    private fun filterRomPauseMenuOption(option: RomPauseMenuOption): Boolean {
        return when (option) {
            RomPauseMenuOption.REWIND -> settingsRepository.isRewindEnabled() && emulatorSession.areSaveStateLoadsAllowed()
            RomPauseMenuOption.LOAD_STATE -> emulatorSession.areSaveStateLoadsAllowed()
            RomPauseMenuOption.CHEATS -> emulatorSession.areCheatsEnabled()
            // Only while this session loaded a script, even if they have all been switched off since
            RomPauseMenuOption.MODS -> emulatorManager.getLuaScripts().isNotEmpty()
            RomPauseMenuOption.VIEW_ACHIEVEMENTS -> emulatorSession.areRetroAchievementsEnabled()
            else -> true
        }
    }

    private fun ensureEmulatorIsRunning(): Flow<Unit> {
        return _emulatorState.filter { it.isRunning() }.take(1).map { }
    }

    private suspend fun startEmulatorSession(sessionType: EmulatorSession.SessionType) {
        val isUserAuthenticatedInRetroAchievements = retroAchievementsRepository.isUserAuthenticated()
        val isRetroAchievementsHardcoreModeEnabled = settingsRepository.isRetroAchievementsHardcoreEnabled()
        emulatorSession.startSession(
            areRetroAchievementsEnabled = isUserAuthenticatedInRetroAchievements,
            isRetroAchievementsHardcoreModeEnabled = isRetroAchievementsHardcoreModeEnabled,
            sessionType = sessionType,
        )
    }

    private fun dispatchSessionUpdateActions(actions: List<EmulatorSessionUpdateAction>) {
        actions.forEach {
            when (it) {
                EmulatorSessionUpdateAction.DisableRetroAchievements -> {
                    _achievementsEvent.tryEmit(RAEventUi.Reset)
                    emulatorManager.unloadRetroAchievementsData()
                    raSessionJob?.cancel()
                    raSessionJob = null
                }
                EmulatorSessionUpdateAction.EnableRetroAchievements -> {
                    (emulatorSession.currentSessionType() as? EmulatorSession.SessionType.RomSession)?.rom?.let { currentRom ->
                        startRetroAchievementsSession(currentRom)
                    }
                }
                EmulatorSessionUpdateAction.NotifyRetroAchievementsModeSwitch -> {
                    _toastEvent.tryEmit(ToastEvent.CannotSwitchRetroAchievementsMode)
                }
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        performanceLogger.stopSession()
        sessionCoroutineScope.cancel()
        emulatorManager.cleanEmulator()
        romMetadataSyncScheduler.onGameEnded()
    }

    private class EmulatorSessionCoroutineScope : CoroutineScope {
        private var currentCoroutineContext: CoroutineContext = EmptyCoroutineContext

        override val coroutineContext: CoroutineContext get() = currentCoroutineContext

        fun notifyNewSessionStarted() {
            cancel()
            currentCoroutineContext = SupervisorJob() + Dispatchers.Main.immediate
        }

        fun cancel() {
            currentCoroutineContext.cancel()
        }
    }
}