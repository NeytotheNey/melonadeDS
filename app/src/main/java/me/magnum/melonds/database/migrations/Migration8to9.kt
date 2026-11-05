package me.magnum.melonds.database.migrations

import androidx.room.migration.Migration
import androidx.sqlite.db.SupportSQLiteDatabase

/**
 * Adds the `ra_game_metadata` table, the local cache of the RetroAchievements Web API.
 */
class Migration8to9 : Migration(8, 9) {

    companion object {
        // Not private so a JVM test can hold it against the exported schema, since the migration test itself needs a device
        val CREATE_RA_GAME_METADATA = """
            CREATE TABLE IF NOT EXISTS `ra_game_metadata` (
                `game_id` INTEGER NOT NULL,
                `publisher` TEXT,
                `developer` TEXT,
                `genre` TEXT,
                `released` TEXT,
                `released_granularity` TEXT,
                `image_box_art` TEXT,
                `image_title` TEXT,
                `image_ingame` TEXT,
                `image_icon` TEXT,
                `fetched_at` INTEGER NOT NULL,
                `fetch_status` INTEGER NOT NULL,
                PRIMARY KEY(`game_id`)
            )
        """.trimIndent()
    }

    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL(CREATE_RA_GAME_METADATA)
    }
}
