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
    data class LuaScriptToggle(val fileName: String, val enabled: Boolean) : RomConfigUpdateEvent()
    /**
     * Copies a picked file into the game's script folder. A script with the same name is only replaced if [replace] is set
     */
    data class LuaScriptAdd(val uri: Uri, val replace: Boolean) : RomConfigUpdateEvent()
    data class LuaScriptDelete(val fileName: String) : RomConfigUpdateEvent()
}
