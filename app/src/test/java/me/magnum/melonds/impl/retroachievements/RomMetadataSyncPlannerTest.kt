package me.magnum.melonds.impl.retroachievements

import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity.Companion.STATUS_FAILED
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity.Companion.STATUS_NOT_FOUND
import me.magnum.melonds.database.entities.retroachievements.RAGameMetadataEntity.Companion.STATUS_OK
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtKind.BOX_ART
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtKind.ICON
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtKind.TITLE
import me.magnum.melonds.impl.retroachievements.RetroAchievementsArtStore.SaveResult
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.ArtAction
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.ArtDownload
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.ArtDownloadTracker
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.ArtOutcome
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.FetchDecision
import me.magnum.melonds.impl.retroachievements.RomMetadataSyncPlanner.NotFoundHoldBack
import me.magnum.rcheevosapi.exception.RAWebApiException
import me.magnum.rcheevosapi.model.RAGameDetails
import me.magnum.rcheevosapi.model.RAGameId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RomMetadataSyncPlannerTest {

    private val now = 1_000_000_000_000L

    private fun row(
        gameId: Long = 1,
        status: Int = STATUS_OK,
        publisher: String? = "Publisher",
        boxArt: String? = null,
        title: String? = null,
        icon: String? = null,
        fetchedAt: Long = 0,
    ) = RAGameMetadataEntity(gameId, publisher, null, null, null, null, boxArt, title, null, icon, fetchedAt, status)

    private val details = RAGameDetails(
        id = RAGameId(1),
        title = "Game",
        publisher = "New Publisher",
        developer = "Developer",
        genre = null,
        released = "2009",
        releasedGranularity = "year",
        imageIcon = "/Images/1.png",
        imageTitle = null,
        imageIngame = "/Images/3.png",
        imageBoxArt = "/Images/4.png",
    )

    @Test
    fun onlyGamesWithNoRowOrAFailedFetchAreFetched() {
        val existing = mapOf(
            1L to row(1, STATUS_OK),
            2L to row(2, STATUS_NOT_FOUND),
            3L to row(3, STATUS_FAILED),
        )

        assertEquals(listOf(4L, 3L), RomMetadataSyncPlanner.gamesToFetch(listOf(1, 2, 3, 4), existing))
    }

    @Test
    fun aGameThatJustFailedGoesBehindTheOthers() {
        val existing = mapOf(
            1L to row(1, STATUS_FAILED, fetchedAt = 300),
            2L to row(2, STATUS_FAILED, fetchedAt = 100),
            3L to row(3, STATUS_FAILED, fetchedAt = 200),
        )

        assertEquals(listOf(4L, 5L, 2L, 3L, 1L), RomMetadataSyncPlanner.gamesToFetch(listOf(1, 2, 3, 5, 4), existing))
    }

    @Test
    fun twoRomsOfTheSameGameCostOneCall() {
        assertEquals(listOf(5L), RomMetadataSyncPlanner.gamesToFetch(listOf(5, 5), emptyMap()))
    }

    @Test
    fun aFetchedGameIsStoredAsOk() {
        val decision = RomMetadataSyncPlanner.decideFetchResult(1, Result.success(details), row(status = STATUS_FAILED), now) as FetchDecision.Store

        assertEquals(STATUS_OK, decision.row.fetchStatus)
        assertEquals(now, decision.row.fetchedAt)
        assertEquals("New Publisher", decision.row.publisher)
        assertEquals("Developer", decision.row.developer)
        assertEquals("/Images/4.png", decision.row.imageBoxArt)
        assertEquals("/Images/1.png", decision.row.imageIcon)
        assertEquals("/Images/3.png", decision.row.imageIngame)
    }

    @Test
    fun anUnknownGameIsRememberedAsNotFound() {
        val decision = RomMetadataSyncPlanner.decideFetchResult(7, Result.success(null), null, now) as FetchDecision.Store

        assertEquals(7L, decision.row.gameId)
        assertEquals(STATUS_NOT_FOUND, decision.row.fetchStatus)
        assertEquals(now, decision.row.fetchedAt)
        assertNull(decision.row.publisher)
    }

    @Test
    fun aRefreshThatNoLongerFindsTheGameKeepsItsData() {
        val decision = RomMetadataSyncPlanner.decideFetchResult(1, Result.success(null), row(), now) as FetchDecision.Store

        assertEquals(STATUS_NOT_FOUND, decision.row.fetchStatus)
        assertEquals("Publisher", decision.row.publisher)
    }

    @Test
    fun aFailureOfOneGameOnlyMarksThatGame() {
        listOf(RAWebApiException.HttpError(400), RAWebApiException.HttpError(422), RAWebApiException.MalformedResponse()).forEach { error ->
            val decision = RomMetadataSyncPlanner.decideFetchResult(1, Result.failure(error), null, now) as FetchDecision.Store

            assertEquals(STATUS_FAILED, decision.row.fetchStatus)
        }
    }

    @Test
    fun anOutageStopsTheRunAndMarksOnlyTheGameThatHitIt() {
        listOf(
            RAWebApiException.HttpError(404),
            RAWebApiException.HttpError(500),
            RAWebApiException.HttpError(503),
            RAWebApiException.UnexpectedResponse(),
            RAWebApiException.NetworkError("IOException"),
        ).forEach { error ->
            val decision = RomMetadataSyncPlanner.decideFetchResult(7, Result.failure(error), null, now) as FetchDecision.StopAndRetryLater

            assertEquals(7L, decision.failedRow!!.gameId)
            assertEquals(STATUS_FAILED, decision.failedRow!!.fetchStatus)
            assertEquals(now, decision.failedRow!!.fetchedAt)
        }
    }

    @Test
    fun aFailedRefreshKeepsTheDataAndStaysQueued() {
        val decision = RomMetadataSyncPlanner.decideFetchResult(1, Result.failure(RAWebApiException.HttpError(400)), row(), now) as FetchDecision.Store
        val duringOutage = RomMetadataSyncPlanner.decideFetchResult(1, Result.failure(RAWebApiException.HttpError(500)), row(), now) as FetchDecision.StopAndRetryLater

        assertEquals(STATUS_FAILED, decision.row.fetchStatus)
        assertEquals("Publisher", decision.row.publisher)
        assertEquals("Publisher", duringOutage.failedRow!!.publisher)
    }

    @Test
    fun aRateLimitStopsTheRunWithoutTouchingRows() {
        assertEquals(FetchDecision.StopAndRetryLater(), RomMetadataSyncPlanner.decideFetchResult(1, Result.failure(RAWebApiException.RateLimited()), null, now))
    }

    @Test
    fun aGameWhoseResponseKeepsDyingGoesBehindTheOthers() {
        val decision = RomMetadataSyncPlanner.decideFetchResult(1, Result.failure(RAWebApiException.NetworkError("IOException")), null, now) as FetchDecision.StopAndRetryLater
        val existing = mapOf(1L to decision.failedRow!!)

        assertEquals(listOf(2L, 3L, 1L), RomMetadataSyncPlanner.gamesToFetch(listOf(1, 2, 3), existing))
    }

    @Test
    fun aFewNotFoundsAreHeldUntilSomethingVouchesForThem() {
        val notFounds = NotFoundHoldBack()

        assertFalse(notFounds.hold(row(1, STATUS_NOT_FOUND)))
        assertFalse(notFounds.hold(row(2, STATUS_NOT_FOUND)))
        assertEquals(listOf(1L, 2L), notFounds.release().map { it.gameId })
        assertEquals(emptyList<RAGameMetadataEntity>(), notFounds.release())

        // Released, so the count starts over
        assertFalse(notFounds.hold(row(3, STATUS_NOT_FOUND)))
    }

    @Test
    fun heldGamesAreKnownUntilReleased() {
        val notFounds = NotFoundHoldBack()

        notFounds.hold(row(1, STATUS_NOT_FOUND))
        notFounds.hold(row(2, STATUS_NOT_FOUND))
        assertEquals(setOf(1L, 2L), notFounds.heldGameIds())

        notFounds.release()
        assertEquals(emptySet<Long>(), notFounds.heldGameIds())
    }

    @Test
    fun onlyARealGameFromTheControlCallVouchesForTheApi() {
        assertTrue(RomMetadataSyncPlanner.vouchesForApi(Result.success(details)))
        assertFalse(RomMetadataSyncPlanner.vouchesForApi(Result.success(null)))
        assertFalse(RomMetadataSyncPlanner.vouchesForApi(Result.failure(RAWebApiException.UnexpectedResponse())))
    }

    @Test
    fun theHoldBackStartsOverAfterTheControlCallVouchesForThem() {
        val notFounds = NotFoundHoldBack()

        repeat(2) { assertFalse(notFounds.hold(row(it.toLong(), STATUS_NOT_FOUND))) }
        assertTrue(notFounds.hold(row(2, STATUS_NOT_FOUND)))

        assertEquals(listOf(STATUS_NOT_FOUND, STATUS_NOT_FOUND, STATUS_NOT_FOUND), notFounds.release().map { it.fetchStatus })
        assertFalse(notFounds.hold(row(3, STATUS_NOT_FOUND)))
    }

    @Test
    fun tooManyNotFoundsInARowAreStoredAsFailedAndGoBehindTheOthers() {
        val notFounds = NotFoundHoldBack()

        assertFalse(notFounds.hold(row(1, STATUS_NOT_FOUND, fetchedAt = now)))
        assertFalse(notFounds.hold(row(2, STATUS_NOT_FOUND, fetchedAt = now)))
        assertTrue(notFounds.hold(row(3, STATUS_NOT_FOUND, fetchedAt = now)))

        val rows = notFounds.releaseAsFailed()
        assertEquals(listOf(STATUS_FAILED, STATUS_FAILED, STATUS_FAILED), rows.map { it.fetchStatus })
        assertEquals(listOf(4L, 1L, 2L, 3L), RomMetadataSyncPlanner.gamesToFetch(listOf(1, 2, 3, 4), rows.associateBy { it.gameId }))
    }

    @Test
    fun aBadOrMissingKeyStopsTheRunForGood() {
        listOf(RAWebApiException.InvalidKey(), RAWebApiException.MissingKey()).forEach { error ->
            assertEquals(FetchDecision.StopRun, RomMetadataSyncPlanner.decideFetchResult(1, Result.failure(error), null, now))
        }
    }

    @Test
    fun artPrefersBoxArtThenTitleThenIcon() {
        assertEquals(ArtDownload(1, BOX_ART, "/b.png"), RomMetadataSyncPlanner.chooseArt(row(boxArt = "/b.png", title = "/t.png", icon = "/i.png")))
        assertEquals(ArtDownload(1, TITLE, "/t.png"), RomMetadataSyncPlanner.chooseArt(row(title = "/t.png", icon = "/i.png")))
        assertEquals(ArtDownload(1, ICON, "/i.png"), RomMetadataSyncPlanner.chooseArt(row(icon = "/i.png")))
        assertNull(RomMetadataSyncPlanner.chooseArt(row()))
    }

    @Test
    fun artOnDiskOrWithNoImageIsSkipped() {
        val rows = listOf(
            row(1, boxArt = "/1.png"),
            row(2, boxArt = "/2.png"),
            row(3),
            row(4, STATUS_FAILED, title = "/4.png"),
        ).associateBy { it.gameId }

        val downloads = RomMetadataSyncPlanner.artToDownload(listOf(1, 2, 3, 4, 5), rows) { gameId, _ -> gameId == 1L }

        assertEquals(listOf(ArtDownload(2, BOX_ART, "/2.png"), ArtDownload(4, TITLE, "/4.png")), downloads)
    }

    @Test
    fun artIsOutdatedOnlyWhenTheChosenImageChanged() {
        val previous = row(boxArt = "/b.png", icon = "/i.png")

        assertFalse(RomMetadataSyncPlanner.isArtOutdated(previous, row(boxArt = "/b.png", icon = "/other.png")))
        assertTrue(RomMetadataSyncPlanner.isArtOutdated(previous, row(boxArt = "/new.png")))
        assertTrue(RomMetadataSyncPlanner.isArtOutdated(previous, row(icon = "/i.png")))
        assertFalse(RomMetadataSyncPlanner.isArtOutdated(null, row(boxArt = "/b.png")))
        assertFalse(RomMetadataSyncPlanner.isArtOutdated(row(), row(boxArt = "/b.png")))
    }

    @Test
    fun aChangedTitleScreenMakesArtOutdatedButOneComingBackDoesNot() {
        val previous = row(boxArt = "/b.png", title = "/t.png")

        assertFalse(RomMetadataSyncPlanner.isArtOutdated(previous, row(boxArt = "/b.png", title = "/t.png")))
        assertTrue(RomMetadataSyncPlanner.isArtOutdated(previous, row(boxArt = "/b.png", title = "/new.png")))
        assertTrue(RomMetadataSyncPlanner.isArtOutdated(previous, row(boxArt = "/b.png")))
        // Cleared after failing, so there's nothing on disk for the new path to replace, and the box art stays
        assertFalse(RomMetadataSyncPlanner.isArtOutdated(row(boxArt = "/b.png"), row(boxArt = "/b.png", title = "/t.png")))
    }

    @Test
    fun titleScreensAreWantedOnTopOfTheChosenArt() {
        val rows = mapOf(
            1L to row(gameId = 1, boxArt = "/b.png", title = "/t1.png"),
            2L to row(gameId = 2, boxArt = "/b.png"),
            3L to row(gameId = 3, title = "/t3.png"),
            4L to row(gameId = 4, boxArt = "/b.png", title = "/t4.png"),
        )
        val onDisk = setOf(4L)

        val downloads = RomMetadataSyncPlanner.titleArtToDownload(listOf(1, 2, 3, 4, 5, 1), rows) { gameId, kind ->
            gameId in onDisk && kind == RetroAchievementsArtKind.TITLE
        }

        assertEquals(listOf(1L, 3L), downloads.map { it.gameId })
        assertEquals(listOf("/t1.png", "/t3.png"), downloads.map { it.imagePath })
        assertTrue(downloads.all { it.kind == RetroAchievementsArtKind.TITLE })
    }

    @Test
    fun aTitleScreenWithItsPathClearedIsNotAskedForAgainAndLeavesTheMainArtAlone() {
        val row = row(boxArt = "/b.png", title = "/t.png")

        val cleared = RomMetadataSyncPlanner.withoutImage(row, RetroAchievementsArtKind.TITLE)

        assertEquals("/b.png", cleared.imageBoxArt)
        assertEquals(RetroAchievementsArtKind.BOX_ART, RomMetadataSyncPlanner.chooseArt(cleared)?.kind)
        assertTrue(RomMetadataSyncPlanner.titleArtToDownload(listOf(1), mapOf(1L to cleared)) { _, _ -> false }.isEmpty())
    }

    @Test
    fun onlyAClearAnswerSaysTheImageIsUnavailable() {
        listOf(
            RAWebApiException.HttpError(404),
            RAWebApiException.HttpError(410),
            RAWebApiException.ImageTooLarge(),
            RAWebApiException.InvalidImagePath(),
        ).forEach { error ->
            assertEquals(ArtOutcome.IMAGE_UNAVAILABLE, RomMetadataSyncPlanner.decideArtOutcome(error, null))
        }
    }

    @Test
    fun aBlockARedirectOrAnOutageIsAProblemWithTheHost() {
        listOf(
            RAWebApiException.HttpError(403),
            RAWebApiException.HttpError(301),
            RAWebApiException.HttpError(302),
            RAWebApiException.HttpError(503),
            RAWebApiException.NetworkError("IOException"),
            RAWebApiException.UnexpectedResponse(),
        ).forEach { error ->
            assertEquals(ArtOutcome.HOST_PROBLEM, RomMetadataSyncPlanner.decideArtOutcome(error, null))
        }
        assertEquals(ArtOutcome.RATE_LIMITED, RomMetadataSyncPlanner.decideArtOutcome(RAWebApiException.HttpError(429), null))
    }

    @Test
    fun whatBecameOfADownloadedImageIsItsOutcome() {
        assertEquals(ArtOutcome.SAVED, RomMetadataSyncPlanner.decideArtOutcome(null, SaveResult.SAVED))
        assertEquals(ArtOutcome.NOT_AN_IMAGE, RomMetadataSyncPlanner.decideArtOutcome(null, SaveResult.NOT_AN_IMAGE))
        assertEquals(ArtOutcome.INCONCLUSIVE, RomMetadataSyncPlanner.decideArtOutcome(null, SaveResult.DISCARDED))
        assertEquals(ArtOutcome.INCONCLUSIVE, RomMetadataSyncPlanner.decideArtOutcome(null, SaveResult.FAILED))
    }

    @Test
    fun aLoneHostProblemOnlyAffectsThatImage() {
        val tracker = ArtDownloadTracker()

        assertEquals(ArtAction.DONE, tracker.actionFor(ArtOutcome.HOST_PROBLEM))
        assertFalse(tracker.isStopped)
    }

    @Test
    fun threeHostProblemsInARowStopTheDownloads() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.HOST_PROBLEM)
        tracker.actionFor(ArtOutcome.HOST_PROBLEM)

        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.HOST_PROBLEM))
        assertTrue(tracker.isStopped)
    }

    @Test
    fun aSuccessResetsTheCount() {
        val tracker = ArtDownloadTracker()

        repeat(5) {
            assertEquals(ArtAction.DONE, tracker.actionFor(ArtOutcome.HOST_PROBLEM))
            assertEquals(ArtAction.DONE, tracker.actionFor(ArtOutcome.HOST_PROBLEM))
            assertEquals(ArtAction.DONE, tracker.actionFor(ArtOutcome.SAVED))
        }
        assertFalse(tracker.isStopped)
    }

    @Test
    fun neutralOutcomesNeitherResetNorCount() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.HOST_PROBLEM)
        tracker.actionFor(ArtOutcome.INCONCLUSIVE)
        tracker.actionFor(ArtOutcome.HOST_PROBLEM)

        assertFalse(tracker.isStopped)
        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.HOST_PROBLEM))
    }

    @Test
    fun aLone404StillMovesOnToTheNextKind() {
        val tracker = ArtDownloadTracker()

        assertEquals(ArtAction.TRY_NEXT_KIND, tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE))
        assertEquals(ArtAction.TRY_NEXT_KIND, tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE))
        assertFalse(tracker.isStopped)
    }

    @Test
    fun aBlanket404StopsAfterThreeInARowWithoutClearingTheThird() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE)
        tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE)

        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE))
    }

    @Test
    fun aSavedImageBetween404sKeepsThemFromAddingUp() {
        val tracker = ArtDownloadTracker()

        repeat(5) {
            assertEquals(ArtAction.TRY_NEXT_KIND, tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE))
            assertEquals(ArtAction.TRY_NEXT_KIND, tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE))
            assertEquals(ArtAction.DONE, tracker.actionFor(ArtOutcome.SAVED))
        }
        assertFalse(tracker.isStopped)
    }

    @Test
    fun stoppingWithNothingSavedDoesNotAskForARetry() {
        val tracker = ArtDownloadTracker()

        repeat(3) { tracker.actionFor(ArtOutcome.HOST_PROBLEM) }

        assertTrue(tracker.isStopped)
        assertFalse(tracker.shouldRetry)
    }

    @Test
    fun stoppingAfterSavingSomethingAsksForARetry() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.SAVED)
        repeat(3) { tracker.actionFor(ArtOutcome.HOST_PROBLEM) }

        assertTrue(tracker.shouldRetry)
    }

    @Test
    fun anImageSavedAsTheRunStoppedStillCounts() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.RATE_LIMITED)
        assertFalse(tracker.shouldRetry)

        tracker.actionFor(ArtOutcome.SAVED)
        assertTrue(tracker.shouldRetry)
    }

    @Test
    fun aRunThatDidNotStopNeedsNoRetry() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.SAVED)

        assertFalse(tracker.shouldRetry)
    }

    @Test
    fun aLoneNonImageMovesOnButSeveralInARowStopBeforeClearingMore() {
        val tracker = ArtDownloadTracker()

        assertEquals(ArtAction.TRY_NEXT_KIND, tracker.actionFor(ArtOutcome.NOT_AN_IMAGE))
        assertEquals(ArtAction.TRY_NEXT_KIND, tracker.actionFor(ArtOutcome.NOT_AN_IMAGE))
        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.NOT_AN_IMAGE))
    }

    @Test
    fun nonImagesAndHostProblemsCountTogether() {
        val tracker = ArtDownloadTracker()

        tracker.actionFor(ArtOutcome.NOT_AN_IMAGE)
        tracker.actionFor(ArtOutcome.HOST_PROBLEM)

        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.NOT_AN_IMAGE))
    }

    @Test
    fun aRateLimitStopsRightAway() {
        val tracker = ArtDownloadTracker()

        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.RATE_LIMITED))
        assertTrue(tracker.isStopped)
    }

    @Test
    fun nothingIsClearedOnceStopped() {
        val tracker = ArtDownloadTracker()
        tracker.actionFor(ArtOutcome.RATE_LIMITED)

        // Downloads that were in flight when it stopped
        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.NOT_AN_IMAGE))
        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.IMAGE_UNAVAILABLE))
        assertEquals(ArtAction.STOP_DOWNLOADS, tracker.actionFor(ArtOutcome.SAVED))
        assertTrue(tracker.isStopped)
    }

    @Test
    fun forgettingAnImageMakesTheNextRefreshReachTheRow() {
        val row = row(boxArt = "/b.png", title = "/t.png", fetchedAt = now)

        val forgotten = RomMetadataSyncPlanner.withoutImage(row, BOX_ART)

        assertEquals(0L, forgotten.fetchedAt)
        assertEquals(STATUS_OK, forgotten.fetchStatus)
        assertTrue(forgotten.fetchedAt <= RomMetadataSyncPlanner.refreshCutoff(now))
        // But it's not fetched again by itself
        assertEquals(emptyList<Long>(), RomMetadataSyncPlanner.gamesToFetch(listOf(1), mapOf(1L to forgotten)))
    }

    @Test
    fun aForgottenImageLeavesTheNextKindToBeChosen() {
        val row = row(boxArt = "/b.png", title = "/t.png", icon = "/i.png")

        val withoutBoxArt = RomMetadataSyncPlanner.withoutImage(row, BOX_ART)
        assertEquals(ArtDownload(1, TITLE, "/t.png"), RomMetadataSyncPlanner.chooseArt(withoutBoxArt))

        val withoutTitle = RomMetadataSyncPlanner.withoutImage(withoutBoxArt, TITLE)
        assertEquals(ArtDownload(1, ICON, "/i.png"), RomMetadataSyncPlanner.chooseArt(withoutTitle))

        assertNull(RomMetadataSyncPlanner.chooseArt(RomMetadataSyncPlanner.withoutImage(withoutTitle, ICON)))
    }

    @Test
    fun fetchingAgainBringsAForgottenImageBack() {
        val forgotten = row(boxArt = null, icon = "/Images/1.png")

        val decision = RomMetadataSyncPlanner.decideFetchResult(1, Result.success(details), forgotten, now) as FetchDecision.Store

        assertEquals("/Images/4.png", decision.row.imageBoxArt)
    }

    @Test
    fun aCancelledRunIsPickedUpFromTheRowsItStored() {
        // Cancelled after storing game 1, whose art never arrived, and before getting to games 2 and 3
        val stored = mapOf(1L to row(1, boxArt = "/1.png"))

        assertEquals(listOf(2L, 3L), RomMetadataSyncPlanner.gamesToFetch(listOf(1, 2, 3), stored))
        assertEquals(
            listOf(ArtDownload(1, BOX_ART, "/1.png")),
            RomMetadataSyncPlanner.artToDownload(listOf(1, 2, 3), stored) { _, _ -> false },
        )
    }

    @Test
    fun aRefreshOnlyReachesRowsTwoWeeksOld() {
        val twoWeeksMs = 14L * 24 * 60 * 60 * 1000

        assertEquals(now - twoWeeksMs, RomMetadataSyncPlanner.refreshCutoff(now))
    }
}
