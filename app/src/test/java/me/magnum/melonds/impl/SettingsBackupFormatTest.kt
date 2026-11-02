package me.magnum.melonds.impl

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsBackupFormatTest {

    private fun putTyped(entry: JSONObject): Map<String, *> {
        val preferences = FakeSharedPreferences()
        val editor = preferences.edit()
        SettingsBackupFormat.putTypedPreference(editor, "key", entry)
        editor.commit()
        return preferences.all
    }

    @Test
    fun getBackupLongReadsTypedAndPlainNumbers() {
        val key = SettingsBackupFormat.LAST_VERSION_KEY
        val typed = SettingsBackupFormat.preferencesToJson(mapOf(key to 5_000_000_000L))

        assertEquals(5_000_000_000L, SettingsBackupFormat.getBackupLong(typed, key))
        // Backups made before the values had types
        assertEquals(42L, SettingsBackupFormat.getBackupLong(JSONObject("""{"$key": 42}"""), key))
        assertEquals(5_000_000_000L, SettingsBackupFormat.getBackupLong(JSONObject("""{"$key": 5000000000}"""), key))
    }

    @Test
    fun longFloatAndSetEntriesComeBackWithTheirTypes() {
        val json = SettingsBackupFormat.preferencesToJson(mapOf("long" to 7L, "float" to 1f, "set" to setOf("a", "b")))

        assertEquals(mapOf("key" to 7L), putTyped(json.getJSONObject("long")))
        assertEquals(mapOf("key" to 1f), putTyped(json.getJSONObject("float")))
        assertEquals(mapOf("key" to setOf("a", "b")), putTyped(json.getJSONObject("set")))
    }

    @Test
    fun setsKeepOnlyTheirStrings() {
        val json = SettingsBackupFormat.preferencesToJson(mapOf("set" to setOf("a", 1, null)))

        val value = json.getJSONObject("set").get("value") as JSONArray
        assertEquals(listOf("a"), (0 until value.length()).map { value.get(it) })
    }

    @Test
    fun valuesOfOtherTypesAreLeftOut() {
        val json = SettingsBackupFormat.preferencesToJson(mapOf("double" to 0.5, "list" to listOf("a"), "null" to null, "int" to 1))

        assertEquals(setOf("int"), json.keys().asSequence().toSet())
        assertTrue(putTyped(JSONObject().put("type", "double").put("value", 0.5)).isEmpty())
    }
}
