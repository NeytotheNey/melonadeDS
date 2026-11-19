package me.magnum.melonds.ui.romdetails.model

import android.net.Uri
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeMicSource
import java.util.UUID

sealed class RomConfigUpdateEvent {
    data class RuntimeConsoleUpdate(val newRuntimeConsole: RuntimeConsoleType) : RomConfigUpdateEvent()
    data class RuntimeMicSourceUpdate(val newRuntimeMicSource: RuntimeMicSource) : RomConfigUpdateEvent()
    data class LayoutUpdate(val newLayoutId: UUID?) : RomConfigUpdateEvent()
    data class GbaSlotTypeUpdated(val type: RomGbaSlotConfigUiModel.Type) : RomConfigUpdateEvent()
    data class GbaRomPathUpdate(val gbaRomPath: Uri?) : RomConfigUpdateEvent()
    data class GbaSavePathUpdate(val gbaSavePath: Uri?) : RomConfigUpdateEvent()
    data class CustomNameUpdate(val customName: String?) : RomConfigUpdateEvent()
    data class WideViewWidthUpdate(val newWideViewWidth: Int?) : RomConfigUpdateEvent()
    /**
     * The game's wide layout, or null for automatic
     */
    data class WideLayoutUpdate(val newWideLayoutId: UUID?) : RomConfigUpdateEvent()
    data class WideCameraUpdate(val enabled: Boolean) : RomConfigUpdateEvent()
    /**
     * The widest the camera zooms out to, in DS pixels
     */
    data class WideCameraMaxWidthUpdate(val maxWidth: Int) : RomConfigUpdateEvent()
    data class WideCameraReturnToCenterUpdate(val enabled: Boolean) : RomConfigUpdateEvent()
    data class WideCameraZoomOnTextUpdate(val enabled: Boolean) : RomConfigUpdateEvent()
    data class LuaScriptToggle(val fileName: String, val enabled: Boolean) : RomConfigUpdateEvent()
    /**
     * Links a picked folder as the game's script folder, or unlinks it if [folderUri] is null
     */
    data class LuaScriptFolderUpdate(val folderUri: Uri?) : RomConfigUpdateEvent()
    /**
     * A new [value] for the option [key] of the script [fileName], as LuaScriptOption.resolve() gives it
     */
    data class LuaScriptOptionUpdate(val fileName: String, val key: String, val value: String) : RomConfigUpdateEvent()
    /**
     * Deletes what the script [fileName] saved with melon.storage for this game
     */
    data class LuaScriptDataClear(val fileName: String) : RomConfigUpdateEvent()
    /**
     * The save backups list was opened, so it's read again
     */
    data object SaveBackupsShown : RomConfigUpdateEvent()
    /**
     * Restores the backup [fileName] over the game's save
     */
    data class SaveBackupRestore(val fileName: String) : RomConfigUpdateEvent()
}
