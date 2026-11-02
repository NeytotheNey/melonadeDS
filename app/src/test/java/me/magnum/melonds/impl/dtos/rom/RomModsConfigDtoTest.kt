package me.magnum.melonds.impl.dtos.rom

import com.google.gson.Gson
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import org.junit.Assert.assertEquals
import org.junit.Test

class RomModsConfigDtoTest {

    @Test
    fun oldConfigsLoadWithNoOptions() {
        val json = """{"wideViewWidth":null,"luaScriptFolder":null,"luaScripts":[{"fileName":"aim.lua","enabled":true}]}"""

        val config = Gson().fromJson(json, RomModsConfigDto::class.java).toModel()

        assertEquals(listOf(RomLuaScript("aim.lua", enabled = true)), config.luaScripts)
    }

    @Test
    fun optionsSurviveSavingAndLoading() {
        val gson = Gson()
        val json = """{"luaScripts":[{"fileName":"aim.lua","enabled":false,"options":{"speed":"7","bad":null}}]}"""

        val config = gson.fromJson(json, RomModsConfigDto::class.java).toModel()
        assertEquals(listOf(RomLuaScript("aim.lua", enabled = false, options = mapOf("speed" to "7"))), config.luaScripts)

        val reloaded = gson.fromJson(gson.toJson(RomModsConfigDto.fromModel(config)), RomModsConfigDto::class.java).toModel()
        assertEquals(config, reloaded)
    }
}
