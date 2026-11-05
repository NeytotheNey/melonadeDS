package me.magnum.melonds.database.daos

import androidx.room.Dao
import androidx.room.Query
import androidx.room.Upsert
import kotlinx.coroutines.flow.Flow
import me.magnum.melonds.database.entities.retroachievements.RAGameAchievementCounts
import me.magnum.melonds.database.entities.retroachievements.RAGameEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameHashEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity

@Dao
interface RAGameMetadataDao {

    @Query("SELECT * FROM ra_game_metadata")
    suspend fun getAllMetadata(): List<RAGameMetadataEntity>

    @Upsert
    suspend fun upsertMetadata(metadata: RAGameMetadataEntity)

    /**
     * Makes the next sync fetch again the games fetched at or before [fetchedBefore], found or not. The rows keep their data, so the cards don't
     * lose it while waiting for the sync.
     */
    @Query("UPDATE ra_game_metadata SET fetch_status = ${RAGameMetadataEntity.STATUS_FAILED} WHERE fetched_at <= :fetchedBefore")
    suspend fun markForRefetch(fetchedBefore: Long)

    /**
     * Only the hashes that lead somewhere, since the library holds every hash RetroAchievements knows.
     */
    @Query("SELECT * FROM ra_game_hash_library WHERE game_id IN (SELECT game_id FROM ra_game UNION SELECT game_id FROM ra_game_metadata)")
    suspend fun getHashesOfKnownGames(): List<RAGameHashEntity>

    // Rich presence scripts are of no use to a card and can be long
    @Query("SELECT game_id, NULL AS rich_presence_patch, title, icon FROM ra_game")
    suspend fun getGamesWithoutRichPresence(): List<RAGameEntity>

    /**
     * One row for each game with achievements in the local cache. The user's data is only known once it was fetched for the game, since a game with
     * nothing earned has no unlock rows either.
     */
    @Query(
        """
        SELECT a.game_id AS game_id,
            COUNT(*) AS total,
            (SELECT COUNT(DISTINCT u.achievement_id) FROM ra_user_achievement u WHERE u.game_id = a.game_id AND u.is_unlocked = 1) AS earned,
            EXISTS(
                SELECT 1 FROM ra_game_set_metadata m
                WHERE m.game_id = a.game_id AND (m.last_user_data_updated IS NOT NULL OR m.last_hardcore_user_data_updated IS NOT NULL)
            ) AS is_user_data_known
        FROM ra_achievement a
        GROUP BY a.game_id
        """
    )
    suspend fun getAchievementCounts(): List<RAGameAchievementCounts>

    @Query("SELECT EXISTS(SELECT 1 FROM ra_game_hash_library)")
    suspend fun hasHashLibrary(): Boolean

    /**
     * The ones of [gameHashes] that are in the hash library. This goes by what's asked for since the library holds every hash RetroAchievements knows.
     * Keep [gameHashes] well under SQLite's limit of 999 parameters.
     */
    @Query("SELECT game_hash FROM ra_game_hash_library WHERE game_hash IN (:gameHashes)")
    suspend fun findHashesInLibrary(gameHashes: List<String>): List<String>

    /**
     * Emits whenever a table the metadata snapshot reads from is written to. The value means nothing, so it only has to name the tables and be cheap.
     */
    @Query("SELECT EXISTS(SELECT 1 FROM ra_game_metadata) + EXISTS(SELECT 1 FROM ra_game) + EXISTS(SELECT 1 FROM ra_achievement) + EXISTS(SELECT 1 FROM ra_game_hash_library)")
    fun observeSourceTables(): Flow<Long>
}
