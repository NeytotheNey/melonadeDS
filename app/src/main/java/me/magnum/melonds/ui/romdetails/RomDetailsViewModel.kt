package me.magnum.melonds.ui.romdetails

import android.net.Uri
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.common.Permission
import me.magnum.melonds.common.UriPermissionManager
import me.magnum.melonds.common.romprocessors.RomFileProcessorFactory
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.domain.model.emulator.LuaLayoutChoice
import me.magnum.melonds.domain.model.emulator.toLuaLayoutChoices
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.repositories.LayoutsRepository
import me.magnum.melonds.domain.repositories.RetroAchievementsRepository
import me.magnum.melonds.domain.repositories.RomsRepository
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.LuaScriptDataStore
import me.magnum.melonds.impl.LuaScriptFolderSync
import me.magnum.melonds.impl.LuaScriptHeader
import me.magnum.melonds.impl.LuaScriptStorage
import me.magnum.melonds.impl.RomIconProvider
import me.magnum.melonds.parcelables.RomParcelable
import me.magnum.melonds.ui.romdetails.model.LuaScriptEvent
import me.magnum.melonds.ui.romdetails.model.RomConfigUiState
import me.magnum.melonds.ui.romdetails.model.RomConfigUpdateEvent
import me.magnum.melonds.ui.romdetails.model.RomGbaSlotConfigUiModel
import me.magnum.melonds.ui.romlist.RomIcon
import javax.inject.Inject

@HiltViewModel
class RomDetailsViewModel @Inject constructor(
    private val romDetailsUiMapper: RomDetailsUiMapper,
    private val romsRepository: RomsRepository,
    private val settingsRepository: SettingsRepository,
    private val romIconProvider: RomIconProvider,
    private val uriPermissionManager: UriPermissionManager,
    private val retroAchievementsRepository: RetroAchievementsRepository,
    private val luaScriptStorage: LuaScriptStorage,
    private val luaScriptDataStore: LuaScriptDataStore,
    private val romFileProcessorFactory: RomFileProcessorFactory,
    private val layoutsRepository: LayoutsRepository,
    private val savedStateHandle: SavedStateHandle,
) : ViewModel() {

    private val _rom = MutableStateFlow(savedStateHandle.get<RomParcelable>(RomDetailsActivity.KEY_ROM)!!.rom)
    val rom = _rom.asStateFlow()

    private val _romConfig = MutableStateFlow(_rom.value.config)

    // What the last read of the linked script folder found. Null until it has been read
    private val _luaScriptFolderState = MutableStateFlow<LuaScriptFolderState?>(null)
    // Same rule as EmulatorSession: hardcore only applies while logged in to RetroAchievements
    private val _areLuaScriptsBlockedByHardcore = MutableStateFlow(false)
    private var luaScriptSyncJob: Job? = null
    private var romReloadJob: Job? = null
    // How much each script has saved with melon.storage, by file name
    private val _luaScriptDataSizes = MutableStateFlow<Map<String, Long>>(emptyMap())
    // Where the game's script data is kept (see LuaScriptDataStore.getGameKey). Read from the ROM's header the first time it's needed, and
    // again next time if the header couldn't be read
    private var luaDataGameKey: String? = null

    // Buffered until the screen collects it, since a picker result can arrive before that (after the process was killed, for example)
    private val _luaScriptEvent = Channel<LuaScriptEvent>(Channel.UNLIMITED)
    val luaScriptEvent = _luaScriptEvent.receiveAsFlow()

    /**
     * @param folderUri The folder that was read. The rest only applies while the ROM is still linked to it
     * @param isReadable False if the folder couldn't be read, in which case the script list was kept as it was
     * @param headers Each script's header, by file name
     */
    private data class LuaScriptFolderState(val folderUri: Uri, val isReadable: Boolean, val headers: Map<String, LuaScriptHeader>)

    val romConfigUiState by lazy {
        val uiStateFlow = MutableStateFlow<RomConfigUiState>(RomConfigUiState.Loading)
        viewModelScope.launch {
            // For the scripts' layout options. Null until read, so the rest doesn't wait for them
            val luaLayoutChoices = layoutsRepository.getLayouts()
                .map<List<LayoutConfiguration>, List<LuaLayoutChoice>?> { it.toLuaLayoutChoices() }
                .onStart { emit(null) }
            // For the widescreen section. combine() takes up to 5 flows, so the renderer comes with the config
            val romConfigAndRenderer = combine(_romConfig, settingsRepository.getVideoRenderer()) { romConfig, renderer -> romConfig to renderer }
            combine(romConfigAndRenderer, _luaScriptFolderState, _areLuaScriptsBlockedByHardcore, _luaScriptDataSizes, luaLayoutChoices) { (romConfig, renderer), folderState, areLuaScriptsBlocked, dataSizes, layouts ->
                val romConfigUiModel = romDetailsUiMapper.mapRomConfigToUi(romConfig)
                // What was read from the previous folder says nothing about the new one
                val currentFolderState = folderState?.takeIf { it.folderUri == romConfig.mods.luaScriptFolder }
                romConfigUiModel.copy(
                    isWidescreenSupported = renderer == VideoRenderer.OPENGL,
                    luaScripts = romConfigUiModel.luaScripts.copy(
                        isFolderMissing = currentFolderState?.isReadable == false,
                        isFolderEmpty = currentFolderState?.isReadable == true && romConfig.mods.luaScripts.isEmpty(),
                        areScriptsBlockedByHardcore = areLuaScriptsBlocked,
                        headers = currentFolderState?.headers.orEmpty(),
                        dataSizes = dataSizes,
                        layouts = layouts,
                    ),
                )
            }.collect {
                uiStateFlow.value = RomConfigUiState.Ready(it)
            }
        }

        uiStateFlow.asStateFlow()
    }

    /**
     * Reads the hardcore setting again. Called whenever the screen resumes, since the setting can change in between
     */
    fun refreshLuaScriptsHardcoreState() {
        viewModelScope.launch {
            _areLuaScriptsBlockedByHardcore.value = settingsRepository.isRetroAchievementsHardcoreEnabled() && retroAchievementsRepository.isUserAuthenticated()
        }
    }

    fun onRomConfigUpdateEvent(event: RomConfigUpdateEvent) {
        val currentRomConfig = _romConfig.value
        val newRomConfig = when(event) {
            is RomConfigUpdateEvent.RuntimeConsoleUpdate -> currentRomConfig.copy(runtimeConsoleType = event.newRuntimeConsole)
            is RomConfigUpdateEvent.RuntimeMicSourceUpdate -> currentRomConfig.copy(runtimeMicSource = event.newRuntimeMicSource)
            is RomConfigUpdateEvent.LayoutUpdate -> currentRomConfig.copy(layoutId = event.newLayoutId)
            is RomConfigUpdateEvent.GbaSlotTypeUpdated -> currentRomConfig.let {
                val newGbaSlotConfig = when (event.type) {
                    RomGbaSlotConfigUiModel.Type.None -> RomGbaSlotConfig.None
                    RomGbaSlotConfigUiModel.Type.GbaRom -> RomGbaSlotConfig.GbaRom(null, null)
                    RomGbaSlotConfigUiModel.Type.RumblePak -> RomGbaSlotConfig.RumblePak
                    RomGbaSlotConfigUiModel.Type.MemoryExpansion -> RomGbaSlotConfig.MemoryExpansion
                    RomGbaSlotConfigUiModel.Type.MotionPakHomebrew -> RomGbaSlotConfig.MotionPakHomebrew
                    RomGbaSlotConfigUiModel.Type.MotionPakRetail -> RomGbaSlotConfig.MotionPakRetail
                }
                it.copy(gbaSlotConfig = newGbaSlotConfig)
            }
            // The file permissions are only taken here, when a file is picked. Taking them on every save would throw if a grant was lost
            // (after a settings restore, for example), and the script sync saves on its own when the screen resumes
            is RomConfigUpdateEvent.GbaRomPathUpdate -> currentRomConfig.let {
                (currentRomConfig.gbaSlotConfig as? RomGbaSlotConfig.GbaRom)?.let { gbaConfig ->
                    event.gbaRomPath?.let { uri -> uriPermissionManager.persistFilePermissions(uri, Permission.READ) }
                    it.copy(gbaSlotConfig = gbaConfig.copy(romPath = event.gbaRomPath))
                }
            }
            is RomConfigUpdateEvent.GbaSavePathUpdate -> currentRomConfig.let {
                (currentRomConfig.gbaSlotConfig as? RomGbaSlotConfig.GbaRom)?.let { gbaConfig ->
                    event.gbaSavePath?.let { uri -> uriPermissionManager.persistFilePermissions(uri, Permission.READ_WRITE) }
                    it.copy(gbaSlotConfig = gbaConfig.copy(savePath = event.gbaSavePath))
                }
            }
            is RomConfigUpdateEvent.CustomNameUpdate -> currentRomConfig.copy(customName = event.customName)
            is RomConfigUpdateEvent.WideViewWidthUpdate -> currentRomConfig.copy(mods = currentRomConfig.mods.copy(wideViewWidth = event.newWideViewWidth))
            is RomConfigUpdateEvent.LuaScriptToggle -> currentRomConfig.withLuaScripts { luaScripts ->
                luaScripts.map { if (it.fileName == event.fileName) it.copy(enabled = event.enabled) else it }
            }
            is RomConfigUpdateEvent.LuaScriptOptionUpdate -> currentRomConfig.withLuaScripts { luaScripts ->
                luaScripts.map { if (it.fileName == event.fileName) it.copy(options = it.options + (event.key to event.value)) else it }
            }
            is RomConfigUpdateEvent.LuaScriptFolderUpdate -> {
                updateLuaScriptFolder(event.folderUri)
                null
            }
            is RomConfigUpdateEvent.LuaScriptDataClear -> {
                clearLuaScriptData(event.fileName)
                null
            }
        }

        newRomConfig?.let { applyRomConfig(it) }
    }

    /**
     * Reads the ROM again from the repository. Called whenever the screen resumes: the emulator runs on top of this screen and can change the
     * config meanwhile (the pause menu saves script options), and without this the next change made here would write the old config back
     */
    fun reloadRom() {
        romReloadJob?.cancel()
        romReloadJob = viewModelScope.launch {
            val romUri = _rom.value.uri
            val repositoryRom = try {
                romsRepository.getRoms().first().find { it.uri == romUri }
            } catch (e: Exception) {
                null
            } ?: return@launch

            // A copy, since the repository changes its ROMs in place. Changes made here are saved to the repository right away, so its config
            // is never older than this one
            val reloadedRom = repositoryRom.copy()
            if (reloadedRom != _rom.value) {
                _rom.value = reloadedRom
                _romConfig.value = reloadedRom.config
                savedStateHandle[RomDetailsActivity.KEY_ROM] = RomParcelable(reloadedRom)
            }
        }
    }

    suspend fun getRomIcon(rom: Rom): RomIcon {
        val romIconBitmap = romIconProvider.getRomIcon(rom)
        val iconFiltering = settingsRepository.getRomIconFiltering()
        return RomIcon(romIconBitmap, iconFiltering)
    }

    /**
     * Brings the script list in step with the linked folder (see [LuaScriptFolderSync]), and reads the scripts' headers and how much data
     * they saved. Called when the Mods tab is shown and when the screen resumes, so files added to or removed from the folder outside the app
     * show up
     */
    fun syncLuaScripts() {
        luaScriptSyncJob?.cancel()
        val pendingRomReload = romReloadJob
        luaScriptSyncJob = viewModelScope.launch {
            // Synced against the newest config, or the sync would save the old one back
            pendingRomReload?.join()
            val folderUri = _romConfig.value.mods.luaScriptFolder
            val (scriptFiles, headers) = withContext(Dispatchers.IO) {
                val scriptFiles = folderUri?.let { luaScriptStorage.listScripts(it) }
                val headers = scriptFiles.orEmpty().mapNotNull { scriptFile ->
                    luaScriptStorage.readHeader(scriptFile)?.let { scriptFile.name to it }
                }.toMap()
                scriptFiles to headers
            }

            // Applied to the config as it is now, so switches flipped while the folder was read are kept. If the folder was changed in the
            // meantime, the sync started for the new one takes over
            val romConfig = _romConfig.value
            if (romConfig.mods.luaScriptFolder != folderUri) {
                return@launch
            }
            val syncedScripts = LuaScriptFolderSync.sync(romConfig.mods.luaScripts, folderUri != null, scriptFiles?.map { it.name })
            if (syncedScripts != romConfig.mods.luaScripts) {
                applyRomConfig(romConfig.withLuaScripts { syncedScripts })
            }
            // When the folder can't be read, the headers read from it before are kept for as long as the screen is open
            val shownHeaders = if (scriptFiles == null) {
                _luaScriptFolderState.value?.takeIf { it.folderUri == folderUri }?.headers.orEmpty()
            } else {
                headers
            }
            _luaScriptFolderState.value = folderUri?.let { LuaScriptFolderState(it, isReadable = scriptFiles != null, headers = shownHeaders) }
            refreshLuaScriptDataSizes()
        }
    }

    private suspend fun refreshLuaScriptDataSizes() {
        val scriptNames = _romConfig.value.mods.luaScripts.map { it.fileName }
        _luaScriptDataSizes.value = withContext(Dispatchers.IO) {
            // Without a key nothing is shown, rather than the data of a guessed one
            val gameKey = getLuaDataGameKey() ?: return@withContext emptyMap()
            scriptNames.associateWith { luaScriptDataStore.getSize(gameKey, it) }.filterValues { it > 0 }
        }
    }

    private fun clearLuaScriptData(fileName: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                getLuaDataGameKey()?.let { luaScriptDataStore.delete(it, fileName) }
            }
            refreshLuaScriptDataSizes()
        }
    }

    /**
     * Same key as the emulator uses, or null if the ROM's header can't be read (the emulator then keeps the data for the session only). Reads
     * the header, so only on an IO thread
     */
    private fun getLuaDataGameKey(): String? {
        luaDataGameKey?.let { return it }
        val rom = _rom.value
        val gameCode = try {
            romFileProcessorFactory.getFileRomProcessorForDocument(rom.uri)?.getRomInfo(rom)?.gameCode
        } catch (e: Exception) {
            null
        }
        return LuaScriptDataStore.getGameKey(gameCode, rom.uri.toString())?.also { luaDataGameKey = it }
    }

    private fun updateLuaScriptFolder(newFolderUri: Uri?) {
        val oldFolderUri = _romConfig.value.mods.luaScriptFolder
        if (newFolderUri == oldFolderUri) {
            // Picking the same folder again keeps the switches. It's read again, which clears the "can't find the folder" state if access
            // to it was given back
            if (newFolderUri != null) {
                takeLuaScriptFolderPermission(newFolderUri)
            }
            syncLuaScripts()
            return
        }

        if (newFolderUri != null && !takeLuaScriptFolderPermission(newFolderUri)) {
            _luaScriptEvent.trySend(LuaScriptEvent.FolderPickFailed)
            return
        }

        // The switches were for the old folder's scripts, so the list starts over
        val currentRomConfig = _romConfig.value
        applyRomConfig(currentRomConfig.copy(mods = currentRomConfig.mods.copy(luaScriptFolder = newFolderUri, luaScripts = emptyList())))
        oldFolderUri?.let { releaseLuaScriptFolderPermission(it) }
        syncLuaScripts()
    }

    private fun takeLuaScriptFolderPermission(folderUri: Uri): Boolean {
        return try {
            uriPermissionManager.persistDirectoryPermissions(folderUri, Permission.READ)
            true
        } catch (e: SecurityException) {
            false
        }
    }

    /**
     * Gives back the read permission of a folder that was unlinked, unless something else still uses it: another game's script folder, or
     * one of the app's own folders (picking the same folder there gives the same permission, so releasing it would break that one too)
     */
    private fun releaseLuaScriptFolderPermission(folderUri: Uri) {
        val currentRom = _rom.value
        viewModelScope.launch {
            // Finished even if the screen is closed meanwhile, or the permission would never be given back
            withContext(NonCancellable + Dispatchers.IO) {
                val isFolderStillUsed = try {
                    val isUsedByOtherRom = romsRepository.getRoms().first().any {
                        !it.hasSameFileAsRom(currentRom) && it.config.mods.luaScriptFolder == folderUri
                    }
                    isUsedByOtherRom || folderUri in getAppFolders()
                } catch (e: Exception) {
                    // When in doubt the permission is kept. Keeping one too many is harmless, releasing one in use breaks that folder
                    true
                }
                if (!isFolderStillUsed) {
                    uriPermissionManager.releaseDirectoryPermissions(folderUri, Permission.READ)
                }
            }
        }
    }

    private fun getAppFolders(): List<Uri> {
        return settingsRepository.getRomSearchDirectories().toList() + listOfNotNull(
            settingsRepository.getDsBiosDirectory(),
            settingsRepository.getDsiBiosDirectory(),
            settingsRepository.getSaveFileDirectory(),
            settingsRepository.getPerformanceLogDirectory(),
        )
    }

    private fun RomConfig.withLuaScripts(update: (List<RomLuaScript>) -> List<RomLuaScript>): RomConfig {
        return copy(mods = mods.copy(luaScripts = update(mods.luaScripts)))
    }

    private fun applyRomConfig(newConfig: RomConfig) {
        _romConfig.value = newConfig
        _rom.update { it.copy(config = newConfig) }
        // Otherwise the ROM from the intent comes back after the process is killed, and a folder linked before that would look like it was
        // never linked, so its permission wouldn't be given back when it's replaced
        savedStateHandle[RomDetailsActivity.KEY_ROM] = RomParcelable(_rom.value)
        saveRomConfig(newConfig)
    }

    private fun saveRomConfig(newConfig: RomConfig) {
        romsRepository.updateRomConfig(_rom.value, newConfig)
    }
}
