package me.magnum.melonds.database.entities.retroachievements

import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.PrimaryKey

/**
 * What `API_GetGame` said about a game, kept for good so each game costs one Web API call (see docs/plan-launcher.md). A row also records
 * the games that call couldn't help with, through [fetchStatus].
 */
@Entity(tableName = "ra_game_metadata")
data class RAGameMetadataEntity(
    @PrimaryKey @ColumnInfo(name = "game_id") val gameId: Long,
    @ColumnInfo(name = "publisher") val publisher: String?,
    @ColumnInfo(name = "developer") val developer: String?,
    @ColumnInfo(name = "genre") val genre: String?,
    @ColumnInfo(name = "released") val released: String?,
    @ColumnInfo(name = "released_granularity") val releasedGranularity: String?,
    @ColumnInfo(name = "image_box_art") val imageBoxArt: String?,
    @ColumnInfo(name = "image_title") val imageTitle: String?,
    @ColumnInfo(name = "image_ingame") val imageIngame: String?,
    @ColumnInfo(name = "image_icon") val imageIcon: String?,
    @ColumnInfo(name = "fetched_at") val fetchedAt: Long,
    @ColumnInfo(name = "fetch_status") val fetchStatus: Int,
) {

    companion object {
        const val STATUS_OK = 0
        const val STATUS_NOT_FOUND = 1
        // Retried on the next sync. Such a row can still hold the data of an earlier fetch, since a refresh keeps it until new data arrives
        const val STATUS_FAILED = 2
    }
}
