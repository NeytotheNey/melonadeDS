package me.magnum.melonds.ui.romdetails

import android.net.Uri
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.common.Permission
import me.magnum.melonds.common.UriPermissionManager
import me.magnum.melonds.common.romprocessors.RomFileProcessorFactory
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.repositories.RetroAchievementsRepository
import me.magnum.melonds.domain.repositories.RomsRepository
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.LuaScriptStorage
import me.magnum.melonds.impl.RomIconProvider
import me.magnum.melonds.parcelables.RomParcelable
import me.magnum.melonds.ui.romdetails.model.LuaScriptEvent
import me.magnum.melonds.ui.romdetails.model.RomConfigUiState
import me.magnum.melonds.ui.romdetails.model.RomConfigUpdateEvent
import me.magnum.melonds.ui.romdetails.model.RomGbaSlotConfigUiModel
import me.magnum.melonds.ui.romdetails.model.RomLuaScriptsUiModel
import me.magnum.melonds.ui.romlist.RomIcon
import javax.inject.Inject

@HiltViewModel
class RomDetailsViewModel @Inject constructor(
    private val romDetailsUiMapper: RomDetailsUiMapper,
    private val romsRepository: RomsRepository,
    private val settingsRepository: SettingsRepository,
    private val romIconProvider: RomIconProvider,
    private val uriPermissionManager: UriPermissionManager,
    private val romFileProcessorFactory: RomFileProcessorFactory,
    private val retroAchievementsRepository: RetroAchievementsRepository,
    private val luaScriptStorage: LuaScriptStorage,
    savedStateHandle: SavedStateHandle,
) : ViewModel() {

    private val _rom = MutableStateFlow(savedStateHandle.get<RomParcelable>(RomDetailsActivity.KEY_ROM)!!.rom)
    val rom = _rom.asStateFlow()

    private val _romConfig = MutableStateFlow(_rom.value.config)

    // The ROM's script folder, worked out from the ROM header the same way the emulator does. Null until the header has been read
    private val _scriptFolder = MutableStateFlow<String?>(null)
    // Same rule as EmulatorSession: hardcore only applies while logged in to RetroAchievements
    private val _areLuaScriptsBlockedByHardcore = MutableStateFlow(false)
    // Bumped when a script file is added or removed, so missing files are checked again even if the config didn't change
    private val _luaScriptFilesVersion = MutableStateFlow(0)

    // Buffered until the screen collects it, since a picker result can arrive before that (after the process was killed, for example)
    private val _luaScriptEvent = Channel<LuaScriptEvent>(Channel.UNLIMITED)
    val luaScriptEvent = _luaScriptEvent.receiveAsFlow()

    init {
        viewModelScope.launch {
            val rom = _rom.value
            _scriptFolder.value = withContext(Dispatchers.IO) {
                val gameCode = romFileProcessorFactory.getFileRomProcessorForDocument(rom.uri)?.getRomInfo(rom)?.gameCode
                luaScriptStorage.getScriptFolderName(rom, gameCode)
            }
        }
    }

    val romConfigUiState by lazy {
        val uiStateFlow = MutableStateFlow<RomConfigUiState>(RomConfigUiState.Loading)
        viewModelScope.launch {
            combine(_romConfig, _scriptFolder, _areLuaScriptsBlockedByHardcore, _luaScriptFilesVersion) { romConfig, scriptFolder, areLuaScriptsBlocked, _ ->
                romDetailsUiMapper.mapRomConfigToUi(romConfig).copy(
                    luaScripts = getLuaScriptsUiModel(romConfig, scriptFolder).copy(
                        isScriptFolderKnown = scriptFolder != null,
                        areScriptsBlockedByHardcore = areLuaScriptsBlocked,
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
            is RomConfigUpdateEvent.GbaRomPathUpdate -> currentRomConfig.let {
                (currentRomConfig.gbaSlotConfig as? RomGbaSlotConfig.GbaRom)?.let { gbaConfig ->
                    it.copy(gbaSlotConfig = gbaConfig.copy(romPath = event.gbaRomPath))
                }
            }
            is RomConfigUpdateEvent.GbaSavePathUpdate -> currentRomConfig.let {
                (currentRomConfig.gbaSlotConfig as? RomGbaSlotConfig.GbaRom)?.let { gbaConfig ->
                    it.copy(gbaSlotConfig = gbaConfig.copy(savePath = event.gbaSavePath))
                }
            }
            is RomConfigUpdateEvent.CustomNameUpdate -> currentRomConfig.copy(customName = event.customName)
            is RomConfigUpdateEvent.WideViewWidthUpdate -> currentRomConfig.copy(mods = currentRomConfig.mods.copy(wideViewWidth = event.newWideViewWidth))
            is RomConfigUpdateEvent.LuaScriptToggle -> currentRomConfig.withLuaScripts { luaScripts ->
                luaScripts.map { if (it.fileName == event.fileName) it.copy(enabled = event.enabled) else it }
            }
            // These change files first, and update the config once that worked
            is RomConfigUpdateEvent.LuaScriptAdd -> {
                addLuaScript(event.uri, event.replace)
                null
            }
            is RomConfigUpdateEvent.LuaScriptDelete -> {
                deleteLuaScript(event.fileName)
                null
            }
        }

        newRomConfig?.let { applyRomConfig(it) }
    }

    suspend fun getRomIcon(rom: Rom): RomIcon {
        val romIconBitmap = romIconProvider.getRomIcon(rom)
        val iconFiltering = settingsRepository.getRomIconFiltering()
        return RomIcon(romIconBitmap, iconFiltering)
    }

    private fun addLuaScript(uri: Uri, replace: Boolean) {
        viewModelScope.launch {
            // The picker result can arrive before the folder is known, if the process was killed while it was open
            val scriptFolder = _scriptFolder.filterNotNull().first()
            val fileName = withContext(Dispatchers.IO) { luaScriptStorage.getPickedFileName(uri) }
            if (fileName == null) {
                _luaScriptEvent.trySend(LuaScriptEvent.AddFailed)
                return@launch
            }
            // .lua files often have no reliable MIME type, so the picker shows every file and the name is checked here
            if (!luaScriptStorage.isLuaScriptName(fileName)) {
                _luaScriptEvent.trySend(LuaScriptEvent.NotLuaScript)
                return@launch
            }
            // A file left in the folder without a config entry isn't shown anywhere, so only a script in the list needs confirming
            if (!replace && _romConfig.value.mods.luaScripts.any { it.fileName == fileName }) {
                _luaScriptEvent.trySend(LuaScriptEvent.ConfirmReplace(uri, fileName))
                return@launch
            }

            val result = withContext(Dispatchers.IO) { luaScriptStorage.importScript(scriptFolder, uri, fileName) }
            when (result) {
                LuaScriptStorage.ImportResult.Success -> {
                    // Applied to the config as it is now, so changes made while the file was copied are kept. A replaced script keeps its
                    // place and its switch
                    applyRomConfig(_romConfig.value.withLuaScripts { luaScripts ->
                        if (luaScripts.any { it.fileName == fileName }) luaScripts else luaScripts + RomLuaScript(fileName = fileName, enabled = true)
                    })
                    _luaScriptFilesVersion.update { it + 1 }
                }
                LuaScriptStorage.ImportResult.NotLuaScript -> _luaScriptEvent.trySend(LuaScriptEvent.NotLuaScript)
                LuaScriptStorage.ImportResult.TooLarge -> _luaScriptEvent.trySend(LuaScriptEvent.TooLarge)
                LuaScriptStorage.ImportResult.Failed -> _luaScriptEvent.trySend(LuaScriptEvent.AddFailed)
            }
        }
    }

    private fun deleteLuaScript(fileName: String) {
        // The screen only offers delete once the folder is known, so the file is never left behind
        val scriptFolder = _scriptFolder.value ?: return
        viewModelScope.launch {
            val isFileGone = withContext(Dispatchers.IO) { luaScriptStorage.deleteScript(scriptFolder, fileName) }
            if (!isFileGone) {
                _luaScriptEvent.trySend(LuaScriptEvent.DeleteFailed)
                return@launch
            }

            applyRomConfig(_romConfig.value.withLuaScripts { luaScripts -> luaScripts.filter { it.fileName != fileName } })
            _luaScriptFilesVersion.update { it + 1 }
        }
    }

    /**
     * Checks the script files: which are missing, and the descriptions in their headers. Done whenever the list is built, so editing a file
     * shows up next time
     */
    private suspend fun getLuaScriptsUiModel(romConfig: RomConfig, scriptFolder: String?): RomLuaScriptsUiModel {
        if (scriptFolder == null) {
            return RomLuaScriptsUiModel()
        }

        return withContext(Dispatchers.IO) {
            val fileNames = romConfig.mods.luaScripts.map { it.fileName }
            val missingScripts = fileNames.filter { !luaScriptStorage.scriptExists(scriptFolder, it) }.toSet()
            val descriptions = (fileNames - missingScripts).mapNotNull { fileName ->
                luaScriptStorage.readDescription(scriptFolder, fileName)?.let { fileName to it }
            }.toMap()
            RomLuaScriptsUiModel(missingScripts = missingScripts, descriptions = descriptions)
        }
    }

    private fun RomConfig.withLuaScripts(update: (List<RomLuaScript>) -> List<RomLuaScript>): RomConfig {
        return copy(mods = mods.copy(luaScripts = update(mods.luaScripts)))
    }

    private fun applyRomConfig(newConfig: RomConfig) {
        _romConfig.value = newConfig
        _rom.update { it.copy(config = newConfig) }
        saveRomConfig(newConfig)
    }

    private fun saveRomConfig(newConfig: RomConfig) {
        if (newConfig.gbaSlotConfig is RomGbaSlotConfig.GbaRom) {
            newConfig.gbaSlotConfig.romPath?.let { uriPermissionManager.persistFilePermissions(it, Permission.READ) }
            newConfig.gbaSlotConfig.savePath?.let { uriPermissionManager.persistFilePermissions(it, Permission.READ_WRITE) }
        }
        romsRepository.updateRomConfig(_rom.value, newConfig)
    }
}
