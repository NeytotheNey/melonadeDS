package me.magnum.melonds.impl.dtos.rom

import androidx.core.net.toUri
import com.google.gson.annotations.SerializedName
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RomModsConfig

data class RomModsConfigDto(
    @SerializedName("wideViewWidth")
    val wideViewWidth: Int?,
    @SerializedName("luaScriptFolder")
    val luaScriptFolder: String?,
    @SerializedName("luaScripts")
    val luaScripts: List<LuaScriptDto?>?,
) {

    data class LuaScriptDto(
        @SerializedName("fileName")
        val fileName: String?,
        @SerializedName("enabled")
        val enabled: Boolean?,
    )

    fun toModel(): RomModsConfig {
        return RomModsConfig(
            wideViewWidth = wideViewWidth,
            luaScriptFolder = luaScriptFolder?.toUri(),
            // Gson bypasses Kotlin defaults and null checks, so a missing list, a null entry or a missing file name can come through as null.
            // Entries without a file name are dropped instead of failing the whole config
            luaScripts = luaScripts?.mapNotNull { script ->
                script?.fileName?.let { RomLuaScript(it, script.enabled == true) }
            }.orEmpty(),
        )
    }

    companion object {
        fun fromModel(romModsConfig: RomModsConfig): RomModsConfigDto {
            return RomModsConfigDto(
                wideViewWidth = romModsConfig.wideViewWidth,
                luaScriptFolder = romModsConfig.luaScriptFolder?.toString(),
                luaScripts = romModsConfig.luaScripts.map { LuaScriptDto(it.fileName, it.enabled) },
            )
        }
    }
}
