package me.magnum.melonds.impl.dtos.rom

import com.google.gson.annotations.SerializedName
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.domain.model.rom.config.RomModsConfig
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeMicSource
import java.util.UUID

data class RomConfigDto(
    @SerializedName("runtimeConsoleType")
    val runtimeConsoleType: RuntimeConsoleType,
    @SerializedName("runtimeMicSource")
    val runtimeMicSource: RuntimeMicSource,
    @SerializedName("layoutId")
    val layoutId: String?,
    @SerializedName("gbaSlotConfig")
    val gbaSlotConfig: RomGbaSlotConfigDto,
    @SerializedName("customName")
    val customName: String? = null,
    // Configs saved before mods existed have no "mods" entry. Gson leaves it null, which means all mods are off
    @SerializedName("mods")
    val mods: RomModsConfigDto? = null,
    // Configs saved before the swap was kept have no "screensSwapped" entry. Gson leaves it null, which means not swapped
    @SerializedName("screensSwapped")
    val screensSwapped: Boolean? = null,
) {

    companion object {
        fun fromModel(romConfig: RomConfig): RomConfigDto {
            return RomConfigDto(
                romConfig.runtimeConsoleType,
                romConfig.runtimeMicSource,
                romConfig.layoutId?.toString(),
                RomGbaSlotConfigDto.fromModel(romConfig.gbaSlotConfig),
                romConfig.customName,
                RomModsConfigDto.fromModel(romConfig.mods),
                romConfig.screensSwapped,
            )
        }
    }

    fun toModel(): RomConfig {
        return RomConfig(
            runtimeConsoleType,
            runtimeMicSource,
            layoutId?.let { UUID.fromString(it) },
            gbaSlotConfig.toModel(),
            customName = customName,
            mods = mods?.toModel() ?: RomModsConfig(),
            screensSwapped = screensSwapped == true,
        )
    }
}