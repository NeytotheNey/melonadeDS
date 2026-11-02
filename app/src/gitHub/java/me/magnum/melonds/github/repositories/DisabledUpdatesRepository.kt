package me.magnum.melonds.github.repositories

import me.magnum.melonds.domain.model.appupdate.AppUpdate
import me.magnum.melonds.domain.repositories.UpdatesRepository

/**
 * Update checking is turned off in this fork because the GitHub repositories point at upstream's releases, so this never
 * reports an update and never contacts GitHub.
 */
class DisabledUpdatesRepository : UpdatesRepository {
    override suspend fun checkNewUpdate(): Result<AppUpdate?> {
        return Result.success(null)
    }

    override fun skipUpdate(update: AppUpdate) {
        // Do nothing, since update checking is disabled
    }

    override fun notifyUpdateDownloaded(update: AppUpdate) {
        // Do nothing
    }
}
