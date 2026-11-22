package me.magnum.melonds.impl.emulator

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.R
import me.magnum.melonds.common.PermissionHandler
import me.magnum.melonds.common.romprocessors.RomFileProcessorFactory
import me.magnum.melonds.common.runtime.ScreenshotFrameBufferProvider
import me.magnum.melonds.domain.model.Cheat
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.EmulatorConfiguration
import me.magnum.melonds.domain.model.MicSource
import me.magnum.melonds.domain.model.emulator.EmulatorEvent
import me.magnum.melonds.domain.model.emulator.FirmwareLaunchResult
import me.magnum.melonds.domain.model.emulator.LuaScriptHeader
import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.domain.model.emulator.LuaScriptOptionValue
import me.magnum.melonds.domain.model.emulator.LuaScriptSetup
import me.magnum.melonds.domain.model.emulator.LuaScriptStatus
import me.magnum.melonds.domain.model.emulator.RomLaunchResult
import me.magnum.melonds.domain.model.emulator.resolveValues
import me.magnum.melonds.domain.model.retroachievements.GameAchievementData
import me.magnum.melonds.domain.model.camera.WideCameraRequest
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
import me.magnum.melonds.domain.model.render.Shade
import me.magnum.melonds.domain.model.render.Sun
import me.magnum.melonds.domain.model.render.TiltShift
import me.magnum.melonds.domain.model.retroachievements.RAEvent
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeEnum
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.domain.services.EmulatorManager
import me.magnum.melonds.impl.LuaScriptDataStore
import me.magnum.melonds.impl.LuaScriptHeaderParser
import me.magnum.melonds.impl.LuaScriptStorage
import me.magnum.melonds.impl.camera.DSiCameraSourceMultiplexer
import me.magnum.melonds.impl.diagnostics.DebugLog
import me.magnum.melonds.impl.diagnostics.DebugSwitches
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow

class AndroidEmulatorManager(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
    private val sramProvider: SramProvider,
    private val saveBackupManager: SaveBackupManager,
    private val screenshotFrameBufferProvider: ScreenshotFrameBufferProvider,
    private val romFileProcessorFactory: RomFileProcessorFactory,
    private val permissionHandler: PermissionHandler,
    private val cameraManager: DSiCameraSourceMultiplexer,
    private val luaScriptStorage: LuaScriptStorage,
    private val luaScriptDataStore: LuaScriptDataStore,
) : EmulatorManager {

    private val _emulatorEvents = MutableSharedFlow<EmulatorEvent>(extraBufferCapacity = Int.MAX_VALUE)
    override val emulatorEvents: Flow<EmulatorEvent> = _emulatorEvents.asSharedFlow()

    private val achievementsSharedFlow = MutableSharedFlow<RAEvent>(replay = 0, extraBufferCapacity = Int.MAX_VALUE)

    // The session's Lua scripts, changed by the message queue and UI threads under luaScriptsLock as whole-state replacements that readers
    // take without the lock
    private val luaScriptsLock = Any()
    @Volatile
    private var luaScriptsState = LuaScriptsState.EMPTY

    // The running game's widescreen width and camera margin: the launch ones, then whatever setWideView() changed them to
    @Volatile
    private var romWideViewWidth = 0
    @Volatile
    private var romWideViewMargin = 0

    // The running game's save file, backed up at game start and with each state load; null for a firmware boot
    @Volatile
    private var currentSram: SramSession? = null

    /**
     * The running game's save, and the mark that tells the ROM details screen not to restore a backup over it
     */
    private class SramSession(val location: SramLocation, val runningGame: SaveBackupManager.RunningGame)

    // Writes the scripts' storage snapshots, drained and written under luaDataLock so an older one can't overwrite a newer one
    private val luaDataScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val luaDataLock = Any()

    private val messageQueue = EmulatorMessageQueue { type, data ->
        when (type) {
            EmulatorEventType.EventRumbleStart -> _emulatorEvents.tryEmit(EmulatorEvent.RumbleStart(data.getInt()))
            EmulatorEventType.EventRumbleStop -> _emulatorEvents.tryEmit(EmulatorEvent.RumbleStop)
            EmulatorEventType.EventEmulatorStop -> getStopReason(data.getInt())?.let { _emulatorEvents.tryEmit(EmulatorEvent.Stop(it)) }
            EmulatorEventType.EventRAAchievementPrimed -> achievementsSharedFlow.tryEmit(RAEvent.OnAchievementPrimed(data.getLong()))
            EmulatorEventType.EventRAAchievementTriggered -> achievementsSharedFlow.tryEmit(RAEvent.OnAchievementTriggered(data.getLong()))
            EmulatorEventType.EventRAAchievementUnprimed -> achievementsSharedFlow.tryEmit(RAEvent.OnAchievementUnPrimed(data.getLong()))
            EmulatorEventType.EventRAAchievementProgressUpdated -> {
                val event = RAEvent.OnAchievementProgressUpdated(
                    achievementId = data.getLong(),
                    current = data.getInt(),
                    target = data.getInt(),
                    progress = String(ByteArray(data.getInt()).apply { data.get(this) }),
                )
                achievementsSharedFlow.tryEmit(event)
            }
            EmulatorEventType.EventRALeaderboardAttemptStarted -> achievementsSharedFlow.tryEmit(RAEvent.OnLeaderboardAttemptStarted(data.getLong()))
            EmulatorEventType.EventRALeaderboardAttemptUpdated -> {
                val event = RAEvent.OnLeaderboardAttemptUpdated(
                    leaderboardId = data.getLong(),
                    formattedValue = String(ByteArray(data.getInt()).apply { data.get(this) }),
                )
                achievementsSharedFlow.tryEmit(event)
            }
            EmulatorEventType.EventRALeaderboardAttemptCanceled -> achievementsSharedFlow.tryEmit(RAEvent.OnLeaderboardAttemptCancelled(data.getLong()))
            EmulatorEventType.EventRALeaderboardAttemptCompleted -> {
                val event = RAEvent.OnLeaderboardAttemptCompleted(
                    leaderboardId = data.getLong(),
                    value = data.getInt(),
                    formattedValue = String(ByteArray(data.getInt()).apply { data.get(this) }),
                )
                achievementsSharedFlow.tryEmit(event)
            }
            EmulatorEventType.EventLuaScriptStopped -> {
                val scriptId = data.getInt()
                val error = String(ByteArray(data.getInt()).apply { data.get(this) })
                synchronized(luaScriptsLock) {
                    val state = luaScriptsState
                    luaScriptsState = state.copy(runningIds = state.runningIds - scriptId, errors = state.errors + (scriptId to error))
                }
                val scriptName = luaScriptsState.names.getOrNull(scriptId) ?: "?"
                _emulatorEvents.tryEmit(EmulatorEvent.LuaScriptStopped(scriptId, scriptName, error))
            }
            EmulatorEventType.EventLuaSwapScreens -> {
                val scriptId = data.getInt()
                _emulatorEvents.tryEmit(EmulatorEvent.LuaSwapScreens(scriptId, data.getInt() != 0))
            }
            EmulatorEventType.EventLuaShowControls -> {
                val scriptId = data.getInt()
                _emulatorEvents.tryEmit(EmulatorEvent.LuaShowControls(scriptId, data.getInt() != 0))
            }
            EmulatorEventType.EventLuaFastForward -> {
                val scriptId = data.getInt()
                _emulatorEvents.tryEmit(EmulatorEvent.LuaFastForward(scriptId, data.getInt() != 0))
            }
            EmulatorEventType.EventLuaDataSaved -> luaDataScope.launch { saveLuaData() }
            EmulatorEventType.EventLuaSetLayout -> {
                val scriptId = data.getInt()
                val key = String(ByteArray(data.getInt()).apply { data.get(this) })
                _emulatorEvents.tryEmit(EmulatorEvent.LuaSetLayout(scriptId, key.ifEmpty { null }))
            }
            EmulatorEventType.EventLuaStateLoaded -> _emulatorEvents.tryEmit(EmulatorEvent.LuaStateLoaded)
            EmulatorEventType.EventLuaTiltShift -> {
                val scriptId = data.getInt()
                val tiltShift = TiltShift(
                    top = data.getFloat(),
                    bottom = data.getFloat(),
                    center = data.getFloat(),
                    width = data.getFloat(),
                    radius = data.getFloat(),
                    saturation = data.getFloat(),
                    contrast = data.getFloat(),
                    // Negative when the script didn't pass it
                    color = data.getFloat().takeIf { it >= 0f },
                )
                _emulatorEvents.tryEmit(EmulatorEvent.LuaTiltShift(scriptId, tiltShift))
            }
            EmulatorEventType.EventLuaWideCamera -> {
                val scriptId = data.getInt()
                val zoom = data.getFloat().takeIf { it > 0f }
                val hasPan = data.getInt() != 0
                val pan = data.getFloat().takeIf { hasPan }
                val holdNative = data.getInt() != 0
                val hasPanY = data.getInt() != 0
                val panY = data.getFloat().takeIf { hasPanY }
                val request = WideCameraRequest(zoom = zoom, pan = pan, holdNative = holdNative, panY = panY)
                _emulatorEvents.tryEmit(EmulatorEvent.LuaWideCamera(scriptId, request))
            }
            EmulatorEventType.EventLuaBloom -> {
                val scriptId = data.getInt()
                val bloom = Bloom(
                    strength = data.getFloat(),
                    threshold = data.getFloat(),
                    radius = data.getFloat(),
                    vignette = data.getFloat(),
                    vignetteSize = data.getFloat(),
                    onlyThreeD = data.getInt() != 0,
                    haze = data.getFloat(),
                    hazeColor = data.getInt(),
                    hazeStart = data.getFloat(),
                    hazeFalloff = data.getFloat(),
                )
                _emulatorEvents.tryEmit(EmulatorEvent.LuaBloom(scriptId, bloom))
            }
            EmulatorEventType.EventLuaDepthOfField -> {
                val scriptId = data.getInt()
                val depthOfField = DepthOfField(
                    strength = data.getFloat(),
                    radius = data.getFloat(),
                    focusX = data.getFloat(),
                    focusY = data.getFloat(),
                    focusDepth = data.getFloat(),
                    range = data.getFloat(),
                    falloff = data.getFloat(),
                )
                _emulatorEvents.tryEmit(EmulatorEvent.LuaDepthOfField(scriptId, depthOfField))
            }
            EmulatorEventType.EventLuaShade -> {
                val scriptId = data.getInt()
                val shade = Shade(
                    strength = data.getFloat(),
                    radius = data.getFloat(),
                    range = data.getFloat(),
                    color = data.getInt(),
                )
                _emulatorEvents.tryEmit(EmulatorEvent.LuaShade(scriptId, shade))
            }
            EmulatorEventType.EventLuaSun -> {
                val scriptId = data.getInt()
                val sun = Sun(
                    strength = data.getFloat(),
                    directionX = data.getFloat(),
                    directionY = data.getFloat(),
                    directionZ = data.getFloat(),
                    upX = data.getFloat(),
                    upY = data.getFloat(),
                    upZ = data.getFloat(),
                    length = data.getFloat(),
                    steps = data.getInt(),
                    bias = data.getFloat(),
                    softness = data.getFloat(),
                    color = data.getInt(),
                    near = data.getFloat(),
                )
                _emulatorEvents.tryEmit(EmulatorEvent.LuaSun(scriptId, sun))
            }
        }
    }

    override suspend fun loadRom(
        rom: Rom,
        cheats: List<Cheat>,
        luaScripts: List<RomLuaScript>,
        luaScriptFolder: Uri?,
        areLuaStateLoadsAllowed: Boolean,
        wideViewWidth: Int,
        wideViewMargin: Int,
    ): RomLaunchResult {
        romWideViewWidth = wideViewWidth
        romWideViewMargin = wideViewMargin
        return withContext(Dispatchers.IO) {
            val fileRomDocument = DocumentFile.fromSingleUri(context, rom.uri) ?: return@withContext RomLaunchResult.LaunchFailedRomNotFound
            val fileRomProcessor = romFileProcessorFactory.getFileRomProcessorForDocument(fileRomDocument)
            val romUri = fileRomProcessor?.getRealRomUri(rom) ?: return@withContext RomLaunchResult.LaunchFailedRomNotSupported

            val configuration = getRomEmulatorConfiguration(rom)
            DebugLog.logConfiguration("rom launch", rom, configuration, cheats.size, settingsRepository)
            setupEmulator(configuration)

            endSramSession()
            val sram = try {
                sramProvider.getSramForRom(rom)
            } catch (exception: RomFolderUnreadableException) {
                Log.w(DebugLog.TAG, "Not starting the game", exception)
                return@withContext RomLaunchResult.LaunchFailedRomFolderUnreadable
            } catch (exception: SramLoadException) {
                return@withContext RomLaunchResult.LaunchFailedSramProblem(exception)
            }

            // Set before the game reads its .sav, so a restore from the ROM details screen waits for it or is refused
            currentSram = SramSession(sram, saveBackupManager.markGameRunning(rom, sram))

            // Before the core reads the .sav, so nothing is writing it: this game's SaveManager doesn't exist yet, and the last game's wrote its
            // final flush when its instance was destroyed, in stopEmulator() or at the latest in setupEmulator() above
            backUpSram("at game start") { settingsRepository.isSaveBackupOnGameStartEnabled() }

            val gbaSlotRomConfig = rom.config.gbaSlotConfig
            val gbaSlotType = when (gbaSlotRomConfig) {
                RomGbaSlotConfig.None -> MelonEmulator.GbaSlotType.NONE
                is RomGbaSlotConfig.GbaRom -> MelonEmulator.GbaSlotType.GBA_ROM
                RomGbaSlotConfig.MemoryExpansion -> MelonEmulator.GbaSlotType.MEMORY_EXPANSION
                RomGbaSlotConfig.RumblePak -> MelonEmulator.GbaSlotType.RUMBLE_PAK
                RomGbaSlotConfig.MotionPakHomebrew -> MelonEmulator.GbaSlotType.MOTION_PAK_HOMEBREW
                RomGbaSlotConfig.MotionPakRetail -> MelonEmulator.GbaSlotType.MOTION_PAK_RETAIL
            }

            val loadResult = MelonEmulator.loadRom(
                romUri = romUri,
                sramUri = sram.sramUri,
                gbaSlotType = gbaSlotType,
                gbaRomUri = (gbaSlotRomConfig as? RomGbaSlotConfig.GbaRom)?.romPath,
                gbaSramUri = (gbaSlotRomConfig as? RomGbaSlotConfig.GbaRom)?.savePath
            )
            DebugLog.event { "ROM load result: ${loadResult.name}" }
            if (loadResult.isTerminal || !isActive) {
                cameraManager.stopCurrentCameraSource()
                MelonEmulator.stopEmulation()
                endSramSession()
                RomLaunchResult.LaunchFailed(loadResult)
            } else {
                messageQueue.start()
                MelonEmulator.setupCheats(cheats.toTypedArray())
                setupLuaScripts(luaScripts, luaScriptFolder, areLuaStateLoadsAllowed) {
                    LuaScriptDataStore.getGameKey(fileRomProcessor.getRomInfo(rom)?.gameCode, rom.uri.toString())
                }
                MelonEmulator.startEmulation()

                RomLaunchResult.LaunchSuccessful(loadResult != MelonEmulator.LoadResult.SUCCESS_GBA_FAILED)
            }
        }
    }

    override suspend fun loadFirmware(consoleType: ConsoleType): FirmwareLaunchResult {
        endSramSession()
        return withContext(Dispatchers.IO) {
            val configuration = getFirmwareEmulatorConfiguration(consoleType)
            DebugLog.logConfiguration("firmware launch", null, configuration, null, settingsRepository)
            setupEmulator(configuration)
            val result = MelonEmulator.bootFirmware()
            DebugLog.event { "Firmware boot result: ${result.name}" }
            if (result != MelonEmulator.FirmwareLoadResult.SUCCESS) {
                cameraManager.stopCurrentCameraSource()
                MelonEmulator.stopEmulation()
                FirmwareLaunchResult.LaunchFailed(result)
            } else {
                messageQueue.start()
                clearLuaScripts()
                MelonEmulator.startEmulation()
                FirmwareLaunchResult.LaunchSuccessful
            }
        }
    }

    override fun setWideView(width: Int, margin: Int) {
        romWideViewWidth = width
        romWideViewMargin = margin
        MelonEmulator.setWideView(width, margin)
        DebugLog.event { "Wide view width: $width margin: $margin" }
    }

    override suspend fun updateRomEmulatorConfiguration(rom: Rom) {
        val configuration = getRomEmulatorConfiguration(rom)
        DebugLog.logConfiguration("configuration update", rom, configuration, null, settingsRepository)
        MelonEmulator.updateEmulatorConfiguration(configuration)
    }

    override suspend fun updateFirmwareEmulatorConfiguration(consoleType: ConsoleType) {
        val configuration = getFirmwareEmulatorConfiguration(consoleType)
        DebugLog.logConfiguration("configuration update", null, configuration, null, settingsRepository)
        MelonEmulator.updateEmulatorConfiguration(configuration)
    }

    // Off the main thread: it waits for the emulator thread to park if a pause was just asked for, which can take a long frame
    override suspend fun getRewindWindow(): RewindWindow = withContext(Dispatchers.IO) {
        MelonEmulator.getRewindWindow()
    }

    override fun areLuaScriptsRunning(): Boolean {
        return luaScriptsState.runningIds.isNotEmpty()
    }

    override fun getLuaScripts(): List<LuaScriptStatus> {
        return luaScriptsState.toStatuses()
    }

    override fun getLuaScript(scriptId: Int): LuaScriptStatus? {
        return luaScriptsState.getStatus(scriptId)
    }

    override fun disableLuaScript(scriptId: Int) {
        synchronized(luaScriptsLock) {
            val state = luaScriptsState
            if (scriptId !in state.runningIds) {
                return
            }
            luaScriptsState = state.copy(runningIds = state.runningIds - scriptId)
        }
        DebugLog.event { "Lua script ${luaScriptsState.names.getOrNull(scriptId)} switched off by the player" }
        MelonEmulator.disableLuaScript(scriptId)
    }

    override fun enableLuaScript(scriptId: Int): Boolean {
        synchronized(luaScriptsLock) {
            val state = luaScriptsState
            // A script that couldn't be read at launch has nothing to run
            if (scriptId !in 0 until state.loadedCount || scriptId in state.runningIds) {
                return false
            }
            // If loading it fails, the stop event switches it off again with the new error
            luaScriptsState = state.copy(runningIds = state.runningIds + scriptId, errors = state.errors - scriptId)
        }
        DebugLog.event { "Lua script ${luaScriptsState.names.getOrNull(scriptId)} switched on by the player" }
        MelonEmulator.enableLuaScript(scriptId)
        return true
    }

    override fun restartLuaScript(scriptId: Int, optionValues: Map<String, String>): Boolean {
        val nativeOptions = synchronized(luaScriptsLock) {
            val state = luaScriptsState
            if (scriptId !in 0 until state.loadedCount) {
                return false
            }
            val options = state.headers.getOrNull(scriptId)?.options.orEmpty()
            val resolvedValues = options.resolveValues(optionValues)
            // If loading it fails, the stop event switches it off again with the new error
            luaScriptsState = state.copy(
                optionValues = state.optionValues.mapIndexed { id, values -> if (id == scriptId) resolvedValues else values },
                runningIds = state.runningIds + scriptId,
                errors = state.errors - scriptId,
            )
            getNativeOptionValues(options, resolvedValues)
        }
        DebugLog.event { "Lua script ${luaScriptsState.names.getOrNull(scriptId)} restarted with new options by the player" }
        MelonEmulator.restartLuaScript(scriptId, nativeOptions)
        return true
    }

    override fun getFps(): Float {
        return MelonEmulator.getFPS()
    }

    override suspend fun pauseEmulator() {
        DebugLog.event { "Pause" }
        MelonEmulator.pauseEmulation()
    }

    override suspend fun resumeEmulator() {
        DebugLog.event { "Resume" }
        MelonEmulator.resumeEmulation()
    }

    override suspend fun resetEmulator() {
        DebugLog.event { "Reset" }
        MelonEmulator.resetEmulation()
    }

    override suspend fun updateCheats(cheats: List<Cheat>) {
        DebugLog.event { "Cheats updated: ${cheats.size} active" }
        MelonEmulator.setupCheats(cheats.toTypedArray())
    }

    override suspend fun setupRetroAchievements(achievementData: GameAchievementData) {
        val richPresencePath = if (settingsRepository.isRetroAchievementsRichPresenceEnabled()) {
            achievementData.richPresencePatch
        } else {
            null
        }

        MelonEmulator.setupAchievements(
            achievements = achievementData.lockedAchievements.toTypedArray(),
            leaderboards = achievementData.leaderboards.toTypedArray(),
            richPresenceScript = richPresencePath,
        )
    }

    override fun unloadRetroAchievementsData() {
        MelonEmulator.unloadRetroAchievementsData()
    }

    override suspend fun loadRewindState(rewindSaveState: RewindSaveState): Boolean {
        return MelonEmulator.loadRewindState(rewindSaveState).also {
            DebugLog.event { "Rewind state loaded (success=$it)" }
        }
    }

    override suspend fun saveState(saveStateFileUri: Uri): Boolean = withContext(Dispatchers.IO) {
        MelonEmulator.saveState(saveStateFileUri).also {
            DebugLog.event { "Save state written to ${getSaveStateName(saveStateFileUri)} (success=$it)" }
        }
    }

    override suspend fun loadState(saveStateFileUri: Uri): Boolean = withContext(Dispatchers.IO) {
        val success = MelonEmulator.loadState(saveStateFileUri).also {
            DebugLog.event { "Save state loaded from ${getSaveStateName(saveStateFileUri)} (success=$it)" }
        }
        // After the load, not before: the load doesn't write the .sav, and it writes any pending in-game save to disk itself
        // (SaveManager::SyncFromState), so the save writer is idle now and the backup can't catch it halfway through a write. Callers resume
        // the game only after this returns. Done whether or not the load worked
        backUpSram("after a state load") { settingsRepository.isSaveBackupOnStateLoadEnabled() }
        success
    }

    /**
     * Backs up the game's .sav to its save-backups folder, so a state load or a bad session can't cost the player their real save. A failed
     * backup is only logged, so it never changes the launch's or the load's result. [isEnabled] is that moment's setting, and [moment] is for
     * the log.
     */
    private fun backUpSram(moment: String, isEnabled: () -> Boolean) {
        try {
            val session = currentSram ?: return
            if (!isEnabled()) {
                return
            }
            if (saveBackupManager.backUpSave(session.location) == SaveBackupManager.BackupResult.Failed) {
                Log.w(DebugLog.TAG, "Could not back up the save $moment")
            }
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not back up the save $moment", e)
        }
    }

    private fun endSramSession() {
        currentSram?.let { saveBackupManager.markGameStopped(it.runningGame) }
        currentSram = null
    }

    /**
     * The save state's file name, without the folder it's in.
     */
    private fun getSaveStateName(saveStateFileUri: Uri): String {
        return saveStateFileUri.lastPathSegment?.substringAfterLast('/') ?: "?"
    }

    override suspend fun takeScreenshot(): Boolean = withContext(Dispatchers.IO) {
        MelonEmulator.takeScreenshot()
    }

    override fun stopEmulator() {
        DebugLog.event { "Stop" }
        MelonEmulator.stopEmulation()
        endSramSession()
        // Written before this returns, so the ROM details screen shown next sees the newest data and a Clear there can't be undone by a late write
        saveLuaData()
        clearLuaScripts()
        cameraManager.stopCurrentCameraSource()
        messageQueue.stop()
    }

    override fun cleanEmulator() {
        // The running mark stays: this path doesn't stop the native emulator, so stopEmulator() and the next loadRom() clear it
        cameraManager.dispose()
        messageQueue.cleanup()
    }

    override fun observeRetroAchievementEvents(): Flow<RAEvent> {
        return achievementsSharedFlow.asSharedFlow()
    }

    private fun setupEmulator(emulatorConfiguration: EmulatorConfiguration) {
        // Debug builds only: the Debug switches setting, applied when the game's core reads its debug.melonade.* switches
        if (BuildConfig.DEBUG) {
            val debugSwitches = DebugSwitches.parse(settingsRepository.getDebugSwitches())
            MelonEmulator.setDebugSwitches(debugSwitches.toNativeString())
            DebugSwitches.setCurrent(debugSwitches)
            if (debugSwitches.switches.isNotEmpty() || debugSwitches.errors.isNotEmpty()) {
                DebugLog.event {
                    "Debug switches setting: ${debugSwitches.describe().ifEmpty { "none" }}" +
                        if (debugSwitches.errors.isEmpty()) "" else " (ignored: ${debugSwitches.errors.joinToString(", ")})"
                }
            }
        }
        MelonEmulator.setupEmulator(
            emulatorConfiguration = emulatorConfiguration,
            dsiCameraSource = cameraManager,
            screenshotBuffer = screenshotFrameBufferProvider.frameBuffer(),
        )
    }

    /**
     * Reads the enabled scripts from the game's linked folder (see [LuaScriptStorage]) and gives them to the emulator, reporting unreadable ones
     * as stopped.
     */
    private fun setupLuaScripts(luaScripts: List<RomLuaScript>, luaScriptFolder: Uri?, areStateLoadsAllowed: Boolean, getDataGameKey: () -> String?) {
        val enabledScripts = luaScripts.filter { it.enabled }
        if (enabledScripts.isEmpty()) {
            clearLuaScripts()
            return
        }

        val readResults = luaScriptStorage.readScripts(luaScriptFolder, enabledScripts.map { it.fileName })
        val loadedScripts = readResults.mapNotNull { (fileName, result) ->
            (result as? LuaScriptStorage.ReadResult.Success)?.let { fileName to it.source }
        }
        val failedScripts = readResults.mapNotNull { (fileName, result) -> getLuaScriptReadError(result)?.let { fileName to it } }

        DebugLog.event { "Lua scripts: ${loadedScripts.size} of ${enabledScripts.size} enabled loaded" }
        // Declarations are read from the source being sent, so they match the script that runs
        val headers = loadedScripts.map { LuaScriptHeaderParser.parse(it.second) } + failedScripts.map { null }
        val optionValues = loadedScripts.mapIndexed { id, (fileName, _) ->
            val storedValues = enabledScripts.firstOrNull { it.fileName == fileName }?.options.orEmpty()
            headers[id]?.options.orEmpty().resolveValues(storedValues)
        } + failedScripts.map { emptyMap() }
        synchronized(luaScriptsLock) {
            luaScriptsState = LuaScriptsState(
                names = loadedScripts.map { it.first } + failedScripts.map { it.first },
                loadedCount = loadedScripts.size,
                runningIds = loadedScripts.indices.toSet(),
                errors = failedScripts.withIndex().associate { (index, script) -> loadedScripts.size + index to script.second },
                headers = headers,
                optionValues = optionValues,
            )
        }
        if (loadedScripts.isNotEmpty()) {
            // Anything left from the last session is written first, so the data read below is the newest
            saveLuaData()
            // Null if the ROM's header couldn't be read, in which case the scripts' data lives for the session only rather than under a guessed key
            val dataGameKey = getDataGameKey()
            luaScriptDataStore.setSessionGameKey(dataGameKey)
            // Modules and images come from the whole folder tree; without them require() or image.load() say what's missing
            val folderContent = luaScriptFolder?.let { luaScriptStorage.readFolderContent(it) }
            DebugLog.event {
                val modules = folderContent?.modules.orEmpty()
                val images = folderContent?.images.orEmpty()
                "Lua folder content: ${modules.count { it.source != null }} of ${modules.size} modules, ${images.count { it.pixels != null }} of ${images.size} images"
            }
            val scriptSetups = loadedScripts.mapIndexed { id, (fileName, source) ->
                LuaScriptSetup(
                    name = fileName,
                    source = source,
                    savedData = dataGameKey?.let { luaScriptDataStore.read(it, fileName) },
                    dataGameKey = dataGameKey.orEmpty(),
                    options = getNativeOptionValues(headers[id]?.options.orEmpty(), optionValues[id]),
                )
            }
            MelonEmulator.setupLuaScripts(
                scripts = scriptSetups.toTypedArray(),
                modules = folderContent?.modules.orEmpty().toTypedArray(),
                images = folderContent?.images.orEmpty().toTypedArray(),
                areStateLoadsAllowed = areStateLoadsAllowed,
            )
        }
        // Told like a script that fails to load, so the player sees why it isn't running
        failedScripts.forEachIndexed { index, (fileName, error) ->
            _emulatorEvents.tryEmit(EmulatorEvent.LuaScriptStopped(loadedScripts.size + index, fileName, error))
        }
    }

    private fun getNativeOptionValues(options: List<LuaScriptOption>, values: Map<String, String>): Array<LuaScriptOptionValue> {
        return options.map { it.toNativeValue(values[it.key] ?: it.resolve(null)) }.toTypedArray()
    }

    /**
     * Writes the storage snapshots native has waiting, on an IO thread or before a launch reads the saved data.
     */
    private fun saveLuaData() {
        synchronized(luaDataLock) {
            val snapshots = try {
                MelonEmulator.drainLuaData()
            } catch (e: OutOfMemoryError) {
                // Native put the snapshots back, so the next drain tries again
                DebugLog.event { "Lua storage not saved: out of memory" }
                null
            } ?: return
            snapshots.forEach {
                luaScriptDataStore.write(it.gameKey, it.scriptName, it.data)
            }
            if (snapshots.isNotEmpty()) {
                DebugLog.event { "Lua storage saved for ${snapshots.size} script(s)" }
            }
        }
    }

    private fun getLuaScriptReadError(result: LuaScriptStorage.ReadResult): String? {
        val error = when (result) {
            is LuaScriptStorage.ReadResult.Success -> return null
            LuaScriptStorage.ReadResult.FolderNotFound -> R.string.lua_script_error_folder_not_found
            LuaScriptStorage.ReadResult.FileNotFound -> R.string.lua_script_error_file_not_found
            LuaScriptStorage.ReadResult.TooLarge -> R.string.lua_script_error_too_large
            LuaScriptStorage.ReadResult.Failed -> R.string.lua_script_error_unreadable
        }
        return context.getString(error)
    }

    private fun clearLuaScripts() {
        synchronized(luaScriptsLock) {
            luaScriptsState = LuaScriptsState.EMPTY
        }
        luaScriptDataStore.setSessionGameKey(null)
    }

    private suspend fun getRomEmulatorConfiguration(rom: Rom): EmulatorConfiguration {
        val baseConfiguration = settingsRepository.getEmulatorConfiguration()
        val mustUseCustomBios = baseConfiguration.useCustomBios || rom.config.runtimeConsoleType != RuntimeConsoleType.DEFAULT
        return baseConfiguration.copy(
            useCustomBios = mustUseCustomBios,
            showBootScreen = baseConfiguration.showBootScreen && mustUseCustomBios,
            consoleType = getRomOptionOrDefault(rom.config.runtimeConsoleType, baseConfiguration.consoleType),
            micSource = getRomOptionOrDefault(rom.config.runtimeMicSource, baseConfiguration.micSource),
            wideViewWidth = romWideViewWidth,
            wideViewMargin = romWideViewMargin,
        ).run { getPermissionAdjustedConfiguration(this) }
    }

    private suspend fun getFirmwareEmulatorConfiguration(consoleType: ConsoleType): EmulatorConfiguration {
        return settingsRepository.getEmulatorConfiguration().copy(
            consoleType = consoleType,
            useCustomBios = true,
            showBootScreen = true,
        ).run { getPermissionAdjustedConfiguration(this) }
    }

    private fun <T, U> getRomOptionOrDefault(romOption: T, default: U): U where T : RuntimeEnum<T, U> {
        return if (romOption.getDefault() == romOption) {
            default
        } else {
            romOption.getValue()
        }
    }

    private suspend fun getPermissionAdjustedConfiguration(originalConfiguration: EmulatorConfiguration): EmulatorConfiguration {
        if (originalConfiguration.micSource == MicSource.DEVICE) {
            if (!permissionHandler.checkPermission(android.Manifest.permission.RECORD_AUDIO)) {
                return originalConfiguration.copy(micSource = MicSource.NONE)
            }
        }

        return originalConfiguration
    }

    private fun getStopReason(internalReason: Int): EmulatorEvent.Stop.Reason? {
        return when (internalReason) {
            GBAModeNotSupported -> EmulatorEvent.Stop.Reason.GBAModeNotSupported
            BadExceptionRegion -> EmulatorEvent.Stop.Reason.BadExceptionRegion
            PowerOff -> EmulatorEvent.Stop.Reason.PowerOff
            else -> null
        }
    }

    /**
     * The session's Lua scripts: first the ones sent at launch (their position is the ID native uses), then the ones that couldn't be read,
     * which native never sees
     */
    private data class LuaScriptsState(
        val names: List<String>,
        // How many of names were sent to native
        val loadedCount: Int,
        val runningIds: Set<Int>,
        // The errors that switched scripts off, by ID
        val errors: Map<Int, String>,
        // From each script's header, read from the source sent at launch, so it matches the script that is running
        val headers: List<LuaScriptHeader?>,
        // The option values each script runs with: set at launch and changed by restartLuaScript()
        val optionValues: List<Map<String, String>>,
    ) {
        fun toStatuses(): List<LuaScriptStatus> {
            return names.mapIndexed { id, name -> getStatus(id, name) }
        }

        fun getStatus(id: Int): LuaScriptStatus? {
            val name = names.getOrNull(id) ?: return null
            return getStatus(id, name)
        }

        private fun getStatus(id: Int, name: String): LuaScriptStatus {
            return LuaScriptStatus(
                name = name,
                description = headers.getOrNull(id)?.description,
                isRunning = id in runningIds,
                lastError = errors[id],
                options = headers.getOrNull(id)?.options.orEmpty(),
                optionValues = optionValues.getOrNull(id).orEmpty(),
            )
        }

        companion object {
            val EMPTY = LuaScriptsState(emptyList(), 0, emptySet(), emptyMap(), emptyList(), emptyList())
        }
    }

    /**
     * The values of the different stop reasons, as defined in the emulator's core (in `melonDS::Platform::StopReason`)
     */
    companion object InternalStopReason {
        private const val GBAModeNotSupported = 2
        private const val BadExceptionRegion = 3
        private const val PowerOff = 4
    }
}