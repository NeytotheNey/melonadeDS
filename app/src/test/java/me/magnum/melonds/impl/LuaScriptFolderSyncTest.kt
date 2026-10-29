package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import org.junit.Assert.assertEquals
import org.junit.Test

class LuaScriptFolderSyncTest {

    @Test
    fun newFileIsAddedSwitchedOff() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true))

        val synced = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = listOf("aim.lua", "turbo.lua"))

        assertEquals(listOf(RomLuaScript("aim.lua", enabled = true), RomLuaScript("turbo.lua", enabled = false)), synced)
    }

    @Test
    fun goneFileIsDroppedWhenTheFolderCanBeRead() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true), RomLuaScript("turbo.lua", enabled = true))

        val synced = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = listOf("turbo.lua"))

        assertEquals(listOf(RomLuaScript("turbo.lua", enabled = true)), synced)
    }

    @Test
    fun listIsKeptWhenTheFolderCantBeRead() {
        val scripts = listOf(RomLuaScript("turbo.lua", enabled = false), RomLuaScript("aim.lua", enabled = true))

        val synced = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = null)

        assertEquals(scripts, synced)
    }

    @Test
    fun emptyFolderClearsTheList() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true))

        assertEquals(emptyList<RomLuaScript>(), LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = emptyList()))
    }

    @Test
    fun listWithNoFolderIsCleared() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true))

        assertEquals(emptyList<RomLuaScript>(), LuaScriptFolderSync.sync(scripts, hasFolder = false, folderScriptNames = null))
    }

    @Test
    fun scriptsAreSortedByNameIgnoringCase() {
        val synced = LuaScriptFolderSync.sync(emptyList(), hasFolder = true, folderScriptNames = listOf("b.lua", "C.lua", "a.lua", "A.lua"))

        assertEquals(listOf("A.lua", "a.lua", "b.lua", "C.lua"), synced.map { it.fileName })
    }

    @Test
    fun sameNameListedTwiceIsOneEntry() {
        val synced = LuaScriptFolderSync.sync(emptyList(), hasFolder = true, folderScriptNames = listOf("aim.lua", "aim.lua"))

        assertEquals(listOf(RomLuaScript("aim.lua", enabled = false)), synced)
    }
}
