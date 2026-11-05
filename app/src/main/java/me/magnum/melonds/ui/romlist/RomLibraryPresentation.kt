package me.magnum.melonds.ui.romlist

import me.magnum.melonds.domain.model.SortingMode
import me.magnum.melonds.domain.model.SortingOrder
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata

enum class RomLibraryFilter {
    ALL,
    PLAYED,
    NOT_PLAYED,
    HAS_ACHIEVEMENTS,
}

fun <T> filterLibrary(items: List<T>, filter: RomLibraryFilter, isPlayed: (T) -> Boolean, hasAchievements: (T) -> Boolean): List<T> {
    return when (filter) {
        RomLibraryFilter.ALL -> items
        RomLibraryFilter.PLAYED -> items.filter(isPlayed)
        RomLibraryFilter.NOT_PLAYED -> items.filterNot(isPlayed)
        RomLibraryFilter.HAS_ACHIEVEMENTS -> items.filter(hasAchievements)
    }
}

enum class LibraryEmptyReason {
    NO_ROMS,
    NO_SEARCH_RESULTS,
    NO_SEARCH_AND_FILTER_RESULTS,
    NOTHING_PLAYED,
    EVERYTHING_PLAYED,
    NO_ACHIEVEMENTS,
}

/**
 * Why nothing is showing.
 *
 * @param hasRomsBeforeFilter Whether the search, if any, left anything for the filter to work on
 */
fun libraryEmptyReason(hasRomsBeforeFilter: Boolean, filter: RomLibraryFilter, isSearching: Boolean): LibraryEmptyReason {
    return when {
        !hasRomsBeforeFilter -> if (isSearching) LibraryEmptyReason.NO_SEARCH_RESULTS else LibraryEmptyReason.NO_ROMS
        isSearching -> LibraryEmptyReason.NO_SEARCH_AND_FILTER_RESULTS
        else -> when (filter) {
            RomLibraryFilter.ALL -> LibraryEmptyReason.NO_ROMS
            RomLibraryFilter.PLAYED -> LibraryEmptyReason.NOTHING_PLAYED
            RomLibraryFilter.NOT_PLAYED -> LibraryEmptyReason.EVERYTHING_PLAYED
            RomLibraryFilter.HAS_ACHIEVEMENTS -> LibraryEmptyReason.NO_ACHIEVEMENTS
        }
    }
}

enum class MosaicSectionLabel {
    RECENTLY_PLAYED,
    LEAST_RECENTLY_PLAYED,
    A_TO_Z,
    Z_TO_A,
}

/**
 * Names the order the tiles are really in, or nothing when that order has nothing to do with playing and nothing was played. The featured tiles lead
 * whatever the sort, but they carry their own "Continue" and "Recent" labels.
 */
fun mosaicSectionLabel(sortingMode: SortingMode, sortingOrder: SortingOrder, hasPlayedGames: Boolean): MosaicSectionLabel? {
    return when (sortingMode) {
        SortingMode.ALPHABETICALLY -> if (sortingOrder == SortingOrder.ASCENDING) MosaicSectionLabel.A_TO_Z else MosaicSectionLabel.Z_TO_A
        SortingMode.RECENTLY_PLAYED -> when {
            !hasPlayedGames -> null
            sortingOrder == SortingOrder.DESCENDING -> MosaicSectionLabel.RECENTLY_PLAYED
            else -> MosaicSectionLabel.LEAST_RECENTLY_PLAYED
        }
    }
}

enum class RomTileStyle {
    /** Real art fills the tile. */
    ART,
    /** No art, but there's a real title or publisher to show next to the banner icon. */
    TITLED,
    /** Nothing but the file. */
    NO_METADATA,
}

/**
 * @param name The ROM's name, which a scan makes up from the file name when the banner has no title
 */
fun romTileStyle(customName: String?, name: String, fileName: String, metadata: RomLibraryMetadata, hasArt: Boolean): RomTileStyle {
    val hasOwnTitle = customName != null || metadata.title != null || (name.isNotBlank() && name != romFileStem(fileName))
    return when {
        hasArt -> RomTileStyle.ART
        hasOwnTitle || metadata.caption() != null -> RomTileStyle.TITLED
        else -> RomTileStyle.NO_METADATA
    }
}

/** The file name without its extension. */
fun romFileStem(fileName: String): String {
    return fileName.substringBeforeLast('.')
}

/**
 * A ROM with no real banner still decodes to an icon, of one single color or nothing at all.
 */
fun isBlankIcon(pixels: IntArray): Boolean {
    return pixels.all { it == pixels[0] }
}

data class AppNameParts(val prefix: String, val accent: String, val suffix: String)

/**
 * Splits "melonadeDS Dev" around the part the app bar colors, keeping whatever a build type added to the name.
 */
fun splitAppName(appName: String): AppNameParts {
    val accentStart = appName.indexOf(APP_NAME_ACCENT)
    if (accentStart < 0) {
        return AppNameParts(appName, "", "")
    }
    return AppNameParts(appName.substring(0, accentStart), APP_NAME_ACCENT, appName.substring(accentStart + APP_NAME_ACCENT.length))
}

private const val APP_NAME_ACCENT = "DS"
