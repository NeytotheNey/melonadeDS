package me.magnum.melonds.common.workers

import android.content.Context
import androidx.hilt.work.HiltWorker
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import dagger.assisted.Assisted
import dagger.assisted.AssistedInject
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import me.magnum.melonds.database.daos.RAGameMetadataDao
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity
import me.magnum.melonds.domain.model.RomScanningStatus
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.repositories.RetroAchievementsRepository
import me.magnum.melonds.domain.repositories.RomsRepository
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtKind
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtStore
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.ArtAction
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.ArtDownloadTracker
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.FetchDecision
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.NotFoundHoldBack
import me.magnum.rcheevosapi.RAWebApi
import me.magnum.rcheevosapi.RAWebApiKeyStore
import me.magnum.rcheevosapi.model.RAGameId

/**
 * Fills the local metadata cache: one `API_GetGame` call for each scanned game with no usable row, and the art of those games. Everything is
 * stored as it arrives and art follows after every batch of games, so a run that gets stopped, as WorkManager does after ten minutes, loses nothing
 * and the next run picks up where this one left off.
 */
@HiltWorker
class RomMetadataSyncWorker @AssistedInject constructor(
    @Assisted appContext: Context,
    @Assisted workerParams: WorkerParameters,
    private val keyStore: RAWebApiKeyStore,
    private val romsRepository: RomsRepository,
    private val retroAchievementsRepository: RetroAchievementsRepository,
    private val raWebApi: RAWebApi,
    private val gameMetadataDao: RAGameMetadataDao,
    private val artStore: RetroAchievementsArtStore,
) : CoroutineWorker(appContext, workerParams) {

    private companion object {
        // RetroAchievements publishes no rate limit but asks clients not to hammer it, and this runs in the background anyway
        const val DELAY_BETWEEN_API_CALLS_MS = 500L
        const val MAX_CONCURRENT_ART_DOWNLOADS = 2
        const val GAMES_PER_ART_BATCH = 20
        // After this, a later scan or a manual action starts over, instead of backing off for hours on end
        const val MAX_RUN_ATTEMPTS = 5
    }

    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        if (keyStore.getApiKey() == null) {
            return@withContext Result.success()
        }

        val gameHashes = getScannedRoms().map { it.retroAchievementsHash }.filter { it.isNotBlank() }.distinct()
        val gameIds = retroAchievementsRepository.getGameIds(gameHashes).values.map { it.id }
        val artTracker = ArtDownloadTracker()
        val notFounds = NotFoundHoldBack()

        // First the art an earlier run left missing, which needs no API calls
        downloadMissingArt(gameIds, artTracker)

        // Read after that, since that can clear image paths, which storing these rows again later would otherwise bring back
        val existingRows = gameMetadataDao.getAllMetadata().associateBy { it.gameId }

        RomMetadataSyncPlanner.gamesToFetch(gameIds, existingRows).chunked(GAMES_PER_ART_BATCH).forEach { batch ->
            val stopDecision = fetchMetadata(batch, existingRows, notFounds)
            // Even after a stop, since being told to slow down by the API says nothing about the media host, and these rows are already stored.
            // If the connection is what's gone, the first few downloads find out, and if WorkManager stopped this run the coroutine is already cancelled
            downloadMissingArt(batch - notFounds.heldGameIds(), artTracker)

            when (stopDecision) {
                is FetchDecision.StopRun -> return@withContext Result.success()
                is FetchDecision.StopAndRetryLater -> return@withContext retryUnlessOutOfAttempts()
                else -> {}
            }
        }

        // Every game got its turn, so what's still held was never contradicted, and this few can really be missing from RetroAchievements
        notFounds.release().forEach { gameMetadataDao.upsertMetadata(it) }

        if (artTracker.shouldRetry) retryUnlessOutOfAttempts() else Result.success()
    }

    private fun retryUnlessOutOfAttempts(): Result {
        return if (runAttemptCount + 1 < MAX_RUN_ATTEMPTS) Result.retry() else Result.success()
    }

    /**
     * The ROMs as of the end of the scan in progress, if any. Asking for them starts the first scan if this process hasn't had one yet.
     */
    private suspend fun getScannedRoms(): List<Rom> {
        romsRepository.getRoms().first()
        romsRepository.getRomScanningStatus().first { it == RomScanningStatus.NOT_SCANNING }
        return romsRepository.getRoms().first()
    }

    /**
     * @return The decision that ended the fetching early, or null if every game got its turn
     */
    private suspend fun fetchMetadata(gameIds: List<Long>, existingRows: Map<Long, RAGameMetadataEntity>, notFounds: NotFoundHoldBack): FetchDecision? {
        gameIds.forEach { gameId ->
            val previousRow = existingRows[gameId]
            val result = raWebApi.getGame(RAGameId(gameId))
            when (val decision = RomMetadataSyncPlanner.decideFetchResult(gameId, result, previousRow, System.currentTimeMillis())) {
                is FetchDecision.Store -> when (decision.row.fetchStatus) {
                    RAGameMetadataEntity.STATUS_NOT_FOUND -> {
                        if (notFounds.hold(decision.row)) {
                            // They could really be missing, which only an answer about a game that exists can tell
                            delay(DELAY_BETWEEN_API_CALLS_MS)
                            if (RomMetadataSyncPlanner.vouchesForApi(raWebApi.getControlGame())) {
                                notFounds.release().forEach { gameMetadataDao.upsertMetadata(it) }
                            } else {
                                notFounds.releaseAsFailed().forEach { gameMetadataDao.upsertMetadata(it) }
                                return FetchDecision.StopAndRetryLater()
                            }
                        }
                    }
                    RAGameMetadataEntity.STATUS_OK -> {
                        // A real answer shows the API is working, so the ones before it were real too
                        notFounds.release().forEach { gameMetadataDao.upsertMetadata(it) }
                        if (RomMetadataSyncPlanner.isArtOutdated(previousRow, decision.row)) {
                            artStore.deleteArt(gameId)
                        }
                        gameMetadataDao.upsertMetadata(decision.row)
                    }
                    else -> gameMetadataDao.upsertMetadata(decision.row)
                }
                is FetchDecision.StopAndRetryLater -> {
                    decision.failedRow?.let { gameMetadataDao.upsertMetadata(it) }
                    return decision
                }
                is FetchDecision.StopRun -> return decision
            }
            delay(DELAY_BETWEEN_API_CALLS_MS)
        }
        return null
    }

    private suspend fun downloadMissingArt(gameIds: List<Long>, tracker: ArtDownloadTracker) {
        if (!artStore.isAvailable() || tracker.isStopped) {
            return
        }

        val rows = gameMetadataDao.getAllMetadata().associateBy { it.gameId }
        val gamesMissingArt = RomMetadataSyncPlanner.artToDownload(gameIds, rows, artStore::exists).mapTo(HashSet()) { it.gameId }
        val gamesMissingTitle = RomMetadataSyncPlanner.titleArtToDownload(gameIds, rows, artStore::exists).mapTo(HashSet()) { it.gameId }
        val semaphore = Semaphore(MAX_CONCURRENT_ART_DOWNLOADS)
        coroutineScope {
            // Shuffled so that images that always fail, which are all that stays on the list from one run to the next, don't line up and stop every run
            (gamesMissingArt + gamesMissingTitle).shuffled().map { gameId ->
                async {
                    semaphore.withPermit {
                        // One at a time for a game, since each works from the row the last one left
                        var row = rows.getValue(gameId)
                        if (gameId in gamesMissingArt && !tracker.isStopped) {
                            row = downloadArt(row, tracker)
                        }
                        if (gameId in gamesMissingTitle && !tracker.isStopped) {
                            downloadTitleArt(row, tracker)
                        }
                    }
                }
            }.awaitAll()
        }
    }

    /**
     * Gets the game's art, going down its images until one works.
     *
     * @return The game's row as it is afterward
     */
    private suspend fun downloadArt(firstRow: RAGameMetadataEntity, tracker: ArtDownloadTracker): RAGameMetadataEntity {
        var row = firstRow
        while (true) {
            val art = RomMetadataSyncPlanner.chooseArt(row) ?: return row
            if (artStore.exists(art.gameId, art.kind)) {
                return row
            }

            val clearCount = artStore.clearCount
            val result = raWebApi.downloadImage(art.imagePath)
            val saveResult = result.getOrNull()?.let { artStore.save(art.gameId, art.kind, it, clearCount) }
            when (tracker.actionFor(RomMetadataSyncPlanner.decideArtOutcome(result.exceptionOrNull(), saveResult))) {
                ArtAction.DONE, ArtAction.STOP_DOWNLOADS -> return row
                ArtAction.TRY_NEXT_KIND -> {
                    row = RomMetadataSyncPlanner.withoutImage(row, art.kind)
                    gameMetadataDao.upsertMetadata(row)
                }
            }
        }
    }

    /**
     * Gets the title screen, which the game may already have as its art. There's no next kind for this one: if it fails for good only its own path goes, and
     * the game's chosen art doesn't care.
     */
    private suspend fun downloadTitleArt(row: RAGameMetadataEntity, tracker: ArtDownloadTracker) {
        val imagePath = row.imageTitle ?: return
        if (artStore.exists(row.gameId, RetroAchievementsArtKind.TITLE)) {
            return
        }

        val clearCount = artStore.clearCount
        val result = raWebApi.downloadImage(imagePath)
        val saveResult = result.getOrNull()?.let { artStore.save(row.gameId, RetroAchievementsArtKind.TITLE, it, clearCount) }
        if (tracker.actionFor(RomMetadataSyncPlanner.decideArtOutcome(result.exceptionOrNull(), saveResult)) == ArtAction.TRY_NEXT_KIND) {
            gameMetadataDao.upsertMetadata(RomMetadataSyncPlanner.withoutImage(row, RetroAchievementsArtKind.TITLE))
        }
    }
}
