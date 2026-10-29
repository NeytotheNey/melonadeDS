package me.magnum.melonds.impl.emulator

import android.content.Context
import android.net.Uri
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
import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.domain.model.emulator.LuaScriptOptionValue
import me.magnum.melonds.domain.model.emulator.LuaScriptSetup
import me.magnum.melonds.domain.model.emulator.LuaScriptStatus
import me.magnum.melonds.domain.model.emulator.RomLaunchResult
import me.magnum.melonds.domain.model.emulator.resolveValues
import me.magnum.melonds.domain.model.retroachievements.GameAchievementData
import me.magnum.melonds.domain.model.retroachievements.RAEvent
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeEnum
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.domain.services.EmulatorManager
import me.magnum.melonds.impl.LuaScriptDataStore
import me.magnum.melonds.impl.LuaScriptHeader
import me.magnum.melonds.impl.LuaScriptStorage
import me.magnum.melonds.impl.camera.DSiCameraSourceMultiplexer
import me.magnum.melonds.impl.debug.DebugLog
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow

class AndroidEmulatorManager(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
    private val sramProvider: SramProvider,
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

    // Names of the session's Lua scripts: first the ones sent at launch (their position is the ID native uses), then the ones that couldn't be
    // read, which native never sees. Then the IDs of the ones still running, and the errors that switched scripts off. Replaced at launch. The
    // running set is shrunk by the message queue thread when an error switches a script off, and changed by the UI thread when the player
    // switches one off or on, so changes to it go through luaScriptsLock
    private val luaScriptsLock = Any()
    @Volatile
    private var luaScriptNames = emptyList<String>()
    // How many of luaScriptNames were sent to native
    @Volatile
    private var loadedLuaScriptCount = 0
    @Volatile
    private var runningLuaScripts = emptySet<Int>()
    @Volatile
    private var luaScriptErrors = emptyMap<Int, String>()
    // From each script's header, read from the source sent at launch, so it matches the script that is running
    @Volatile
    private var luaScriptHeaders = emptyList<LuaScriptHeader?>()
    // The option values each script runs with: set at launch and changed by restartLuaScript()
    @Volatile
    private var luaScriptOptionValues = emptyList<Map<String, String>>()

    // Writes the scripts' storage snapshots. Snapshots are drained and written under luaDataLock, so an older one can't overwrite a newer one
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
                    runningLuaScripts = runningLuaScripts - scriptId
                    luaScriptErrors = luaScriptErrors + (scriptId to error)
                }
                val scriptName = luaScriptNames.getOrNull(scriptId) ?: "?"
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
        }
    }

    override suspend fun loadRom(rom: Rom, cheats: List<Cheat>, luaScripts: List<RomLuaScript>, luaScriptFolder: Uri?): RomLaunchResult {
        return withContext(Dispatchers.IO) {
            val fileRomDocument = DocumentFile.fromSingleUri(context, rom.uri) ?: return@withContext RomLaunchResult.LaunchFailedRomNotFound
            val fileRomProcessor = romFileProcessorFactory.getFileRomProcessorForDocument(fileRomDocument)
            val romUri = fileRomProcessor?.getRealRomUri(rom) ?: return@withContext RomLaunchResult.LaunchFailedRomNotSupported

            val configuration = getRomEmulatorConfiguration(rom)
            DebugLog.logConfiguration("rom launch", rom, configuration, cheats.size, settingsRepository)
            setupEmulator(configuration)

            val sram = try {
                sramProvider.getSramForRom(rom)
            } catch (exception: SramLoadException) {
                return@withContext RomLaunchResult.LaunchFailedSramProblem(exception)
            }

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
                sramUri = sram,
                gbaSlotType = gbaSlotType,
                gbaRomUri = (gbaSlotRomConfig as? RomGbaSlotConfig.GbaRom)?.romPath,
                gbaSramUri = (gbaSlotRomConfig as? RomGbaSlotConfig.GbaRom)?.savePath
            )
            DebugLog.event { "ROM load result: ${loadResult.name}" }
            if (loadResult.isTerminal || !isActive) {
                cameraManager.stopCurrentCameraSource()
                MelonEmulator.stopEmulation()
                RomLaunchResult.LaunchFailed(loadResult)
            } else {
                messageQueue.start()
                MelonEmulator.setupCheats(cheats.toTypedArray())
                setupLuaScripts(luaScripts, luaScriptFolder) {
                    LuaScriptDataStore.getGameKey(fileRomProcessor.getRomInfo(rom)?.gameCode, rom.uri.toString())
                }
                MelonEmulator.startEmulation()

                RomLaunchResult.LaunchSuccessful(loadResult != MelonEmulator.LoadResult.SUCCESS_GBA_FAILED)
            }
        }
    }

    override suspend fun loadFirmware(consoleType: ConsoleType): FirmwareLaunchResult {
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

    override suspend fun getRewindWindow(): RewindWindow {
        return MelonEmulator.getRewindWindow()
    }

    override fun areLuaScriptsRunning(): Boolean {
        return runningLuaScripts.isNotEmpty()
    }

    override fun getLuaScripts(): List<LuaScriptStatus> {
        synchronized(luaScriptsLock) {
            return luaScriptNames.mapIndexed { id, name ->
                LuaScriptStatus(
                    name = name,
                    description = luaScriptHeaders.getOrNull(id)?.description,
                    isRunning = id in runningLuaScripts,
                    lastError = luaScriptErrors[id],
                    options = luaScriptHeaders.getOrNull(id)?.options.orEmpty(),
                    optionValues = luaScriptOptionValues.getOrNull(id).orEmpty(),
                )
            }
        }
    }

    override fun disableLuaScript(scriptId: Int) {
        synchronized(luaScriptsLock) {
            if (scriptId !in runningLuaScripts) {
                return
            }
            runningLuaScripts = runningLuaScripts - scriptId
        }
        DebugLog.event { "Lua script ${luaScriptNames.getOrNull(scriptId)} switched off by the player" }
        MelonEmulator.disableLuaScript(scriptId)
    }

    override fun enableLuaScript(scriptId: Int): Boolean {
        synchronized(luaScriptsLock) {
            // A script that couldn't be read at launch has nothing to run
            if (scriptId !in 0 until loadedLuaScriptCount || scriptId in runningLuaScripts) {
                return false
            }
            // If loading it fails, the stop event switches it off again with the new error
            runningLuaScripts = runningLuaScripts + scriptId
            luaScriptErrors = luaScriptErrors - scriptId
        }
        DebugLog.event { "Lua script ${luaScriptNames.getOrNull(scriptId)} switched on by the player" }
        MelonEmulator.enableLuaScript(scriptId)
        return true
    }

    override fun restartLuaScript(scriptId: Int, optionValues: Map<String, String>): Boolean {
        val nativeOptions = synchronized(luaScriptsLock) {
            if (scriptId !in 0 until loadedLuaScriptCount) {
                return false
            }
            val options = luaScriptHeaders.getOrNull(scriptId)?.options.orEmpty()
            val resolvedValues = options.resolveValues(optionValues)
            luaScriptOptionValues = luaScriptOptionValues.mapIndexed { id, values -> if (id == scriptId) resolvedValues else values }
            // If loading it fails, the stop event switches it off again with the new error
            runningLuaScripts = runningLuaScripts + scriptId
            luaScriptErrors = luaScriptErrors - scriptId
            getNativeOptionValues(options, resolvedValues)
        }
        DebugLog.event { "Lua script ${luaScriptNames.getOrNull(scriptId)} restarted with new options by the player" }
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
        MelonEmulator.loadState(saveStateFileUri).also {
            DebugLog.event { "Save state loaded from ${getSaveStateName(saveStateFileUri)} (success=$it)" }
        }
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
        // The scripts' last storage snapshots were made as the emulator stopped. They're written before this returns (they're small), so the
        // ROM details screen shown next sees the newest data, and a Clear there can't be undone by a late write
        saveLuaData()
        clearLuaScripts()
        cameraManager.stopCurrentCameraSource()
        messageQueue.stop()
    }

    override fun cleanEmulator() {
        cameraManager.dispose()
        messageQueue.cleanup()
    }

    override fun observeRetroAchievementEvents(): Flow<RAEvent> {
        return achievementsSharedFlow.asSharedFlow()
    }

    private fun setupEmulator(emulatorConfiguration: EmulatorConfiguration) {
        MelonEmulator.setupEmulator(
            emulatorConfiguration = emulatorConfiguration,
            dsiCameraSource = cameraManager,
            screenshotBuffer = screenshotFrameBufferProvider.frameBuffer(),
        )
    }

    /**
     * Reads the enabled scripts from the game's linked folder (see [LuaScriptStorage]) and gives them to the emulator. A script that can't be
     * read is listed as switched off with the reason as its error, the same as a script that fails to load
     */
    private fun setupLuaScripts(luaScripts: List<RomLuaScript>, luaScriptFolder: Uri?, getDataGameKey: () -> String?) {
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
        synchronized(luaScriptsLock) {
            luaScriptNames = loadedScripts.map { it.first } + failedScripts.map { it.first }
            loadedLuaScriptCount = loadedScripts.size
            runningLuaScripts = loadedScripts.indices.toSet()
            luaScriptErrors = failedScripts.withIndex().associate { (index, script) -> loadedScripts.size + index to script.second }
            luaScriptHeaders = loadedScripts.map { LuaScriptHeader.parse(it.second) } + failedScripts.map { null }
            // Declarations are read from the source being sent, so they match the script that runs
            luaScriptOptionValues = loadedScripts.mapIndexed { id, (fileName, _) ->
                val storedValues = enabledScripts.firstOrNull { it.fileName == fileName }?.options.orEmpty()
                luaScriptHeaders[id]?.options.orEmpty().resolveValues(storedValues)
            } + failedScripts.map { emptyMap() }
        }
        if (loadedScripts.isNotEmpty()) {
            // Anything left from the last session is written first, so the data read below is the newest
            saveLuaData()
            // Null if the ROM's header couldn't be read. The scripts then start with no data and keep it for the session only (native doesn't
            // save without a key), rather than loading and saving under a guessed key
            val dataGameKey = getDataGameKey()
            luaScriptDataStore.setSessionGameKey(dataGameKey)
            // Modules and images come from the whole folder tree. Without them the scripts still run, and require() or image.load() say
            // what's missing
            val folderContent = luaScriptFolder?.let { luaScriptStorage.readFolderContent(it) }
            DebugLog.event {
                val modules = folderContent?.modules.orEmpty()
                val images = folderContent?.images.orEmpty()
                "Lua folder content: ${modules.count { it.source != null }} of ${modules.size} modules, ${images.count { it.pixels != null }} of ${images.size} images"
            }
            val headers = luaScriptHeaders
            val optionValues = luaScriptOptionValues
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
     * Writes the storage snapshots native has waiting. Called on an IO thread when native says there are some, after the emulator stopped,
     * and before a launch reads the saved data
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
            luaScriptNames = emptyList()
            loadedLuaScriptCount = 0
            runningLuaScripts = emptySet()
            luaScriptErrors = emptyMap()
            luaScriptHeaders = emptyList()
            luaScriptOptionValues = emptyList()
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
            micSource = getRomOptionOrDefault(rom.config.runtimeMicSource, baseConfiguration.micSource)
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
     * The values of the different stop reasons, as defined in the emulator's core (in `melonDS::Platform::StopReason`)
     */
    companion object InternalStopReason {
        private const val GBAModeNotSupported = 2
        private const val BadExceptionRegion = 3
        private const val PowerOff = 4
    }
}