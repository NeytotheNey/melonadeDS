package me.magnum.melonds.ui.emulator

import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.hardware.display.DisplayManager
import android.hardware.input.InputManager
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.view.Display
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.annotation.StringRes
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import androidx.core.os.ConfigurationCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isGone
import androidx.core.view.isInvisible
import androidx.core.view.isVisible
import androidx.core.view.updateLayoutParams
import androidx.lifecycle.DEFAULT_ARGS_KEY
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.viewmodel.MutableCreationExtras
import androidx.recyclerview.widget.DividerItemDecoration
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.squareup.picasso.Picasso
import dagger.hilt.android.AndroidEntryPoint
import kotlin.math.abs
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.filterIsInstance
import kotlinx.coroutines.launch
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.R
import me.magnum.melonds.common.PermissionHandler
import me.magnum.melonds.databinding.ActivityEmulatorBinding
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.ControllerConfiguration
import me.magnum.melonds.domain.model.FpsCounterPosition
import me.magnum.melonds.domain.model.InputConfig
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SaveStateSlot
import me.magnum.melonds.domain.model.emulator.LuaScriptStatus
import me.magnum.melonds.domain.model.layout.Insets
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.ScreenFold
import me.magnum.melonds.domain.model.rewind.RewindWindowPosition
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.model.ui.Orientation
import me.magnum.melonds.extensions.find60HzMode
import me.magnum.melonds.extensions.setLayoutOrientation
import me.magnum.melonds.extensions.setupGameSystemBars
import me.magnum.melonds.impl.diagnostics.DebugLog
import me.magnum.melonds.impl.diagnostics.PerformanceLogger
import me.magnum.melonds.impl.emulator.LifecycleOwnerProvider
import me.magnum.melonds.impl.layout.DeviceLayoutDisplayMapper
import me.magnum.melonds.impl.layout.SecondaryDisplaySelector
import me.magnum.melonds.impl.system.AppForegroundStateObserver
import me.magnum.melonds.parcelables.RomInfoParcelable
import me.magnum.melonds.parcelables.RomParcelable
import me.magnum.melonds.ui.cheats.CheatsActivity
import me.magnum.melonds.ui.common.rom.EmulatorLaunchValidatorDelegate
import me.magnum.melonds.ui.emulator.component.EmulatorAudioFocusManager
import me.magnum.melonds.ui.emulator.component.EmulatorOverlayTracker
import me.magnum.melonds.ui.emulator.input.ConnectedControllerManager
import me.magnum.melonds.ui.emulator.input.EmulatorMotionManager
import me.magnum.melonds.ui.emulator.input.EmulatorRumbleManager
import me.magnum.melonds.ui.emulator.input.FrontendInputHandler
import me.magnum.melonds.ui.emulator.input.INativeInputListener
import me.magnum.melonds.ui.emulator.camera.WideCameraController
import me.magnum.melonds.ui.emulator.camera.CameraStickReader
import me.magnum.melonds.ui.emulator.input.InputProcessor
import me.magnum.melonds.ui.emulator.input.MelonTouchHandler
import me.magnum.melonds.ui.emulator.model.EmulatorOverlay
import me.magnum.melonds.ui.emulator.model.EmulatorState
import me.magnum.melonds.ui.emulator.model.EmulatorUiEvent
import me.magnum.melonds.ui.emulator.model.LaunchArgs
import me.magnum.melonds.ui.emulator.model.PauseMenu
import me.magnum.melonds.ui.emulator.model.RAEventUi
import me.magnum.melonds.ui.emulator.model.RewindWindowState
import me.magnum.melonds.ui.emulator.model.RumbleEvent
import me.magnum.melonds.ui.emulator.model.RuntimeInputLayoutConfiguration
import me.magnum.melonds.ui.emulator.model.LuaUiEvent
import me.magnum.melonds.ui.emulator.model.LuaUiSetting
import me.magnum.melonds.ui.emulator.ui.LuaScriptOptionsDialog
import me.magnum.melonds.ui.emulator.ui.LuaScriptOptionsDialogState
import me.magnum.melonds.ui.emulator.ui.LuaScriptsDialog
import me.magnum.melonds.ui.emulator.model.ToastEvent
import me.magnum.melonds.ui.emulator.render.ChoreographerFrameRenderer
import me.magnum.melonds.ui.emulator.render.ChoreographerFrameRendererFactory
import me.magnum.melonds.ui.emulator.render.ExternalPresentation
import me.magnum.melonds.ui.emulator.render.FrameRenderCoordinator
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow
import me.magnum.melonds.ui.emulator.rom.SaveStateAdapter
import me.magnum.melonds.ui.emulator.ui.AchievementListDialog
import me.magnum.melonds.ui.emulator.ui.AchievementUpdatesUi
import me.magnum.melonds.ui.emulator.ui.PendingSubmissionsDialog
import me.magnum.melonds.ui.emulator.ui.RewindWindowUi
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget
import me.magnum.melonds.ui.settings.SettingsActivity
import me.magnum.melonds.ui.theme.MelonTheme
import java.text.SimpleDateFormat
import javax.inject.Inject

@AndroidEntryPoint
class EmulatorActivity : AppCompatActivity() {
    companion object {
        const val KEY_ROM = "rom"
        const val KEY_PATH = "PATH"
        const val KEY_URI = "uri"
        const val KEY_BOOT_FIRMWARE_CONSOLE = "boot_firmware_console"
        const val KEY_BOOT_FIRMWARE_ONLY = "boot_firmware_only"
        // How far the left stick has to be pushed to count as the player moving, for the WideMelon camera's return to center
        private const val LEFT_STICK_MOVE_THRESHOLD = 0.3f

        fun getRomEmulatorActivityIntent(context: Context, rom: Rom): Intent {
            return Intent(context, EmulatorActivity::class.java).apply {
                putExtra(KEY_ROM, RomParcelable(rom))
            }
        }

        fun getFirmwareEmulatorActivityIntent(context: Context, consoleType: ConsoleType): Intent {
            return Intent(context, EmulatorActivity::class.java).apply {
                putExtra(KEY_BOOT_FIRMWARE_ONLY, true)
                putExtra(KEY_BOOT_FIRMWARE_CONSOLE, consoleType.ordinal)
            }
        }
    }

    private lateinit var binding: ActivityEmulatorBinding
    private val viewModel: EmulatorViewModel by viewModels(
        extrasProducer = {
            val extras = MutableCreationExtras(defaultViewModelCreationExtras)
            // Inject intent data into view-model creation extras to make it accessible through the SavedStateHandle
            intent.data?.let { dataUri ->
                val existingExtras = extras[DEFAULT_ARGS_KEY]?.let { Bundle(it) } ?: Bundle()
                existingExtras.putString(KEY_URI, dataUri.toString())
                extras[DEFAULT_ARGS_KEY] = existingExtras
            }
            extras
        }
    )

    @Inject
    lateinit var secondaryDisplaySelector: SecondaryDisplaySelector

    @Inject
    lateinit var deviceLayoutDisplayMapper: DeviceLayoutDisplayMapper

    @Inject
    lateinit var picasso: Picasso

    @Inject
    lateinit var permissionHandler: PermissionHandler

    @Inject
    lateinit var lifecycleOwnerProvider: LifecycleOwnerProvider

    @Inject
    lateinit var appForegroundStateObserver: AppForegroundStateObserver

    @Inject
    lateinit var performanceLogger: PerformanceLogger

    private var presentation: ExternalPresentation? = null
    private var pauseMenuDialog: AlertDialog? = null
    private var displayLock60HzEnabled = false

    private lateinit var handler: Handler
    private val displayListener = object : DisplayManager.DisplayListener {

        override fun onDisplayAdded(displayId: Int) {
            runOnUiThread {
                updateDisplays()
            }
        }

        override fun onDisplayRemoved(displayId: Int) {
            runOnUiThread {
                updateDisplays()
            }
        }

        override fun onDisplayChanged(displayId: Int) {
            updateDisplays()
        }
    }

    private val connectedControllerManager = ConnectedControllerManager()
    private lateinit var emulatorLaunchValidatorDelegate: EmulatorLaunchValidatorDelegate
    private lateinit var emulatorRumbleManager: EmulatorRumbleManager
    private lateinit var emulatorMotionManager: EmulatorMotionManager
    private lateinit var audioFocusManager: EmulatorAudioFocusManager
    private lateinit var frameRenderCoordinator: FrameRenderCoordinator
    private lateinit var choreographerFrameRenderer: ChoreographerFrameRenderer
    private lateinit var mainScreenRenderer: DSRenderer
    private lateinit var melonTouchHandler: MelonTouchHandler
    private lateinit var nativeInputListener: INativeInputListener
    // The WideMelon camera, shared by every surface through frameRenderCoordinator; its window goes to the scripts as well
    private val wideCameraController = WideCameraController(
        // Picked on the render thread, remembered for the game from the main one
        onZoomPicked = { session, zoom -> runOnUiThread { viewModel.onWideCameraZoomPicked(session, zoom) } },
        onCameraChanged = { x, zoom, y -> MelonEmulator.updateWideCamera(x, zoom, y) },
        // Asked once per presented frame while the camera is on, which keeps the native field read running for a known Pokemon game
        gameScene = { MelonEmulator.getGameScene() },
    )
    // Whether the right stick is free to pan the camera, which it isn't while it's mapped to an input
    private var isRightStickFreeForCamera = true
    private val cameraStickReader = CameraStickReader()
    private val frontendInputHandler = object : FrontendInputHandler() {
        var fastForwardEnabled = false
            private set
        private var fastForwardHoldActive = false
        /**
         * Whether fast-forward is running, through the toggle or while the hold input is pressed; only the toggle shows on the on-screen button.
         */
        val isFastForwardActive: Boolean
            get() = fastForwardEnabled || fastForwardHoldActive
        var microphoneEnabled = true
            private set

        override fun onSoftInputTogglePressed() {
            viewModel.luaUiOverrides.onPlayerChanged(LuaUiSetting.SHOW_CONTROLS)
            toggleSoftInputVisibility()
        }

        fun toggleSoftInputVisibility() {
            binding.viewLayoutControls.toggleSoftInputVisibility()
            presentation?.layoutView?.toggleSoftInputVisibility()
        }

        override fun onPausePressed() {
            viewModel.pauseEmulator(true)
        }

        override fun onFastForwardPressed() {
            viewModel.luaUiOverrides.onPlayerChanged(LuaUiSetting.FAST_FORWARD)
            setFastForwardEnabled(!fastForwardEnabled)
        }

        override fun onFastForwardHoldPressed() {
            setFastForwardHoldActive(true)
        }

        override fun onFastForwardHoldReleased() {
            setFastForwardHoldActive(false)
        }

        /**
         * Sets the fast-forward toggle, for Lua scripts (and for putting it back once they stop).
         */
        fun setFastForwardFromScript(enabled: Boolean) {
            if (fastForwardEnabled != enabled) {
                setFastForwardEnabled(enabled)
            }
        }

        /**
         * Turns fast-forward off when a session starts, re-applying any request this session's scripts already made.
         */
        fun resetFastForward() {
            fastForwardHoldActive = false
            setFastForwardEnabled(viewModel.luaUiOverrides.getScriptValue(LuaUiSetting.FAST_FORWARD) ?: false)
        }

        /**
         * Stops a held fast-forward when key events stop reaching the input handler, since the key release might never arrive.
         */
        fun releaseFastForwardHold() {
            setFastForwardHoldActive(false)
        }

        private fun setFastForwardEnabled(enabled: Boolean) {
            fastForwardEnabled = enabled
            binding.viewLayoutControls.setLayoutComponentToggleState(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE, fastForwardEnabled)
            presentation?.layoutView?.setLayoutComponentToggleState(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE, fastForwardEnabled)
            applyFastForwardState()
        }

        private fun setFastForwardHoldActive(active: Boolean) {
            // Skip key repeats, which send the press again while the key is held
            if (fastForwardHoldActive == active) {
                return
            }
            fastForwardHoldActive = active
            applyFastForwardState()
        }

        private fun applyFastForwardState() {
            val active = isFastForwardActive
            DebugLog.event { "Fast-forward ${if (active) "on" else "off"} (toggle=$fastForwardEnabled, hold=$fastForwardHoldActive)" }
            MelonEmulator.setFastForwardEnabled(active)
            frameRenderCoordinator.setFastForwardActive(active)
            updateDisplayModeLock(ContextCompat.getDisplayOrDefault(this@EmulatorActivity))
        }

        override fun onMicrophonePressed() {
            microphoneEnabled = !microphoneEnabled
            binding.viewLayoutControls.setLayoutComponentToggleState(LayoutComponent.BUTTON_MICROPHONE_TOGGLE, microphoneEnabled)
            presentation?.layoutView?.setLayoutComponentToggleState(LayoutComponent.BUTTON_MICROPHONE_TOGGLE, microphoneEnabled)
            MelonEmulator.setMicrophoneEnabled(microphoneEnabled)
        }

        override fun onResetPressed() {
            viewModel.resetEmulator()
        }

        override fun onSwapScreens() {
            swapScreensForPlayer()
        }

        override fun onQuickSave() {
            viewModel.doQuickSave()
        }

        override fun onQuickLoad() {
            viewModel.doQuickLoad()
        }

        override fun onRewind() {
            viewModel.onOpenRewind()
        }

        // InputProcessor leaves out a held key's repeats, so each press is one step
        override fun onCameraZoomPressed() {
            wideCameraController.onZoomPress()
        }
    }
    private val settingsLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        viewModel.onSettingsChanged()
        applyScreenSettings()
        // Leave the game paused if the pause menu was shown while away (e.g. audio focus lost to a phone call)
        if (!activeOverlays.hasActiveOverlays()) {
            viewModel.resumeEmulator()
        }
    }
    private val cheatsLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        viewModel.onCheatsChanged()
        if (!activeOverlays.hasActiveOverlays()) {
            viewModel.resumeEmulator()
        }
    }
    private val permissionRequestLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        lifecycleScope.launch {
            it.keys.forEach { permission ->
                permissionHandler.notifyPermissionStatusUpdated(permission)
            }
        }
    }
    private val backPressedCallback = object : OnBackPressedCallback(false) {
        override fun handleOnBackPressed() {
            handleBackPressed()
        }
    }

    private val rewindWindowState = mutableStateOf<RewindWindowState>(RewindWindowState.Hidden)
    private val showAchievementList = mutableStateOf(false)
    private val luaScriptsDialogState = mutableStateOf<List<LuaScriptStatus>?>(null)
    private val luaScriptOptionsDialogState = mutableStateOf<LuaScriptOptionsDialogState?>(null)
    private val showPendingSubmissionsDialog = mutableStateOf(false)

    private val activeOverlays = EmulatorOverlayTracker(
        onOverlaysCleared = {
            disableScreenTimeOut()
            presentation?.setPauseOverlayVisibility(false)
            // Closing the last overlay resumes the game, so ask for audio focus again in case it was lost for good while paused
            if (lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED) && !isFinishing) {
                audioFocusManager.requestFocus()
            }
        },
        onOverlaysPresent = {
            // Key events aren't forwarded to the input handler while overlays are shown, so the hold's release would be lost
            frontendInputHandler.releaseFastForwardHold()
            // Nor the camera's sticks and D-pad
            wideCameraController.releaseHeldInput()
            enableScreenTimeOut()
            presentation?.setPauseOverlayVisibility(true)
        }
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handler = Handler(mainLooper)
        // Make the volume keys control the game's volume, even when no sound is playing at that moment
        volumeControlStream = AudioManager.STREAM_MUSIC
        audioFocusManager = EmulatorAudioFocusManager(this, handler) {
            // Pause behind the pause menu when something else takes the audio; the player resumes from the menu
            if (viewModel.emulatorState.value.isRunning() && !activeOverlays.hasActiveOverlays() && !isFinishing) {
                viewModel.pauseEmulator(true)
            }
        }
        lifecycleOwnerProvider.setCurrentLifecycleOwner(this)
        binding = ActivityEmulatorBinding.inflate(layoutInflater)
        supportRequestWindowFeature(Window.FEATURE_NO_TITLE)
        setContentView(binding.root)
        setupFullscreen()
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            // Keep the FPS counter clear of the status bar when it's shown
            val fpsInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.statusBars())
            binding.textFps.updateLayoutParams<ConstraintLayout.LayoutParams> {
                setMargins(fpsInsets.left, fpsInsets.top, fpsInsets.right, fpsInsets.bottom)
            }
            // The layouts ignore the status bar, so only the controls are nudged out from under it
            binding.viewLayoutControls.setTopSystemBarInset(windowInsets.getInsets(WindowInsetsCompat.Type.statusBars()).top)

            val uiInsets = Insets(insets.left, insets.top, insets.right, insets.bottom)
            viewModel.setUiInsets(uiInsets)

            // The insets are consumed here, so pass them on to the Compose overlays (achievement popups) directly
            ViewCompat.dispatchApplyWindowInsets(binding.layoutCompose, windowInsets)
            WindowInsetsCompat.CONSUMED
        }

        onBackPressedDispatcher.addCallback(backPressedCallback)

        emulatorLaunchValidatorDelegate = EmulatorLaunchValidatorDelegate(this, object : EmulatorLaunchValidatorDelegate.Callback {
            override fun onRomValidated(rom: Rom) {
                viewModel.onRomLaunchValidated(rom)
            }

            override fun onFirmwareValidated(consoleType: ConsoleType) {
                viewModel.onFirmwareLaunchValidated(consoleType)
            }

            override fun onValidationAborted() {
                finish()
            }
        })
        emulatorRumbleManager = EmulatorRumbleManager(this, lifecycleScope, connectedControllerManager)
        emulatorMotionManager = EmulatorMotionManager(this, lifecycleScope, connectedControllerManager)
        frameRenderCoordinator = FrameRenderCoordinator()
        frameRenderCoordinator.wideCameraController = wideCameraController
        choreographerFrameRenderer = ChoreographerFrameRendererFactory.createFrameRenderer(frameRenderCoordinator)
        melonTouchHandler = MelonTouchHandler(wideCameraController)
        mainScreenRenderer = DSRenderer(this)
        binding.surfaceMain.apply {
            setRenderer(mainScreenRenderer)
        }
        // Marks surface recreations in the performance log, to match them with graphics memory changes
        binding.surfaceMain.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                performanceLogger.logEvent("surface_created")
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                performanceLogger.logEvent("surface_changed", "${width}x$height")
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                performanceLogger.logEvent("surface_destroyed")
            }
        })

        binding.textFps.visibility = View.INVISIBLE
        binding.viewLayoutControls.setLayoutComponentViewBuilderFactory(RuntimeLayoutComponentViewBuilderFactory())
        binding.viewLayoutControls.apply {
            setFrontendInputHandler(frontendInputHandler)
            setSystemInputHandler(melonTouchHandler)
            setFrameWidthProvider { mainScreenRenderer.frameTopScreenWidth }
            setWideWindowProvider { mainScreenRenderer.frameWideWindow }
            // A recreated activity's views start unswapped, so put back the shown swap before any Lua event is handled
            if (viewModel.areScreensSwapped) {
                swapScreens()
            }
        }

        val layoutChangeListener = View.OnLayoutChangeListener { _, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom ->
            val oldWith = oldRight - oldLeft
            val oldHeight = oldBottom - oldTop

            val newWidth = right - left
            val newHeight = bottom - top

            if (newWidth != oldWith || newHeight != oldHeight) {
                updateRendererScreenAreas()
                viewModel.setUiSize(newWidth, newHeight)
            }
        }
        binding.viewLayoutControls.addOnLayoutChangeListener(layoutChangeListener)

        updateOrientation(resources.configuration)
        disableScreenTimeOut()

        binding.layoutCompose.setContent {
            MelonTheme {
                val achievementsViewModel = viewModels<EmulatorRetroAchievementsViewModel>().value

                LaunchedEffect(Unit) {
                    viewModel.achievementsEvent.filterIsInstance<RAEventUi.Reset>().collect {
                        achievementsViewModel.onSessionReset()
                    }
                }

                AchievementUpdatesUi(viewModel)

                RewindWindowUi(
                    state = rewindWindowState.value,
                    onRewindSaveStateSelected = { state ->
                        viewModel.rewindToState(state)
                        closeRewindWindow()
                    },
                    onDismiss = ::closeRewindWindow,
                )

                if (showAchievementList.value) {
                    AchievementListDialog(
                        viewModel = achievementsViewModel,
                        onDismiss = {
                            activeOverlays.removeActiveOverlay(EmulatorOverlay.ACHIEVEMENTS_DIALOG)
                            viewModel.resumeEmulator()
                            showAchievementList.value = false
                        }
                    )
                }

                luaScriptsDialogState.value?.let { scripts ->
                    LuaScriptsDialog(
                        scripts = scripts,
                        onDisableScript = viewModel::disableLuaScript,
                        onEnableScript = viewModel::enableLuaScript,
                        onOpenOptions = ::openLuaScriptOptionsDialog,
                        onDismiss = ::closeLuaScriptsDialog,
                    )
                }

                luaScriptOptionsDialogState.value?.let { state ->
                    val layouts by remember { viewModel.getLuaLayoutChoices() }.collectAsState(initial = null)
                    LuaScriptOptionsDialog(
                        state = state,
                        layouts = layouts,
                        onSave = { values ->
                            viewModel.setLuaScriptOptions(state.scriptId, values)
                            closeLuaScriptOptionsDialog()
                        },
                        onDismiss = ::closeLuaScriptOptionsDialog,
                    )
                }

                if (showPendingSubmissionsDialog.value) {
                    PendingSubmissionsDialog(
                        pendingSubmissionsSummaryFlow = viewModel.pendingSubmissionsSummary,
                        onExit = { viewModel.exitEmulator(force = true) },
                        onCancel = {
                            activeOverlays.removeActiveOverlay(EmulatorOverlay.PENDING_SUBMISSION_CONFIRM_EXIT)
                            viewModel.resumeEmulator()
                            showPendingSubmissionsDialog.value = false
                        }
                    )
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                permissionHandler.observePermissionRequests().collect {
                    permissionRequestLauncher.launch(arrayOf(it))
                }
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.runtimeLayout.collectLatest {
                    setupSoftInput(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.controllerConfiguration.collect {
                    setupInputHandling(it)
                    connectedControllerManager.setCurrentControllerConfiguration(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                connectedControllerManager.controllersState.collect {
                    binding.viewLayoutControls.setConnectedControllersState(it)
                    presentation?.layoutView?.setConnectedControllersState(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.mainScreenBackground.collectLatest {
                    mainScreenRenderer.setBackground(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.secondaryScreenBackground.collectLatest {
                    presentation?.updateBackground(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.runtimeRendererConfiguration.collectLatest {
                    mainScreenRenderer.updateRendererConfiguration(it)
                    presentation?.updateRendererConfiguration(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.luaTiltShift.collect {
                    mainScreenRenderer.tiltShift = it
                    presentation?.updateTiltShift(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.wideCameraState.collect {
                    wideCameraController.setState(it)
                    updateWideCameraTouch()
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.luaWideCamera.collect {
                    wideCameraController.setScriptRequest(it)
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.currentFps.collectLatest {
                    if (it == null) {
                        binding.textFps.text = null
                    } else {
                        binding.textFps.text = getString(R.string.info_fps, it)
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.toastEvent.collectLatest {
                    val (message, duration) = when (it) {
                        ToastEvent.GbaLoadFailed -> R.string.error_load_gba_rom to Toast.LENGTH_SHORT
                        ToastEvent.QuickSaveSuccessful -> R.string.saved to Toast.LENGTH_SHORT
                        ToastEvent.QuickLoadSuccessful -> R.string.loaded to Toast.LENGTH_SHORT
                        ToastEvent.RewindNotEnabled -> R.string.rewind_not_enabled to Toast.LENGTH_SHORT
                        ToastEvent.RewindNotAvailableWhileRAHardcoreModeEnabled -> R.string.rewind_unavailable_ra_hardcore_enabled to Toast.LENGTH_LONG
                        ToastEvent.StateLoadFailed -> R.string.failed_load_state to Toast.LENGTH_SHORT
                        ToastEvent.StateSaveFailed -> R.string.failed_save_state to Toast.LENGTH_SHORT
                        ToastEvent.StateStateDoesNotExist -> R.string.cant_load_empty_slot to Toast.LENGTH_SHORT
                        ToastEvent.CannotUseSaveStatesWhenRAHardcoreIsEnabled -> R.string.save_states_unavailable_ra_hardcore_enabled to Toast.LENGTH_LONG
                        ToastEvent.CannotLoadStateWhenRunningFirmware,
                        ToastEvent.CannotSaveStateWhenRunningFirmware -> R.string.save_states_not_supported to Toast.LENGTH_LONG
                        ToastEvent.CannotSwitchRetroAchievementsMode -> R.string.retro_achievements_relaunch_to_apply_settings to Toast.LENGTH_LONG
                        ToastEvent.GbaModeNotSupported -> R.string.emulator_stop_gba_mode_unsupported to Toast.LENGTH_SHORT
                        ToastEvent.InternalError -> R.string.emulator_stop_internal_error to Toast.LENGTH_LONG
                    }

                    Toast.makeText(this@EmulatorActivity, message, duration).show()
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.CREATED) {
                viewModel.luaUiEvent.collect {
                    when (it) {
                        is LuaUiEvent.ScriptStopped -> {
                            if (it.error != null) {
                                val message = getString(R.string.lua_script_switched_off, it.scriptName, it.error)
                                Toast.makeText(this@EmulatorActivity, message, Toast.LENGTH_LONG).show()
                            }
                            applyLuaUiSettings(viewModel.luaUiOverrides.onScriptStopped(it.scriptId))
                        }
                        is LuaUiEvent.ScriptStarted -> viewModel.luaUiOverrides.onScriptStarted(it.scriptId)
                        is LuaUiEvent.SessionStarted -> {
                            // The game's own swap, shown before its scripts' first requests so that it's their baseline
                            viewModel.luaUiOverrides.onSessionStarted(mapOf(LuaUiSetting.SWAP_SCREENS to it.screensSwapped))
                            applyLuaUiSetting(LuaUiSetting.SWAP_SCREENS, it.screensSwapped)
                        }
                        is LuaUiEvent.Request -> {
                            val value = viewModel.luaUiOverrides.onScriptRequest(it.setting, it.scriptId, it.value, getLuaUiSetting(it.setting))
                            applyLuaUiSetting(it.setting, value)
                        }
                        LuaUiEvent.SessionEnded -> {
                            val values = viewModel.luaUiOverrides.onSessionEnded()
                            // Nothing to put back if the screen is closing
                            if (!isFinishing) {
                                applyLuaUiSettings(values)
                            }
                        }
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.CREATED) {
                viewModel.uiEvent.collectLatest {
                    when (it) {
                        EmulatorUiEvent.CloseEmulator -> {
                            choreographerFrameRenderer.stopRendering()
                            presentation?.dismiss()
                            finish()
                        }
                        is EmulatorUiEvent.OpenScreen.CheatsScreen -> {
                            val intent = Intent(this@EmulatorActivity, CheatsActivity::class.java)
                            intent.putExtra(CheatsActivity.KEY_ROM_INFO, RomInfoParcelable.fromRomInfo(it.romInfo))
                            cheatsLauncher.launch(intent)
                        }
                        EmulatorUiEvent.OpenScreen.SettingsScreen -> {
                            val settingsIntent = Intent(this@EmulatorActivity, SettingsActivity::class.java)
                            settingsLauncher.launch(settingsIntent)
                        }
                        is EmulatorUiEvent.ShowPauseMenu -> showPauseMenu(it.pauseMenu)
                        is EmulatorUiEvent.ShowRewindWindow -> showRewindWindow(it.rewindWindow, it.windowPosition)
                        is EmulatorUiEvent.ShowRomSaveStates -> {
                            showSaveStateSlotsDialog(it.saveStates) { slot ->
                                if (it.reason == EmulatorUiEvent.ShowRomSaveStates.Reason.SAVING) {
                                    viewModel.saveStateToSlot(slot)
                                } else {
                                    viewModel.loadStateFromSlot(slot)
                                }
                            }
                        }
                        EmulatorUiEvent.ShowAchievementList -> {
                            activeOverlays.addActiveOverlay(EmulatorOverlay.ACHIEVEMENTS_DIALOG)
                            showAchievementList.value = true
                        }
                        is EmulatorUiEvent.ShowLuaScripts -> showLuaScriptsDialog(it.scripts)
                        EmulatorUiEvent.ShowPendingSubmissionsDialog -> {
                            activeOverlays.addActiveOverlay(EmulatorOverlay.PENDING_SUBMISSION_CONFIRM_EXIT)
                            showPendingSubmissionsDialog.value = true
                        }
                        EmulatorUiEvent.SwapScreens -> swapScreensForPlayer()
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.rumbleEvent.collect {
                    when (it) {
                        is RumbleEvent.RumbleStart -> emulatorRumbleManager.startRumbling()
                        RumbleEvent.RumbleStop -> emulatorRumbleManager.stopRumbling()
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.CREATED) {
                viewModel.emulatorState.collectLatest {
                    when (it) {
                        is EmulatorState.Uninitialized -> {
                            binding.viewLayoutControls.isInvisible = true
                            binding.textFps.isGone = true
                            binding.textLoading.isGone = true
                        }
                        is EmulatorState.ValidatingFirmware -> {
                            showLoadingState()
                            emulatorMotionManager.stop()
                            emulatorLaunchValidatorDelegate.validateFirmware(it.consoleType)
                        }
                        is EmulatorState.ValidatingRom -> {
                            showLoadingState()
                            emulatorMotionManager.stop()
                            emulatorLaunchValidatorDelegate.validateRom(it.rom)
                        }
                        EmulatorState.LoadingFirmware,
                        EmulatorState.LoadingRom -> showLoadingState()
                        is EmulatorState.RunningRom,
                        is EmulatorState.RunningFirmware -> {
                            frontendInputHandler.resetFastForward()
                            reapplyLuaUiSettings()
                            applyScreenSettings()
                            binding.textLoading.isGone = true
                            binding.viewLayoutControls.isVisible = true
                            backPressedCallback.isEnabled = true
                            if (it is EmulatorState.RunningRom) {
                                startMotionManagerIfNeeded(it.rom)
                            }
                            // Focus losses while loading are ignored, so start paused if focus is already gone
                            if (audioFocusManager.isFocusLost && !activeOverlays.hasActiveOverlays()) {
                                viewModel.pauseEmulator(true)
                            }
                        }
                        is EmulatorState.RomLoadError -> {
                            binding.viewLayoutControls.isInvisible = true
                            binding.textFps.isGone = true
                            binding.textLoading.isGone = true
                            showRomLoadErrorDialog(R.string.error_load_rom_message)
                        }
                        is EmulatorState.RomFolderUnreadableError -> {
                            binding.viewLayoutControls.isInvisible = true
                            binding.textFps.isGone = true
                            binding.textLoading.isGone = true
                            showRomLoadErrorDialog(R.string.error_rom_folder_unreadable_message)
                        }
                        is EmulatorState.FirmwareLoadError -> {
                            binding.viewLayoutControls.isInvisible = true
                            binding.textFps.isGone = true
                            binding.textLoading.isGone = true
                            showFirmwareLoadErrorDialog(it)
                        }
                        is EmulatorState.RomNotFoundError -> {
                            binding.viewLayoutControls.isInvisible = true
                            binding.textFps.isGone = true
                            binding.textLoading.isGone = true
                            showRomNotFoundDialog(it.romPath)
                        }
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                WindowInfoTracker.getOrCreate(this@EmulatorActivity).windowLayoutInfo(this@EmulatorActivity).collect {
                    val folds = it.displayFeatures.mapNotNull {
                        if (it is FoldingFeature) {
                            ScreenFold(
                                orientation = if (it.orientation == FoldingFeature.Orientation.HORIZONTAL) Orientation.LANDSCAPE else Orientation.PORTRAIT,
                                type = if (it.isSeparating) ScreenFold.FoldType.SEAMLESS else ScreenFold.FoldType.GAP,
                                foldBounds = Rect(it.bounds.left, it.bounds.top, it.bounds.width(), it.bounds.height())
                            )
                        } else {
                            null
                        }
                    }
                    viewModel.setScreenFolds(folds)
                    val foldStates = it.displayFeatures.filterIsInstance<FoldingFeature>().map { feature -> feature.state.toString() }
                    performanceLogger.logStateChange("fold", foldStates.joinToString(" ").ifEmpty { "none" })
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.CREATED) {
                appForegroundStateObserver.onAppMovedToBackgroundEvent.collect {
                    presentation?.dismiss()
                    presentation = null
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        updateDisplays()
        getSystemService<DisplayManager>()?.registerDisplayListener(displayListener, null)
        getSystemService<InputManager>()?.registerInputDeviceListener(connectedControllerManager, null)
        connectedControllerManager.startTrackingControllers()
        frameRenderCoordinator.addSurface(binding.surfaceMain)
        performanceLogger.logEvent("activity_start")
    }

    private fun updateDisplays() {
        val currentDisplay = ContextCompat.getDisplayOrDefault(this)
        val secondaryDisplay = secondaryDisplaySelector.getSecondaryDisplay(this)
        // Mode IDs are per display (e.g. a foldable's inner and cover screens), so the 60 Hz mode has to be picked again whenever the displays change
        updateDisplayModeLock(currentDisplay)

        val displays = deviceLayoutDisplayMapper.mapDisplaysToLayoutDisplays(currentDisplay, secondaryDisplay)
        viewModel.setConnectedDisplays(displays)
        displays.mainScreenDisplay.let {
            performanceLogger.logStateChange("display", "${it.id} ${it.width}x${it.height}")
        }

        showExternalDisplay(secondaryDisplay)
    }

    private fun showExternalDisplay(secondaryDisplay: Display?) {
        if (presentation?.display?.displayId == secondaryDisplay?.displayId) {
            return
        }

        presentation?.dismiss()
        presentation = null

        if (secondaryDisplay != null) {
            presentation = ExternalPresentation(
                context = this,
                display = secondaryDisplay,
                frameRenderCoordinator = frameRenderCoordinator,
            ).apply {
                layoutView.apply {
                    setLayoutComponentViewBuilderFactory(RuntimeLayoutComponentViewBuilderFactory())
                    setFrontendInputHandler(frontendInputHandler)
                    setSystemInputHandler(melonTouchHandler)
                    setWideCamera(wideCameraController.takeIf { it.isEnabled })
                    viewModel.runtimeLayout.value?.let {
                        updateLayout(it)
                    }

                    setLayoutComponentToggleState(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE, frontendInputHandler.fastForwardEnabled)
                    setLayoutComponentToggleState(LayoutComponent.BUTTON_MICROPHONE_TOGGLE, frontendInputHandler.microphoneEnabled)
                    setConnectedControllersState(connectedControllerManager.controllersState.value)
                }

                updateRendererConfiguration(viewModel.runtimeRendererConfiguration.value)
                updateTiltShift(viewModel.luaTiltShift.value)
                updateBackground(viewModel.secondaryScreenBackground.value)
                if (binding.viewLayoutControls.areScreensSwapped()) {
                    swapScreens()
                }
                if (activeOverlays.hasActiveOverlays()) {
                    setPauseOverlayVisibility(true)
                }

                show()
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)

        val launchArgs = LaunchArgs.fromIntent(intent)
        // Invalid arguments. Ignore completely
        if (launchArgs == null)
            return

        val currentState = viewModel.emulatorState.value
        if (currentState.isRunning()) {
            // If the same ROM/firmware is already running, ignore the intent
            if (launchArgs.matchesRunningState(currentState)) {
                return
            }

            viewModel.pauseEmulator(false)

            activeOverlays.addActiveOverlay(EmulatorOverlay.SWITCH_NEW_ROM_DIALOG)
            MaterialAlertDialogBuilder(this)
                    .setTitle(getString(R.string.title_emulator_running))
                    .setMessage(getString(R.string.message_stop_emulation))
                    .setPositiveButton(R.string.ok) { _, _ ->
                        setIntent(intent)
                        viewModel.relaunchWithNewArgs(launchArgs)
                    }
                    .setNegativeButton(R.string.no) { dialog, _ ->
                        dialog.cancel()
                    }
                    .setOnDismissListener {
                        activeOverlays.removeActiveOverlay(EmulatorOverlay.SWITCH_NEW_ROM_DIALOG)
                    }
                    .setOnCancelListener {
                        viewModel.resumeEmulator()
                    }
                    .show()
        }
    }

    override fun onResume() {
        super.onResume()
        performanceLogger.logEvent("activity_resume")
        choreographerFrameRenderer.startRendering()
        emulatorMotionManager.resume()

        if (!activeOverlays.hasActiveOverlays()) {
            disableScreenTimeOut()
            audioFocusManager.requestFocus()
            viewModel.resumeEmulator()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus) {
            // Key events stop arriving without focus, so the hold's release would be lost
            frontendInputHandler.releaseFastForwardHold()
        }
        setupFullscreen()
    }

    private fun setupFullscreen() {
        // Runs on every focus change, so a change made in the settings is picked up when returning to the game
        window.setupGameSystemBars(viewModel.showStatusBar())
    }

    private fun startMotionManagerIfNeeded(rom: Rom) {
        val gbaSlotConfig = rom.config.gbaSlotConfig
        if (gbaSlotConfig is RomGbaSlotConfig.MotionPakHomebrew || gbaSlotConfig is RomGbaSlotConfig.MotionPakRetail) {
            emulatorMotionManager.start()
        }
    }

    /**
     * Sets up the window, the surfaces and the FPS counter from the settings.
     */
    private fun applyScreenSettings() {
        val settings = viewModel.getScreenSettings()
        window.setSustainedPerformanceMode(settings.isSustainedPerformanceModeEnabled)
        // Only sets the surfaces' frame rate; the emulator gets frame pacing through its configuration
        frameRenderCoordinator.setFramePacingEnabled(settings.isFramePacingEnabled)
        displayLock60HzEnabled = settings.isDisplayLock60HzEnabled
        updateDisplayModeLock(ContextCompat.getDisplayOrDefault(this))
        setupFpsCounter(settings.fpsCounterPosition)
    }

    /**
     * Pins the window to the display's 60 Hz mode while the 60 Hz lock is on and fast-forward is off, since some devices ignore the surface hint.
     */
    private fun updateDisplayModeLock(display: Display) {
        val lockActive = displayLock60HzEnabled && !frontendInputHandler.isFastForwardActive
        val preferredModeId = if (lockActive) (display.find60HzMode()?.modeId ?: 0) else 0
        val attributes = window.attributes
        // Only update the window when the mode changes, since that triggers onDisplayChanged() and so this again
        if (attributes.preferredDisplayModeId != preferredModeId) {
            attributes.preferredDisplayModeId = preferredModeId
            window.attributes = attributes
        }
    }

    private fun setupFpsCounter(fpsCounterPosition: FpsCounterPosition) {
        if (fpsCounterPosition == FpsCounterPosition.HIDDEN) {
            binding.textFps.isGone = true
        } else {
            binding.textFps.isVisible = true
            val newParams = binding.textFps.layoutParams as ConstraintLayout.LayoutParams
            when (fpsCounterPosition) {
                FpsCounterPosition.TOP_LEFT -> {
                    newParams.topToTop = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.leftToLeft = ConstraintLayout.LayoutParams.PARENT_ID
                }
                FpsCounterPosition.TOP_CENTER -> {
                    newParams.topToTop = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.leftToLeft = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.rightToRight = ConstraintLayout.LayoutParams.PARENT_ID
                }
                FpsCounterPosition.TOP_RIGHT -> {
                    newParams.topToTop = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.rightToRight = ConstraintLayout.LayoutParams.PARENT_ID
                }
                FpsCounterPosition.BOTTOM_LEFT -> {
                    newParams.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.leftToLeft = ConstraintLayout.LayoutParams.PARENT_ID
                }
                FpsCounterPosition.BOTTOM_CENTER -> {
                    newParams.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.leftToLeft = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.rightToRight = ConstraintLayout.LayoutParams.PARENT_ID
                }
                FpsCounterPosition.BOTTOM_RIGHT -> {
                    newParams.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID
                    newParams.rightToRight = ConstraintLayout.LayoutParams.PARENT_ID
                }
                FpsCounterPosition.HIDDEN -> { /* Do nothing here */ }
            }
            binding.textFps.layoutParams = newParams
        }
    }

    private fun setupSoftInput(layoutConfiguration: RuntimeInputLayoutConfiguration?) {
        if (layoutConfiguration != null) {
            setLayoutOrientation(layoutConfiguration.layoutOrientation)
            with(binding.viewLayoutControls) {
                instantiateLayout(layoutConfiguration, LayoutTarget.MAIN_SCREEN)
                setLayoutComponentToggleState(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE, frontendInputHandler.fastForwardEnabled)
                setLayoutComponentToggleState(LayoutComponent.BUTTON_MICROPHONE_TOGGLE, frontendInputHandler.microphoneEnabled)
            }
            handler.post {
                updateRendererScreenAreas()
            }

            presentation?.apply {
                updateLayout(layoutConfiguration)
                layoutView.setLayoutComponentToggleState(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE, frontendInputHandler.fastForwardEnabled)
                layoutView.setLayoutComponentToggleState(LayoutComponent.BUTTON_MICROPHONE_TOGGLE, frontendInputHandler.microphoneEnabled)
            }
        } else {
            binding.viewLayoutControls.destroyLayout()
            presentation?.layoutView?.destroyLayout()
        }
    }

    private fun getLuaUiSetting(setting: LuaUiSetting): Boolean {
        return when (setting) {
            LuaUiSetting.SWAP_SCREENS -> binding.viewLayoutControls.areScreensSwapped()
            LuaUiSetting.SHOW_CONTROLS -> binding.viewLayoutControls.isSoftInputVisible()
            LuaUiSetting.FAST_FORWARD -> frontendInputHandler.fastForwardEnabled
        }
    }

    /**
     * Changes a setting for Lua scripts if it differs from what the UI shows, without counting as the player's change.
     */
    private fun applyLuaUiSetting(setting: LuaUiSetting, value: Boolean) {
        if (getLuaUiSetting(setting) == value) {
            return
        }

        when (setting) {
            LuaUiSetting.SWAP_SCREENS -> swapScreen()
            LuaUiSetting.SHOW_CONTROLS -> frontendInputHandler.toggleSoftInputVisibility()
            LuaUiSetting.FAST_FORWARD -> frontendInputHandler.setFastForwardFromScript(value)
        }
    }

    /**
     * Puts back the swap and controls values the scripts hold after the activity is recreated.
     */
    private fun reapplyLuaUiSettings() {
        listOf(LuaUiSetting.SWAP_SCREENS, LuaUiSetting.SHOW_CONTROLS).forEach { setting ->
            viewModel.luaUiOverrides.getScriptValue(setting)?.let { applyLuaUiSetting(setting, it) }
        }
    }

    private fun applyLuaUiSettings(values: Map<LuaUiSetting, Boolean>) {
        values.forEach { (setting, value) -> applyLuaUiSetting(setting, value) }
    }

    /**
     * The player swapped the screens (with the button, a controller or the pause menu), so no script holds the swap anymore
     */
    private fun swapScreensForPlayer() {
        viewModel.luaUiOverrides.onPlayerChanged(LuaUiSetting.SWAP_SCREENS)
        swapScreen()
        viewModel.onPlayerSwappedScreens(binding.viewLayoutControls.areScreensSwapped())
    }

    private fun swapScreen() {
        binding.viewLayoutControls.swapScreens()
        presentation?.swapScreens()
        viewModel.areScreensSwapped = binding.viewLayoutControls.areScreensSwapped()

        updateRendererScreenAreas()
    }

    /**
     * Gives the layouts' DS top screen to the camera's drags and pinches while the camera is on
     */
    private fun updateWideCameraTouch() {
        val camera = wideCameraController.takeIf { it.isEnabled }
        binding.viewLayoutControls.setWideCamera(camera)
        presentation?.layoutView?.setWideCamera(camera)
    }

    private fun updateRendererScreenAreas() {
        wideCameraController.setScreensSwapped(binding.viewLayoutControls.areScreensSwapped())
        val touchScreenArea = binding.viewLayoutControls.applyScreenAreasTo(mainScreenRenderer)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            window?.systemGestureExclusionRects = touchScreenArea
        }
    }

    private fun setupInputHandling(controllerConfiguration: ControllerConfiguration) {
        nativeInputListener = InputProcessor(controllerConfiguration, melonTouchHandler, frontendInputHandler)
        isRightStickFreeForCamera = listOf(MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ).all { axis ->
            InputConfig.Assignment.Axis.Direction.entries.all { controllerConfiguration.axisToInput(axis, it) == null }
        }
    }

    private fun handleBackPressed() {
        viewModel.pauseEmulator(true)
    }

    private fun showPauseMenu(pauseMenu: PauseMenu) {
        // Ignore repeated requests (e.g. a quick double tap on the menu button) while the menu is already open
        if (pauseMenuDialog?.isShowing == true) {
            return
        }

        val options = Array(pauseMenu.options.size) {
            getString(pauseMenu.options[it].textResource)
        }

        activeOverlays.addActiveOverlay(EmulatorOverlay.PAUSE_MENU)
        pauseMenuDialog = MaterialAlertDialogBuilder(this)
                .setTitle(R.string.pause)
                .setItems(options) { _, which ->
                    val selectedOption = pauseMenu.options[which]
                    viewModel.onPauseMenuOptionSelected(selectedOption)
                }
                .setOnDismissListener {
                    activeOverlays.removeActiveOverlay(EmulatorOverlay.PAUSE_MENU)
                }
                .setOnCancelListener {
                    viewModel.resumeEmulator()
                }
                .show()
    }

    private fun disableScreenTimeOut() {
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun enableScreenTimeOut() {
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (!activeOverlays.hasActiveOverlays() && nativeInputListener.onKeyEvent(event))
            return true

        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        // Lua scripts can read the sticks, so send them while one runs
        if (event.isFromSource(InputDevice.SOURCE_CLASS_JOYSTICK) && viewModel.areLuaScriptsRunning()) {
            MelonEmulator.updateLuaSticks(
                event.getAxisValue(MotionEvent.AXIS_X),
                event.getAxisValue(MotionEvent.AXIS_Y),
                event.getAxisValue(MotionEvent.AXIS_Z),
                event.getAxisValue(MotionEvent.AXIS_RZ),
            )
        }

        // The camera's sticks, not while a menu or dialog is over the game
        if (event.isFromSource(InputDevice.SOURCE_CLASS_JOYSTICK) && wideCameraController.isEnabled && !activeOverlays.hasActiveOverlays()) {
            if (isRightStickFreeForCamera) {
                cameraStickReader.readRight(event)
            }
            val rightX = if (isRightStickFreeForCamera) cameraStickReader.rightX else 0f
            val rightY = if (isRightStickFreeForCamera) cameraStickReader.rightY else 0f
            val isLeftStickPushed = maxOf(abs(event.getAxisValue(MotionEvent.AXIS_X)), abs(event.getAxisValue(MotionEvent.AXIS_Y))) > LEFT_STICK_MOVE_THRESHOLD
            wideCameraController.onSticks(rightX, rightY, isLeftStickPushed)
        }

        if (!activeOverlays.hasActiveOverlays() && nativeInputListener.onMotionEvent(event))
            return true

        return super.dispatchGenericMotionEvent(event)
    }

    private fun showSaveStateSlotsDialog(slots: List<SaveStateSlot>, onSlotPicked: (SaveStateSlot) -> Unit) {
        val dateFormatter = SimpleDateFormat("EEE, dd MMM yyyy", ConfigurationCompat.getLocales(resources.configuration)[0])
        val timeFormatter = SimpleDateFormat("kk:mm:ss", ConfigurationCompat.getLocales(resources.configuration)[0])
        var dialog: AlertDialog? = null
        var adapter: SaveStateAdapter? = null

        adapter = SaveStateAdapter(
            slots = slots,
            picasso = picasso,
            dateFormat = dateFormatter,
            timeFormat = timeFormatter,
            onSlotSelected = {
                dialog?.dismiss()
                onSlotPicked(it)
            },
            onDeletedSlot = {
                viewModel.deleteSaveStateSlot(it)?.let { newSlots ->
                    adapter?.updateSaveStateSlots(newSlots)
                }
            },
        )

        val recyclerView = RecyclerView(this).apply {
            val layoutManager = LinearLayoutManager(this@EmulatorActivity)
            this.layoutManager = layoutManager
            addItemDecoration(DividerItemDecoration(context, layoutManager.orientation))
            this.adapter = adapter
            descendantFocusability = ViewGroup.FOCUS_AFTER_DESCENDANTS
        }

        activeOverlays.addActiveOverlay(EmulatorOverlay.SAVE_STATES_DIALOG)

        dialog = MaterialAlertDialogBuilder(this)
            .setTitle(getString(R.string.save_slot))
            .setView(recyclerView)
            .setNegativeButton(R.string.cancel) { _dialog, _ ->
                _dialog.cancel()
            }
            .setOnDismissListener {
                activeOverlays.removeActiveOverlay(EmulatorOverlay.SAVE_STATES_DIALOG)
            }
            .setOnCancelListener {
                viewModel.resumeEmulator()
            }
            .show()
    }

    private fun showLuaScriptsDialog(scripts: List<LuaScriptStatus>) {
        activeOverlays.addActiveOverlay(EmulatorOverlay.LUA_SCRIPTS_DIALOG)
        luaScriptsDialogState.value = scripts
    }

    private fun closeLuaScriptsDialog() {
        luaScriptsDialogState.value = null
        activeOverlays.removeActiveOverlay(EmulatorOverlay.LUA_SCRIPTS_DIALOG)
        viewModel.resumeEmulator()
    }

    private fun openLuaScriptOptionsDialog(scriptId: Int, script: LuaScriptStatus) {
        // Added before the scripts dialog goes, so the emulator stays paused in between
        activeOverlays.addActiveOverlay(EmulatorOverlay.LUA_SCRIPT_OPTIONS_DIALOG)
        luaScriptOptionsDialogState.value = LuaScriptOptionsDialogState(scriptId, script.name, script.options, script.optionValues)
        // Closed without resuming the emulator
        luaScriptsDialogState.value = null
        activeOverlays.removeActiveOverlay(EmulatorOverlay.LUA_SCRIPTS_DIALOG)
    }

    private fun closeLuaScriptOptionsDialog() {
        luaScriptOptionsDialogState.value = null
        activeOverlays.removeActiveOverlay(EmulatorOverlay.LUA_SCRIPT_OPTIONS_DIALOG)
        viewModel.resumeEmulator()
    }

    private fun showRomLoadErrorDialog(@StringRes message: Int) {
        activeOverlays.addActiveOverlay(EmulatorOverlay.ROM_LOAD_ERROR_DIALOG)
        MaterialAlertDialogBuilder(this)
            .setCancelable(false)
            .setTitle(R.string.error_load_rom)
            .setMessage(message)
            .setPositiveButton(R.string.ok) { dialog, _ ->
                dialog.dismiss()
                finish()
            }
            .show()
    }

    private fun showRomNotFoundDialog(romPath: String) {
        activeOverlays.addActiveOverlay(EmulatorOverlay.ROM_NOT_FOUND_DIALOG)
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.error_rom_not_found)
            .setMessage(getString(R.string.error_rom_not_found_info, romPath))
            .setPositiveButton(R.string.ok) { _, _ ->
                finish()
            }
            .setOnDismissListener {
                finish()
            }
            .show()
    }

    private fun showFirmwareLoadErrorDialog(error: EmulatorState.FirmwareLoadError) {
        activeOverlays.addActiveOverlay(EmulatorOverlay.FIRMWARE_LOAD_ERROR_DIALOG)
        MaterialAlertDialogBuilder(this)
            .setCancelable(false)
            .setTitle(R.string.error_load_firmware)
            .setMessage(resources.getString(R.string.error_load_firmware_message, error.reason.toString()))
            .setPositiveButton(R.string.ok) { dialog, _ ->
                dialog.dismiss()
                finish()
            }
            .show()
    }

    private fun showRewindWindow(rewindWindow: RewindWindow, windowPosition: RewindWindowPosition) {
        activeOverlays.addActiveOverlay(EmulatorOverlay.REWIND_WINDOW)
        rewindWindowState.value = RewindWindowState.Visible(rewindWindow, windowPosition)
    }

    private fun closeRewindWindow() {
        activeOverlays.removeActiveOverlay(EmulatorOverlay.REWIND_WINDOW)
        rewindWindowState.value = RewindWindowState.Hidden
        viewModel.resumeEmulator()
    }

    private fun showLoadingState() {
        binding.viewLayoutControls.isInvisible = true
        binding.textFps.isGone = true
        binding.textLoading.isVisible = true
    }

    private fun updateOrientation(configuration: Configuration) {
        val orientation = if (configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
            Orientation.PORTRAIT
        } else {
            Orientation.LANDSCAPE
        }
        viewModel.setSystemOrientation(orientation)
    }

    override fun onPause() {
        super.onPause()
        performanceLogger.logEvent("activity_pause")
        frontendInputHandler.releaseFastForwardHold()
        enableScreenTimeOut()
        choreographerFrameRenderer.stopRendering()
        emulatorMotionManager.pause()
        viewModel.pauseEmulator(false)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        updateOrientation(newConfig)
        // There is an issue in which, after moving the app to a different display, the app reports that it is still running on the previous display. Adding a frame of delay
        // seems to fix the problem.
        handler.post {
            updateDisplays()
        }
    }

    override fun onStop() {
        super.onStop()
        getSystemService<DisplayManager>()?.unregisterDisplayListener(displayListener)
        getSystemService<InputManager>()?.unregisterInputDeviceListener(connectedControllerManager)
        connectedControllerManager.stopTrackingControllers()
        frameRenderCoordinator.removeSurface(binding.surfaceMain)
        performanceLogger.logEvent("activity_stop")
    }

    override fun onDestroy() {
        super.onDestroy()
        audioFocusManager.abandonFocus()
        emulatorMotionManager.stop()
        frameRenderCoordinator.stop()
        presentation?.dismiss()
    }
}