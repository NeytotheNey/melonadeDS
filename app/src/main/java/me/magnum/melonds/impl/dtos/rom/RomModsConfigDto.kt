package me.magnum.melonds.impl.dtos.rom

import androidx.core.net.toUri
import com.google.gson.annotations.SerializedName
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RomModsConfig
import java.util.UUID

data class RomModsConfigDto(
    @SerializedName("wideViewWidth")
    val wideViewWidth: Int?,
    @SerializedName("luaScriptFolder")
    val luaScriptFolder: String?,
    @SerializedName("luaScripts")
    val luaScripts: List<LuaScriptDto?>?,
    // Missing in configs saved before wide layouts
    @SerializedName("wideLayoutId")
    val wideLayoutId: String? = null,
) {

    data class LuaScriptDto(
        @SerializedName("fileName")
        val fileName: String?,
        @SerializedName("enabled")
        val enabled: Boolean?,
        // Missing in configs saved before scripts had options
        @SerializedName("options")
        val options: Map<String, String?>?,
    )

    fun toModel(): RomModsConfig {
        return RomModsConfig(
            wideViewWidth = wideViewWidth,
            luaScriptFolder = luaScriptFolder?.toUri(),
            // Gson bypasses Kotlin defaults and null checks, so a missing list, a null entry or a missing file name can come through as null.
            // Entries without a file name are dropped instead of failing the whole config
            luaScripts = luaScripts?.mapNotNull { script ->
                script?.fileName?.let { fileName ->
                    val options = script.options.orEmpty().mapNotNull { (key, value) -> value?.let { key to it } }.toMap()
                    RomLuaScript(fileName, script.enabled == true, options)
                }
            }.orEmpty(),
            // An ID that can't be read counts as automatic instead of failing the whole config
            wideLayoutId = wideLayoutId?.let { runCatching { UUID.fromString(it) }.getOrNull() },
        )
    }

    companion object {
        fun fromModel(romModsConfig: RomModsConfig): RomModsConfigDto {
            return RomModsConfigDto(
                wideViewWidth = romModsConfig.wideViewWidth,
                luaScriptFolder = romModsConfig.luaScriptFolder?.toString(),
                luaScripts = romModsConfig.luaScripts.map { LuaScriptDto(it.fileName, it.enabled, it.options) },
                wideLayoutId = romModsConfig.wideLayoutId?.toString(),
            )
        }
    }
}
