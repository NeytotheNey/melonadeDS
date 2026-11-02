package me.magnum.melonds.ui.romdetails

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.domain.model.rom.config.RomGbaSlotConfig
import me.magnum.melonds.domain.repositories.LayoutsRepository
import me.magnum.melonds.ui.romdetails.model.RomConfigUiModel
import me.magnum.melonds.ui.romdetails.model.RomGbaSlotConfigUiModel
import me.magnum.melonds.ui.romdetails.model.RomLuaScriptsUiModel
import me.magnum.melonds.utils.FileUtils
import java.io.File
import java.util.concurrent.ConcurrentHashMap

class RomDetailsUiMapper(
    private val context: Context,
    private val layoutsRepository: LayoutsRepository,
) {

    // Display path of each script folder, by tree URI
    private val folderPathCache = ConcurrentHashMap<Uri, String>()

    suspend fun mapRomConfigToUi(romConfig: RomConfig): RomConfigUiModel {
        return RomConfigUiModel(
            runtimeConsoleType = romConfig.runtimeConsoleType,
            runtimeMicSource = romConfig.runtimeMicSource,
            layoutId = romConfig.layoutId,
            layoutName = romConfig.layoutId?.let { layoutsRepository.getLayout(it)?.name } ?: layoutsRepository.getGlobalLayoutPlaceholder().name,
            gbaSlotConfig = mapGbaSlotConfigToUi(romConfig.gbaSlotConfig),
            customName = romConfig.customName,
            mods = romConfig.mods,
            luaScripts = RomLuaScriptsUiModel(folderPath = romConfig.mods.luaScriptFolder?.let { getFolderPath(it) }),
        )
    }

    private suspend fun getFolderPath(folderUri: Uri): String {
        // Cached per folder and worked out off the main thread, since resolving the path uses reflection and maybe a provider query
        folderPathCache[folderUri]?.let { return it }
        val folderPath = withContext(Dispatchers.IO) {
            // Prefer the resolved path, but a bare "/" means the volume is unknown, so the path inside the volume is shown instead
            FileUtils.getAbsolutePathFromSAFUri(context, folderUri)?.takeIf { it != File.separator }
                ?: getTreeDocumentPath(folderUri)
                ?: folderUri.lastPathSegment
                ?: folderUri.toString()
        }
        folderPathCache[folderUri] = folderPath
        return folderPath
    }

    /**
     * The path part of a tree URI's document ID, like `LuaScripts/White2` for `primary:LuaScripts/White2`, or null if there's none
     */
    private fun getTreeDocumentPath(treeUri: Uri): String? {
        return runCatching { DocumentsContract.getTreeDocumentId(treeUri) }.getOrNull()
            ?.substringAfter(':')
            ?.takeIf { it.isNotEmpty() }
    }

    private fun mapGbaSlotConfigToUi(gbaSlotConfig: RomGbaSlotConfig): RomGbaSlotConfigUiModel {
        return when (gbaSlotConfig) {
            is RomGbaSlotConfig.None -> RomGbaSlotConfigUiModel(type = RomGbaSlotConfigUiModel.Type.None)
            is RomGbaSlotConfig.GbaRom -> RomGbaSlotConfigUiModel(
                type = RomGbaSlotConfigUiModel.Type.GbaRom,
                gbaRomPath = gbaSlotConfig.romPath?.let { DocumentFile.fromSingleUri(context, it)?.name },
                gbaSavePath = gbaSlotConfig.savePath?.let { DocumentFile.fromSingleUri(context, it)?.name },
            )
            RomGbaSlotConfig.RumblePak -> RomGbaSlotConfigUiModel(type = RomGbaSlotConfigUiModel.Type.RumblePak)
            is RomGbaSlotConfig.MemoryExpansion -> RomGbaSlotConfigUiModel(type = RomGbaSlotConfigUiModel.Type.MemoryExpansion)
            RomGbaSlotConfig.MotionPakHomebrew -> RomGbaSlotConfigUiModel(type = RomGbaSlotConfigUiModel.Type.MotionPakHomebrew)
            RomGbaSlotConfig.MotionPakRetail -> RomGbaSlotConfigUiModel(type = RomGbaSlotConfigUiModel.Type.MotionPakRetail)
        }
    }
}
