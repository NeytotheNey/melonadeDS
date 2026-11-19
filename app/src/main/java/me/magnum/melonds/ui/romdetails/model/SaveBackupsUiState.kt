package me.magnum.melonds.ui.romdetails.model

import java.time.LocalDateTime

/**
 * One save backup in the ROM details screen's list
 */
data class SaveBackupUiModel(val fileName: String, val time: LocalDateTime, val sizeBytes: Long)

sealed class SaveBackupsUiState {
    data object Loading : SaveBackupsUiState()
    /** Newest first */
    data class Ready(val backups: List<SaveBackupUiModel>) : SaveBackupsUiState()
    data object Failed : SaveBackupsUiState()
}

/**
 * Results of a restore that the ROM details screen shows
 */
enum class SaveBackupEvent {
    RESTORED,
    GAME_RUNNING,
    BACKUP_FAILED,
    FAILED,
}
