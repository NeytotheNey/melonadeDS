package me.magnum.rcheevosapi.model

/**
 * What the Web API knows about a game. Coverage is uneven, so anything RetroAchievements left blank is null. Image paths are relative to
 * the media host and are also null when RetroAchievements only has its "no image" placeholder.
 */
data class RAGameDetails(
    val id: RAGameId,
    val title: String,
    val publisher: String?,
    val developer: String?,
    val genre: String?,
    val released: String?,
    val releasedGranularity: String?,
    val imageIcon: String?,
    val imageTitle: String?,
    val imageIngame: String?,
    val imageBoxArt: String?,
)
