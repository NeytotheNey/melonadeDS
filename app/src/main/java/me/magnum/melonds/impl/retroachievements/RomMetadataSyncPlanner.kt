package me.magnum.melonds.impl.retroachievements

import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity
import me.magnum.rcheevosapi.exception.RAWebApiException
import me.magnum.rcheevosapi.model.RAGameDetails
import kotlin.time.Duration.Companion.days

/**
 * What a metadata sync should do, apart from doing it, so the rules can be tested without WorkManager, a database or a network.
 */
object RomMetadataSyncPlanner {

    private val MIN_AGE_TO_REFETCH = 14.days
    private const val MAX_CONSECUTIVE_ART_FAILURES = 3
    private const val MAX_CONSECUTIVE_NOT_FOUNDS = 3

    sealed interface FetchDecision {
        /** Store this row and carry on with the next game. */
        data class Store(val row: RAGameMetadataEntity) : FetchDecision
        /**
         * Stop calling the API for now and try again later, because more calls would fail the same way.
         *
         * @param failedRow What to store for the game that hit this, if it could be that game's own fault. Being marked puts it behind the others next
         * time, so one game the server chokes on can't hold up the rest of the library on every run.
         */
        data class StopAndRetryLater(val failedRow: RAGameMetadataEntity? = null) : FetchDecision
        /** Stop for good. Only something the user does, such as saving another key, can change the outcome. */
        data object StopRun : FetchDecision
    }

    data class ArtDownload(val gameId: Long, val kind: RetroAchievementsArtKind, val imagePath: String)

    /** What a download attempt says about the image and about the media host. */
    enum class ArtOutcome {
        SAVED,
        /** Says nothing either way, such as a full disk. The missing file gets another try next run. */
        INCONCLUSIVE,
        /** The host answered clearly that this image can't be had. */
        IMAGE_UNAVAILABLE,
        /** The host sent something that isn't an image. Bad data usually, but a block or a challenge page can look the same. */
        NOT_AN_IMAGE,
        /** Might be this one image or might be the whole host, which only more failures in a row can tell. */
        HOST_PROBLEM,
        /** The host is asking for a break, which is about every image. */
        RATE_LIMITED,
    }

    enum class ArtAction {
        /** Nothing more to do for this game now. A file left missing gets another try next run. */
        DONE,
        /** Forget this image and use the game's next one. */
        TRY_NEXT_KIND,
        STOP_DOWNLOADS,
    }

    /**
     * Turns outcomes into actions over a whole run, since one image proves nothing about the host while several in a row do. Stopping is what keeps a
     * CDN block or a blanket 404 from clearing the art paths of the whole library. Safe to use from several downloads at once.
     */
    class ArtDownloadTracker {
        private var consecutiveFailures = 0
        private var hasSavedAny = false

        @Volatile
        var isStopped = false
            private set

        /**
         * Whether stopping is worth another run soon. With nothing saved at all that run would most likely go the same way, so the next scan or manual
         * action can try again instead.
         */
        val shouldRetry: Boolean
            @Synchronized get() = isStopped && hasSavedAny

        @Synchronized
        fun actionFor(outcome: ArtOutcome): ArtAction {
            if (outcome == ArtOutcome.SAVED) {
                hasSavedAny = true
            }
            if (isStopped) {
                return ArtAction.STOP_DOWNLOADS
            }

            return when (outcome) {
                ArtOutcome.SAVED -> {
                    consecutiveFailures = 0
                    ArtAction.DONE
                }
                ArtOutcome.INCONCLUSIVE -> ArtAction.DONE
                ArtOutcome.IMAGE_UNAVAILABLE, ArtOutcome.NOT_AN_IMAGE -> if (countFailure()) ArtAction.STOP_DOWNLOADS else ArtAction.TRY_NEXT_KIND
                ArtOutcome.HOST_PROBLEM -> if (countFailure()) ArtAction.STOP_DOWNLOADS else ArtAction.DONE
                ArtOutcome.RATE_LIMITED -> {
                    isStopped = true
                    ArtAction.STOP_DOWNLOADS
                }
            }
        }

        private fun countFailure(): Boolean {
            consecutiveFailures++
            isStopped = consecutiveFailures >= MAX_CONSECUTIVE_ART_FAILURES
            return isStopped
        }
    }

    /**
     * Holds on to "not found" answers until something shows the API is working. Game ids come from RetroAchievements' own hash library, so several in a
     * row almost certainly mean a bad response rather than missing games, and "not found" isn't retried by itself.
     */
    class NotFoundHoldBack {
        private val heldRows = mutableListOf<RAGameMetadataEntity>()

        /**
         * @return True if that makes too many in a row
         */
        fun hold(row: RAGameMetadataEntity): Boolean {
            heldRows += row
            return heldRows.size >= MAX_CONSECUTIVE_NOT_FOUNDS
        }

        /**
         * Art can't be downloaded for these for now, since a path cleared on the way would come back when their held row gets stored.
         */
        fun heldGameIds(): Set<Long> = heldRows.mapTo(HashSet()) { it.gameId }

        /**
         * The rows to store as they are, now that the API has been seen working or the run got through all its games.
         */
        fun release(): List<RAGameMetadataEntity> {
            val rows = heldRows.toList()
            heldRows.clear()
            return rows
        }

        /**
         * The rows to store when there were too many: as failed, not as "not found", which puts them behind the other games so the same few can't stop
         * every run before it gets to the rest.
         */
        fun releaseAsFailed(): List<RAGameMetadataEntity> {
            return release().map { it.copy(fetchStatus = RAGameMetadataEntity.STATUS_FAILED) }
        }
    }

    /**
     * Whether the answer to the control call, which asks for a game that exists, shows that the API is giving real answers.
     */
    fun vouchesForApi(controlResult: Result<RAGameDetails?>): Boolean = controlResult.getOrNull() != null

    /**
     * Games with no row yet, then those whose last fetch failed, longest ago first. A game RetroAchievements didn't find is left alone until a manual
     * refresh.
     */
    fun gamesToFetch(gameIds: Collection<Long>, existingRows: Map<Long, RAGameMetadataEntity>): List<Long> {
        return gameIds.distinct()
            .filter { gameId ->
                val row = existingRows[gameId]
                row == null || row.fetchStatus == RAGameMetadataEntity.STATUS_FAILED
            }
            .sortedWith(compareBy({ existingRows[it]?.fetchedAt ?: -1L }, { it }))
    }

    fun decideFetchResult(gameId: Long, result: Result<RAGameDetails?>, previousRow: RAGameMetadataEntity?, now: Long): FetchDecision {
        result.onSuccess { details ->
            return if (details != null) {
                FetchDecision.Store(details.toRow(now))
            } else {
                FetchDecision.Store(previousRow.withStatus(gameId, RAGameMetadataEntity.STATUS_NOT_FOUND, now))
            }
        }

        val failedRow = previousRow.withStatus(gameId, RAGameMetadataEntity.STATUS_FAILED, now)
        return when (val error = result.exceptionOrNull()) {
            is RAWebApiException.InvalidKey, is RAWebApiException.MissingKey -> FetchDecision.StopRun
            is RAWebApiException.RateLimited -> FetchDecision.StopAndRetryLater()
            // With a lost connection the game gets marked too, in case its own response is what keeps dying. In a real outage that costs nothing
            is RAWebApiException.NetworkError, is RAWebApiException.UnexpectedResponse -> FetchDecision.StopAndRetryLater(failedRow)
            is RAWebApiException.HttpError -> {
                // Only a 4xx is about this request in particular, and an unknown game isn't a 404 but a 200 with nothing in it. Anything else, such as a 503,
                // would hit every game the same way
                if (error.code in 400..499 && error.code != 404) FetchDecision.Store(failedRow) else FetchDecision.StopAndRetryLater(failedRow)
            }
            else -> FetchDecision.Store(failedRow)
        }
    }

    /**
     * What a finished download attempt says.
     *
     * @param error Why the download failed, if it did
     * @param saveResult What became of the downloaded image, if there was one
     */
    fun decideArtOutcome(error: Throwable?, saveResult: RetroAchievementsArtStore.SaveResult?): ArtOutcome {
        return when (error) {
            null -> when (saveResult) {
                RetroAchievementsArtStore.SaveResult.SAVED -> ArtOutcome.SAVED
                RetroAchievementsArtStore.SaveResult.NOT_AN_IMAGE -> ArtOutcome.NOT_AN_IMAGE
                else -> ArtOutcome.INCONCLUSIVE
            }
            is RAWebApiException.ImageTooLarge, is RAWebApiException.InvalidImagePath -> ArtOutcome.IMAGE_UNAVAILABLE
            is RAWebApiException.HttpError -> when (error.code) {
                404, 410 -> ArtOutcome.IMAGE_UNAVAILABLE
                429 -> ArtOutcome.RATE_LIMITED
                // A 403 can be a block, and a redirect is never followed, so it would otherwise just fail quietly on every run
                else -> ArtOutcome.HOST_PROBLEM
            }
            is RAWebApiException.NetworkError, is RAWebApiException.UnexpectedResponse -> ArtOutcome.HOST_PROBLEM
            else -> ArtOutcome.INCONCLUSIVE
        }
    }

    /**
     * The row without an image that will never work, which is how a failure is remembered: [chooseArt] moves on to the next kind and no sync tries it
     * again. Fetching the game again brings the path back for another try, and clearing [RAGameMetadataEntity.fetchedAt] makes sure the next manual
     * refresh does that however recently the game was fetched, in case the failure wasn't the image's fault after all.
     */
    fun withoutImage(row: RAGameMetadataEntity, kind: RetroAchievementsArtKind): RAGameMetadataEntity {
        val forgetful = row.copy(fetchedAt = 0)
        return when (kind) {
            RetroAchievementsArtKind.BOX_ART -> forgetful.copy(imageBoxArt = null)
            RetroAchievementsArtKind.TITLE -> forgetful.copy(imageTitle = null)
            RetroAchievementsArtKind.ICON -> forgetful.copy(imageIcon = null)
        }
    }

    /**
     * The one image a game's tile uses, with the same preference as [RetroAchievementsArtStore.findArt].
     */
    fun chooseArt(row: RAGameMetadataEntity): ArtDownload? {
        return row.imageBoxArt?.let { ArtDownload(row.gameId, RetroAchievementsArtKind.BOX_ART, it) }
            ?: row.imageTitle?.let { ArtDownload(row.gameId, RetroAchievementsArtKind.TITLE, it) }
            ?: row.imageIcon?.let { ArtDownload(row.gameId, RetroAchievementsArtKind.ICON, it) }
    }

    /**
     * The chosen art of each of [gameIds] that isn't on disk. This also picks art back up after "Clear downloaded art" or the system clearing
     * the cache folder, without any API call.
     */
    fun artToDownload(
        gameIds: Collection<Long>,
        rows: Map<Long, RAGameMetadataEntity>,
        artExists: (gameId: Long, kind: RetroAchievementsArtKind) -> Boolean,
    ): List<ArtDownload> {
        return gameIds.distinct()
            .mapNotNull { gameId -> rows[gameId]?.let(::chooseArt) }
            .filterNot { artExists(it.gameId, it.kind) }
    }

    /**
     * The title screens of [gameIds] that aren't on disk, which the mosaic's wide tiles use on top of a game's chosen art. Like that art they come from the
     * game's row, so these cost no API calls.
     */
    fun titleArtToDownload(
        gameIds: Collection<Long>,
        rows: Map<Long, RAGameMetadataEntity>,
        artExists: (gameId: Long, kind: RetroAchievementsArtKind) -> Boolean,
    ): List<ArtDownload> {
        return gameIds.distinct()
            .mapNotNull { gameId -> rows[gameId]?.imageTitle?.let { ArtDownload(gameId, RetroAchievementsArtKind.TITLE, it) } }
            .filterNot { artExists(it.gameId, it.kind) }
    }

    /**
     * Art on disk is skipped by file name, so a game whose image changed would otherwise keep the old one. A title screen cleared after failing has nothing
     * on disk to replace, so that coming back says nothing about the rest of the game's art.
     */
    fun isArtOutdated(previousRow: RAGameMetadataEntity?, newRow: RAGameMetadataEntity): Boolean {
        previousRow ?: return false
        val isTitleOutdated = previousRow.imageTitle != null && previousRow.imageTitle != newRow.imageTitle
        val previousArt = chooseArt(previousRow) ?: return isTitleOutdated
        return isTitleOutdated || previousArt.imagePath != chooseArt(newRow)?.imagePath
    }

    /**
     * Rows fetched at or before this time get fetched again by a manual refresh, found or not. Newer ones stay, so tapping refresh repeatedly can't
     * turn into repeated bulk calls. Rows whose fetch failed aren't affected, since every sync retries those anyway.
     */
    fun refreshCutoff(now: Long): Long = now - MIN_AGE_TO_REFETCH.inWholeMilliseconds

    private fun RAGameDetails.toRow(now: Long): RAGameMetadataEntity {
        return RAGameMetadataEntity(
            gameId = id.id,
            publisher = publisher,
            developer = developer,
            genre = genre,
            released = released,
            releasedGranularity = releasedGranularity,
            imageBoxArt = imageBoxArt,
            imageTitle = imageTitle,
            imageIngame = imageIngame,
            imageIcon = imageIcon,
            fetchedAt = now,
            fetchStatus = RAGameMetadataEntity.STATUS_OK,
        )
    }

    // A refresh that fails or no longer finds the game shouldn't take away what the card already shows
    private fun RAGameMetadataEntity?.withStatus(gameId: Long, status: Int, now: Long): RAGameMetadataEntity {
        return this?.copy(fetchStatus = status, fetchedAt = now) ?: RAGameMetadataEntity(
            gameId = gameId,
            publisher = null,
            developer = null,
            genre = null,
            released = null,
            releasedGranularity = null,
            imageBoxArt = null,
            imageTitle = null,
            imageIngame = null,
            imageIcon = null,
            fetchedAt = now,
            fetchStatus = status,
        )
    }
}
