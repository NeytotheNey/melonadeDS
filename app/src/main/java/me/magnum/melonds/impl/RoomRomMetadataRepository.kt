package me.magnum.melonds.impl

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.conflate
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.transform
import me.magnum.melonds.common.suspendRunCatching
import me.magnum.melonds.database.daos.RAGameMetadataDao
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import me.magnum.melonds.domain.repositories.RomMetadataRepository
import me.magnum.melonds.domain.repositories.RomsRepository
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtKind
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtStore
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner
import me.magnum.melonds.impl.retroachievements.buildRomMetadataSnapshot

class RoomRomMetadataRepository(
    private val gameMetadataDao: RAGameMetadataDao,
    private val artStore: RetroAchievementsArtStore,
    romsRepository: RomsRepository,
    scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
) : RomMetadataRepository {

    private companion object {
        // A sync writes a row every second or so, which a debounce shorter than that would never settle on
        const val MIN_MS_BETWEEN_REBUILDS = 1000L
        const val MAX_HASHES_PER_QUERY = 500
    }

    // Playing a game changes the ROM list but not its hashes. The first scan can take long, and the metadata shouldn't wait for it
    private val romHashes: Flow<Set<String>> = romsRepository.getRoms()
        .map { roms -> roms.mapNotNull { rom -> rom.retroAchievementsHash.takeIf { it.isNotBlank() } }.toSet() }
        .distinctUntilChanged()
        .onStart { emit(emptySet()) }

    override val romMetadata: StateFlow<RomMetadataSnapshot> = combine(gameMetadataDao.observeSourceTables(), artStore.version, romHashes) { _, _, hashes -> hashes }
        .mapThrottled(MIN_MS_BETWEEN_REBUILDS) { hashes ->
            // Tiles can always fall back on the banner, so a database problem isn't worth more than keeping what's there
            suspendRunCatching { loadSnapshot(hashes) }.getOrNull()
        }
        .filterNotNull()
        .catch { }
        .stateIn(scope, SharingStarted.WhileSubscribed(), RomMetadataSnapshot.EMPTY)

    override suspend fun markMetadataForRefresh() {
        gameMetadataDao.markForRefetch(RomMetadataSyncPlanner.refreshCutoff(System.currentTimeMillis()))
    }

    override fun clearDownloadedArt() {
        artStore.clear()
    }

    private suspend fun loadSnapshot(romHashes: Set<String>): RomMetadataSnapshot {
        val gameHashes = gameMetadataDao.getHashesOfKnownGames()
        val gameIds = gameHashes.map { it.gameId }
        return buildRomMetadataSnapshot(
            gameHashes = gameHashes,
            games = gameMetadataDao.getGamesWithoutRichPresence(),
            gameMetadata = gameMetadataDao.getAllMetadata(),
            achievementCounts = gameMetadataDao.getAchievementCounts().associateBy { it.gameId },
            artFiles = artStore.findArt(gameIds),
            titleArtFiles = artStore.findArt(gameIds, RetroAchievementsArtKind.TITLE),
            unmatchedRomHashes = findUnmatchedRomHashes(romHashes),
        )
    }

    private suspend fun findUnmatchedRomHashes(romHashes: Set<String>): Set<String> {
        if (romHashes.isEmpty() || !gameMetadataDao.hasHashLibrary()) {
            return emptySet()
        }
        val matched = romHashes.chunked(MAX_HASHES_PER_QUERY).flatMap { gameMetadataDao.findHashesInLibrary(it) }
        return romHashes - matched.toSet()
    }
}

/**
 * Handles the first value right away and then at most one every [intervalMs], always ending on the latest. Unlike a debounce this also makes
 * progress when the values keep coming.
 */
internal fun <T, R> Flow<T>.mapThrottled(intervalMs: Long, transform: suspend (T) -> R): Flow<R> {
    return conflate().transform { value ->
        emit(transform(value))
        delay(intervalMs)
    }
}
