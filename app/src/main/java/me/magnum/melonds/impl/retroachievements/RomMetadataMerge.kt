package me.magnum.melonds.impl.retroachievements

import me.magnum.melonds.database.entities.retroachievements.RAGameAchievementCounts
import me.magnum.melonds.database.entities.retroachievements.RAGameEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameHashEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.domain.model.rom.RomMetadataFetchStatus
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import java.io.File
import java.net.MalformedURLException
import java.net.URL

private val RELEASE_YEAR = Regex("^(\\d{4})(?:\\D.*)?$")

/**
 * Layers what's known about a ROM's game over its [banner]. Each source can be missing or have blanks: RetroAchievements wins where it has a
 * value, the banner's publisher fills in where it doesn't, and everything else stays null so the card shows less.
 */
fun mergeRomMetadata(
    banner: RomLibraryMetadata,
    game: RAGameEntity?,
    gameMetadata: RAGameMetadataEntity?,
    artFile: File?,
    achievements: RAGameAchievementCounts?,
    titleArtFile: File? = null,
): RomLibraryMetadata {
    return banner.copy(
        publisher = gameMetadata?.publisher.orNullIfBlank() ?: banner.publisher,
        developer = gameMetadata?.developer.orNullIfBlank(),
        genre = gameMetadata?.genre.orNullIfBlank(),
        releaseYear = gameMetadata?.released?.let(::parseReleaseYear),
        title = game?.title.orNullIfBlank(),
        artFile = artFile,
        gameIconUrl = game?.icon?.takeIf(::isValidUrl),
        hasAchievements = achievements != null,
        artModifiedAt = artFile?.lastModified(),
        released = gameMetadata?.released.orNullIfBlank(),
        releasedGranularity = gameMetadata?.releasedGranularity.orNullIfBlank(),
        titleArtFile = titleArtFile,
        achievementsInSet = achievements?.total,
        achievementsEarned = achievements?.takeIf { it.isUserDataKnown }?.earned,
        fetchStatus = gameMetadata?.toFetchStatus(),
        fetchedAt = gameMetadata?.fetchedAt,
    )
}

/**
 * Merges everything once, per game, so that a tile only has to look its result up. The banner comes in at the lookup, since it belongs to the ROM.
 */
fun buildRomMetadataSnapshot(
    gameHashes: List<RAGameHashEntity>,
    games: List<RAGameEntity>,
    gameMetadata: List<RAGameMetadataEntity>,
    achievementCounts: Map<Long, RAGameAchievementCounts>,
    artFiles: Map<Long, File>,
    titleArtFiles: Map<Long, File> = emptyMap(),
    unmatchedRomHashes: Set<String> = emptySet(),
): RomMetadataSnapshot {
    val gamesById = games.associateBy { it.gameId }
    val metadataById = gameMetadata.associateBy { it.gameId }
    val mergedById = HashMap<Long, RomLibraryMetadata>()

    val metadataByGameHash = gameHashes.associate { gameHash ->
        val gameId = gameHash.gameId
        gameHash.gameHash to mergedById.getOrPut(gameId) {
            mergeRomMetadata(RomLibraryMetadata(), gamesById[gameId], metadataById[gameId], artFiles[gameId], achievementCounts[gameId], titleArtFiles[gameId])
        }
    }
    return RomMetadataSnapshot(metadataByGameHash, unmatchedRomHashes)
}

/**
 * The year of a release date, which comes as "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS". Years come first whatever the granularity, and anything else
 * isn't a date we understand.
 */
fun parseReleaseYear(released: String): Int? {
    return RELEASE_YEAR.matchEntire(released.trim())?.groupValues?.get(1)?.toInt()
}

private fun RAGameMetadataEntity.toFetchStatus(): RomMetadataFetchStatus {
    return when (fetchStatus) {
        RAGameMetadataEntity.STATUS_OK -> RomMetadataFetchStatus.OK
        RAGameMetadataEntity.STATUS_NOT_FOUND -> RomMetadataFetchStatus.NOT_FOUND
        else -> RomMetadataFetchStatus.FAILED
    }
}

private fun isValidUrl(url: String): Boolean {
    return try {
        URL(url)
        true
    } catch (_: MalformedURLException) {
        false
    }
}

private fun String?.orNullIfBlank(): String? = this?.trim()?.takeIf { it.isNotEmpty() }
