package me.magnum.melonds.database.entities.retroachievements

import androidx.room.ColumnInfo

data class RAGameAchievementCounts(
    @ColumnInfo(name = "game_id") val gameId: Long,
    @ColumnInfo(name = "total") val total: Int,
    @ColumnInfo(name = "earned") val earned: Int,
    @ColumnInfo(name = "is_user_data_known") val isUserDataKnown: Boolean,
)
