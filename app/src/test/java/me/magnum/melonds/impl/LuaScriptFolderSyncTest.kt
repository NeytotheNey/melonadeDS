package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
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
    fun emptyListingKeepsKnownScriptsWithTheirSwitchesAndOptions() {
        // What a folder hidden by Android looks like: an empty listing with no error flag
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true, options = mapOf("speed" to "7")), RomLuaScript("turbo.lua", enabled = false))

        val synced = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = emptyList())

        assertSame(scripts, synced)
    }

    @Test
    fun emptyListingWithNoKnownScriptsStaysEmpty() {
        assertEquals(emptyList<RomLuaScript>(), LuaScriptFolderSync.sync(emptyList(), hasFolder = true, folderScriptNames = emptyList()))
    }

    @Test
    fun scriptsComeBackUnchangedWhenTheHiddenFolderReturns() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true, options = mapOf("speed" to "7")))

        val whileHidden = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = emptyList())
        val afterReturn = LuaScriptFolderSync.sync(whileHidden, hasFolder = true, folderScriptNames = listOf("aim.lua"))

        assertEquals(scripts, afterReturn)
    }

    @Test
    fun goneFileIsStillDroppedWhenOthersAreListed() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true), RomLuaScript("turbo.lua", enabled = true))

        val synced = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = listOf("aim.lua"))

        assertEquals(listOf(RomLuaScript("aim.lua", enabled = true)), synced)
    }

    @Test
    fun emptyListingIsSuspiciousOnlyWhenScriptsAreKnown() {
        assertTrue(LuaScriptFolderSync.isSuspiciouslyEmpty(knownScriptCount = 1, folderScriptNames = emptyList()))
        assertFalse(LuaScriptFolderSync.isSuspiciouslyEmpty(knownScriptCount = 0, folderScriptNames = emptyList()))
        assertFalse(LuaScriptFolderSync.isSuspiciouslyEmpty(knownScriptCount = 2, folderScriptNames = listOf("aim.lua")))
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

    @Test
    fun optionsAreKeptWhenSyncing() {
        val scripts = listOf(RomLuaScript("aim.lua", enabled = true, options = mapOf("speed" to "7")))

        val synced = LuaScriptFolderSync.sync(scripts, hasFolder = true, folderScriptNames = listOf("aim.lua", "turbo.lua"))

        assertEquals(listOf(RomLuaScript("aim.lua", enabled = true, options = mapOf("speed" to "7")), RomLuaScript("turbo.lua", enabled = false)), synced)
    }
}
