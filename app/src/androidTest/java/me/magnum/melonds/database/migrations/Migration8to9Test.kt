package me.magnum.melonds.database.migrations

import android.content.ContentValues
import android.database.sqlite.SQLiteDatabase
import androidx.room.testing.MigrationTestHelper
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import me.magnum.melonds.database.MelonDatabase
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

private const val TEST_DB = "migration-8-to-9-test-db"

@RunWith(AndroidJUnit4::class)
class Migration8to9Test {

    @get:Rule
    val migrationTestHelper = MigrationTestHelper(
        InstrumentationRegistry.getInstrumentation(),
        MelonDatabase::class.java,
    )

    @Test
    fun existing_games_are_kept_and_the_metadata_table_starts_empty() {
        migrationTestHelper.createDatabase(TEST_DB, 8).use { db ->
            val game = ContentValues().apply {
                put("game_id", 1L)
                put("title", "Game")
                put("icon", "https://example.com/icon.png")
            }
            db.insert("ra_game", SQLiteDatabase.CONFLICT_FAIL, game)
        }

        migrationTestHelper.runMigrationsAndValidate(TEST_DB, 9, true, Migration8to9()).use { db ->
            db.query("SELECT * FROM ra_game").use {
                assertEquals(1, it.count)
            }
            db.query("SELECT * FROM ra_game_metadata").use {
                assertEquals(0, it.count)
            }
        }
    }
}
