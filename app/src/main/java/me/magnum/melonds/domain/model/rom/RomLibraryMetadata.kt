package me.magnum.melonds.domain.model.rom

import java.io.File

/**
 * What the last `API_GetGame` call said about a game: it answered, it doesn't know the game, or it failed and gets tried again on the next sync.
 */
enum class RomMetadataFetchStatus {
    OK,
    NOT_FOUND,
    FAILED,
}

/**
 * The information a library card can show about a ROM beyond what the launcher already has (name, file and banner icon).
 *
 * Every field is optional because it comes from sources with uneven coverage: the ROM banner only gives the title and a publisher line,
 * the RetroAchievements connect API adds a title and a game icon once the game was opened with a login, and the art, developer, genre and
 * release date need the RetroAchievements Web API (see docs/plan-launcher.md).
 */
data class RomLibraryMetadata(
    val publisher: String? = null,
    val developer: String? = null,
    val genre: String? = null,
    val releaseYear: Int? = null,
    /** The game's name on RetroAchievements. */
    val title: String? = null,
    /** The downloaded art, the preferred tile image: box art when RetroAchievements has it, otherwise its title screen or icon. */
    val artFile: File? = null,
    /**
     * The RetroAchievements game icon, a square image used when no art was downloaded. A string because this class gets compared, and
     * java.net.URL's equals() can do a DNS lookup.
     */
    val gameIconUrl: String? = null,
    /** True when achievements for this ROM's game are in the local cache. */
    val hasAchievements: Boolean = false,
    /** When [artFile] was written, so that art downloaded again to the same path can be told apart from what failed to load before. */
    val artModifiedAt: Long? = null,
    /** The release date as RetroAchievements gave it, "YYYY-MM-DD" with or without a time. Only [releasedGranularity] says how much of it is real. */
    val released: String? = null,
    /** "day", "month" or "year", or null when RetroAchievements doesn't say. */
    val releasedGranularity: String? = null,
    /** The title screen, when it's on disk. The games that have one get a wide tile in the mosaic. */
    val titleArtFile: File? = null,
    /** How many achievements are in the local cache for this game, or null when none are. */
    val achievementsInSet: Int? = null,
    /** How many of them this user earned, or null when the user's data for this game was never fetched, which isn't the same as none. */
    val achievementsEarned: Int? = null,
    /** Null when `API_GetGame` was never asked about this game. */
    val fetchStatus: RomMetadataFetchStatus? = null,
    /** When [fetchStatus] was last updated, in epoch ms. */
    val fetchedAt: Long? = null,
) {

    /** True when anything here came from RetroAchievements rather than the banner. */
    val hasRetroAchievementsData: Boolean
        get() = title != null ||
            developer != null ||
            genre != null ||
            released != null ||
            artFile != null ||
            gameIconUrl != null ||
            hasAchievements ||
            fetchStatus == RomMetadataFetchStatus.OK

    /** The best image for a tile (a [File] or a URL string), or null when only the banner icon is available. */
    val tileImage: Any?
        get() = artFile ?: gameIconUrl

    /** "Publisher · 2009" style caption, or null when nothing is known. */
    fun caption(): String? {
        val parts = listOfNotNull(publisher, releaseYear?.toString())
        return parts.takeIf { it.isNotEmpty() }?.joinToString(SEPARATOR)
    }

    /** The longer line for the ROM details header, "Publisher · Developer · Genre · 2009", or null when nothing is known. */
    fun details(): String? {
        val parts = listOfNotNull(publisher, developer?.takeIf { it != publisher }, genre, releaseYear?.toString())
        return parts.takeIf { it.isNotEmpty() }?.joinToString(SEPARATOR)
    }

    /**
     * The name to show for [rom]: the user's own name, then the banner's. RetroAchievements' only fills in for a banner with no title, which is also when a
     * scan makes the ROM's name up from the file name.
     */
    fun displayTitle(rom: Rom): String {
        return displayTitle(rom.config.customName, rom.name, rom.fileName)
    }

    fun displayTitle(customName: String?, name: String, fileName: String): String {
        val fileStem = fileName.substringBeforeLast('.')
        return customName ?: name.takeIf { it.isNotBlank() && it != fileStem } ?: title ?: fileStem
    }

    companion object {
        private const val SEPARATOR = " · "

        /** What the launcher knows without any network source: the banner's publisher line. */
        fun fromBanner(rom: Rom): RomLibraryMetadata {
            return fromBanner(rom.developerName)
        }

        fun fromBanner(bannerPublisher: String): RomLibraryMetadata {
            return RomLibraryMetadata(publisher = bannerPublisher.trim().takeIf { it.isNotEmpty() })
        }
    }
}
