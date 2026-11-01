package me.magnum.melonds.github.services

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow
import me.magnum.melonds.domain.model.DownloadProgress
import me.magnum.melonds.domain.model.appupdate.AppUpdate
import me.magnum.melonds.domain.services.UpdateInstallManager

/**
 * Update checking is turned off in this fork (see DisabledUpdatesRepository), so there is never an update to install.
 */
class DisabledUpdateInstallManager : UpdateInstallManager {
    override fun downloadAndInstallUpdate(update: AppUpdate): Flow<DownloadProgress> {
        return emptyFlow()
    }
}
