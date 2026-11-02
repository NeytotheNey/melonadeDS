package me.magnum.melonds.impl

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaScriptDataStoreTest {

    @Test
    fun realGameCodesAreTheKey() {
        assertEquals("AMHE", LuaScriptDataStore.getGameKey("AMHE", "content://roms/hunters.nds"))
    }

    @Test
    fun placeholderCodesAreKeyedByTheRomUri() {
        val key = LuaScriptDataStore.getGameKey("####", "content://roms/homebrew.nds")!!

        assertTrue(key.matches(Regex("homebrew-[0-9a-f]{16}")))
        assertEquals(key, LuaScriptDataStore.getGameKey("0000", "content://roms/homebrew.nds"))
        assertEquals(key, LuaScriptDataStore.getGameKey("amhe", "content://roms/homebrew.nds"))
        assertTrue(key != LuaScriptDataStore.getGameKey("####", "content://roms/other.nds"))
    }

    @Test
    fun unreadableHeadersHaveNoKey() {
        assertNull(LuaScriptDataStore.getGameKey(null, "content://roms/hunters.nds"))
    }

    @Test
    fun scriptNamesBecomeFileNames() {
        assertEquals("aim.lua.bin", LuaScriptDataStore.getFileName("aim.lua"))
        assertEquals("Ünïcode.lua.bin", LuaScriptDataStore.getFileName("Ünïcode.lua"))
    }

    @Test
    fun unsafeScriptNamesAreHashed() {
        val unsafeNames = listOf("../evil.lua", "a/b.lua", "a\\b.lua", "..", ".", "", "x".repeat(201))
        unsafeNames.forEach {
            assertTrue(it, LuaScriptDataStore.getFileName(it).matches(Regex("script-[0-9a-f]{16}\\.bin")))
        }
    }
}
