package me.magnum.melonds.impl.retroachievements

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import me.magnum.melonds.common.workers.RomMetadataSyncWorker
import me.magnum.rcheevosapi.RAWebApiKeyStore
import java.util.concurrent.TimeUnit

/**
 * Starts [RomMetadataSyncWorker] when there's something new for it to do. Without a key, or while a game is running, nothing gets scheduled at all.
 */
class RomMetadataSyncScheduler(
    private val context: Context,
    private val keyStore: RAWebApiKeyStore,
) {

    private companion object {
        const val WORK_NAME = "rom_metadata_sync_worker"
        const val BACKOFF_MINUTES = 15L
    }

    // Also held around each WorkManager call, whose operations run in the order they're made, so a late scan can't enqueue after a game's cancel
    private val gate = RomMetadataSyncGate()

    /**
     * Stops any sync for as long as the game runs. Saved rows and art stay, so the next sync only does what this one didn't get to.
     */
    fun onGameStarted() {
        synchronized(gate) {
            if (gate.onGameStarted(keyStore.getApiKey() != null)) {
                cancelSync()
            }
        }
    }

    /**
     * Makes up for the sync that was cancelled and for anything that asked for one during the game.
     */
    fun onGameEnded() {
        synchronized(gate) {
            if (gate.onGameEnded(keyStore.getApiKey() != null)) {
                enqueue(ExistingWorkPolicy.KEEP)
            }
        }
    }

    /**
     * A scan can add games. A sync that's already waiting or running will see them or the next scan will, so it stays as it is. Scans happen on
     * every launch, and this way they can't pile up while waiting for an unmetered network.
     */
    fun onRomScanFinished() {
        schedule(ExistingWorkPolicy.KEEP)
    }

    /**
     * Whatever is running was started with the old key or before the refresh, so it starts over. Finished games are already stored.
     */
    fun onKeySavedOrRefreshRequested() {
        schedule(ExistingWorkPolicy.REPLACE)
    }

    fun cancelSync() {
        WorkManager.getInstance(context).cancelUniqueWork(WORK_NAME)
    }

    private fun schedule(policy: ExistingWorkPolicy) {
        synchronized(gate) {
            if (gate.canEnqueue(keyStore.getApiKey() != null)) {
                enqueue(policy)
            }
        }
    }

    private fun enqueue(policy: ExistingWorkPolicy) {
        val constraints = Constraints.Builder()
            .setRequiredNetworkType(NetworkType.UNMETERED)
            .build()

        val workRequest = OneTimeWorkRequestBuilder<RomMetadataSyncWorker>()
            .setConstraints(constraints)
            .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, BACKOFF_MINUTES, TimeUnit.MINUTES)
            .build()

        WorkManager.getInstance(context).enqueueUniqueWork(WORK_NAME, policy, workRequest)
    }
}
