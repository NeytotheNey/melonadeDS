package me.magnum.melonds.parcelables

import android.os.Parcel
import android.os.Parcelable
import androidx.core.net.toUri
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RomModsConfig
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeMicSource
import me.magnum.melonds.extensions.parcelable
import java.util.UUID

class RomConfigParcelable : Parcelable {
    val romConfig: RomConfig

    constructor(romConfig: RomConfig) {
        this.romConfig = romConfig
    }

    private constructor(parcel: Parcel) {
        romConfig = RomConfig(
            runtimeConsoleType = RuntimeConsoleType.entries[parcel.readInt()],
            runtimeMicSource = RuntimeMicSource.entries[parcel.readInt()],
            layoutId = parcel.readString()?.let { UUID.fromString(it) },
            gbaSlotConfig = parcel.parcelable<RomGbaSlotConfigParcelable>()!!.gbaSlotConfig,
            customName = parcel.readString(),
            mods = RomModsConfig(
                wideViewWidth = parcel.readInt().takeIf { it > 0 },
                luaScriptFolder = parcel.readString()?.toUri(),
                luaScripts = List(parcel.readInt()) {
                    RomLuaScript(
                        fileName = parcel.readString()!!,
                        enabled = parcel.readInt() != 0,
                        options = List(parcel.readInt()) { parcel.readString()!! to parcel.readString()!! }.toMap(),
                    )
                },
                wideLayoutId = parcel.readString()?.let { UUID.fromString(it) },
            ),
            screensSwapped = parcel.readInt() != 0,
        )
    }

    override fun writeToParcel(dest: Parcel, flags: Int) {
        dest.writeInt(romConfig.runtimeConsoleType.ordinal)
        dest.writeInt(romConfig.runtimeMicSource.ordinal)
        dest.writeString(romConfig.layoutId?.toString())
        dest.writeParcelable(RomGbaSlotConfigParcelable(romConfig.gbaSlotConfig), 0)
        dest.writeString(romConfig.customName)
        // 0 means widescreen is off
        dest.writeInt(romConfig.mods.wideViewWidth ?: 0)
        dest.writeString(romConfig.mods.luaScriptFolder?.toString())
        dest.writeInt(romConfig.mods.luaScripts.size)
        romConfig.mods.luaScripts.forEach {
            dest.writeString(it.fileName)
            dest.writeInt(if (it.enabled) 1 else 0)
            dest.writeInt(it.options.size)
            it.options.forEach { (key, value) ->
                dest.writeString(key)
                dest.writeString(value)
            }
        }
        dest.writeString(romConfig.mods.wideLayoutId?.toString())
        dest.writeInt(if (romConfig.screensSwapped) 1 else 0)
    }

    override fun describeContents(): Int {
        return 0
    }

    companion object CREATOR : Parcelable.Creator<RomConfigParcelable> {
        override fun createFromParcel(parcel: Parcel): RomConfigParcelable {
            return RomConfigParcelable(parcel)
        }

        override fun newArray(size: Int): Array<RomConfigParcelable?> {
            return arrayOfNulls(size)
        }
    }
}