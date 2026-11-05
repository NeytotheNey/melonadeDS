package me.magnum.melonds.domain.model.rom

import java.net.URL

/**
 * The information a library card can show about a ROM beyond what the launcher already has (name, file and banner icon).
 *
 * Every field is optional because it comes from sources with uneven coverage: the ROM banner only gives the title and a publisher line,
 * the RetroAchievements connect API adds a game icon, and box art, developer, genre and release date need the RetroAchievements Web API
 * (see docs/plan-launcher.md).
 */
data class RomLibraryMetadata(
    val publisher: String? = null,
    val developer: String? = null,
    val genre: String? = null,
    val releaseYear: Int? = null,
    /** Portrait box art, the preferred tile image. */
    val boxArtUrl: URL? = null,
    /** The RetroAchievements game icon, a square image used when there is no box art. */
    val gameIconUrl: URL? = null,
    /** True when RetroAchievements has an achievement set for this ROM's hash. */
    val hasAchievements: Boolean = false,
) {

    /** The best image for a tile, or null when only the banner icon is available. */
    val tileImageUrl: URL?
        get() = boxArtUrl ?: gameIconUrl

    /** "Publisher · 2009" style caption, or null when nothing is known. */
    fun caption(): String? {
        val parts = listOfNotNull(publisher?.takeIf { it.isNotBlank() }, releaseYear?.toString())
        return parts.takeIf { it.isNotEmpty() }?.joinToString(" · ")
    }

    companion object {
        /** What the launcher knows without any network source: the banner's publisher line. */
        fun fromBanner(rom: Rom): RomLibraryMetadata {
            return RomLibraryMetadata(publisher = rom.developerName.takeIf { it.isNotBlank() })
        }
    }
}
