package me.magnum.melonds.impl

import android.content.ContextWrapper
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.common.retroachievements.AndroidRAUserAuthStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Rule
import org.junit.Test
import org.json.JSONObject
import org.junit.rules.TemporaryFolder
import java.io.File

class PendingSettingsRestoreTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    private val filesDir by lazy { tempFolder.newFolder("files") }

    // Only filesDir is used, so the rest of the Context can stay stubbed
    private val context by lazy {
        object : ContextWrapper(null) {
            override fun getFilesDir(): File = this@PendingSettingsRestoreTest.filesDir
        }
    }

    /**
     * Stages a backup the way SettingsBackupManager does, then applies it like the app does on start.
     */
    private fun restore(settingsJson: String, preferences: FakeSharedPreferences) {
        val stagingDir = File(filesDir, SettingsBackupFormat.STAGING_DIR).apply { mkdirs() }
        File(stagingDir, SettingsBackupFormat.SETTINGS_FILE).writeText(settingsJson)
        File(stagingDir, SettingsBackupFormat.READY_MARKER).createNewFile()

        PendingSettingsRestore(context, preferences).applyIfPending()

        assertEquals(true, PendingSettingsRestore.consumeResult(context))
        assertFalse(stagingDir.exists())
    }

    @Test
    fun typedBackupComesBackWithTheSameTypes() {
        val backedUp = mapOf(
            // Floats of exactly 0 and 1 are written as "0" and "1" by org.json, so they used to come back as ints
            "float_zero" to 0f,
            "float_one" to 1f,
            "float_two" to 2f,
            "float_fraction" to 0.1f,
            "int" to 3,
            "long" to 5_000_000_000L,
            "small_long" to 7L,
            "boolean" to true,
            "string" to "hello",
            "string_set" to setOf("a", "b"),
            SettingsBackupFormat.LAST_VERSION_KEY to 42L,
        )
        val preferences = FakeSharedPreferences()

        restore(SettingsBackupFormat.preferencesToJson(backedUp).toString(), preferences)

        // Map equality also checks the types, since 1f != 1 and 7L != 7
        assertEquals(backedUp, preferences.all)
    }

    @Test
    fun keptKeysKeepTheirCurrentValues() {
        val preferences = FakeSharedPreferences(
            mapOf(
                PreferenceKeys.PERF_LOG_DIR to "content://this-install",
                AndroidRAUserAuthStore.TOKEN_KEY to "this-install-token",
                "not_in_backup" to 1,
                SettingsBackupFormat.LAST_VERSION_KEY to 42L,
            )
        )
        // A backup shouldn't have these, but if one does they still mustn't overwrite this install's values or be added
        val backup = SettingsBackupFormat.preferencesToJson(
            mapOf(
                PreferenceKeys.PERF_LOG_DIR to "content://other-install",
                AndroidRAUserAuthStore.USERNAME_KEY to "someone-else",
                "string" to "restored",
            )
        )

        restore(backup.toString(), preferences)

        val expected = mapOf(
            PreferenceKeys.PERF_LOG_DIR to "content://this-install",
            AndroidRAUserAuthStore.TOKEN_KEY to "this-install-token",
            // A backup without a version keeps the current one, so the migrations don't run on it again
            SettingsBackupFormat.LAST_VERSION_KEY to 42L,
            "string" to "restored",
            // "not_in_backup" goes back to its default
        )
        assertEquals(expected, preferences.all)
    }

    @Test
    fun oldUntypedBackupStillRestores() {
        // Backups from before the values had types store plain JSON values. A whole-number float like 1.0 can't be told apart from an int in
        // these files, so it comes back as an Int. That's a known limitation of the old format, so it isn't tested here
        val oldBackup = """
            {
              "boolean": true,
              "int": 3,
              "float_fraction": 0.5,
              "string": "hello",
              "string_set": ["a", "b"],
              "last_version": 42
            }
        """.trimIndent()
        val preferences = FakeSharedPreferences()

        restore(oldBackup, preferences)

        val expected = mapOf(
            "boolean" to true,
            "int" to 3,
            "float_fraction" to 0.5f,
            "string" to "hello",
            "string_set" to setOf("a", "b"),
            // last_version is known to be a Long, even when the number would fit in an Int
            SettingsBackupFormat.LAST_VERSION_KEY to 42L,
        )
        assertEquals(expected, preferences.all)
    }

    @Test
    fun oldUntypedDecimalParsedAsDoubleComesBackAsFloat() {
        // On the device, Android's org.json parses a decimal like 0.5 to a Double. The org.json used in these tests parses it to a BigDecimal,
        // so a staged file can't get a Double through. Instead, a JSONObject holding a real Double is passed straight to the private
        // applyPreferences() through reflection
        val oldBackup = JSONObject().put("float_fraction", 0.5)
        val preferences = FakeSharedPreferences()
        val restore = PendingSettingsRestore(context, preferences)

        PendingSettingsRestore::class.java.getDeclaredMethod("applyPreferences", JSONObject::class.java).apply {
            isAccessible = true
            invoke(restore, oldBackup)
        }

        assertEquals(mapOf("float_fraction" to 0.5f), preferences.all)
    }
}
