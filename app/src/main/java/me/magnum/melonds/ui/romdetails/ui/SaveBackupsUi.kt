package me.magnum.melonds.ui.romdetails.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.dialog.BaseDialog
import me.magnum.melonds.ui.common.component.dialog.DialogButton
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.common.preference.ActionLauncherItem
import me.magnum.melonds.ui.romdetails.model.SaveBackupUiModel
import me.magnum.melonds.ui.romdetails.model.SaveBackupsUiState
import me.magnum.melonds.ui.theme.MelonTheme
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle

/**
 * The "Save backups" row; tapping it lists the game's backups, and tapping one asks before restoring it
 */
@Composable
fun SaveBackupsItem(
    saveBackupsUiState: SaveBackupsUiState,
    onShown: () -> Unit,
    onRestore: (fileName: String) -> Unit,
) {
    var isListShown by rememberSaveable { mutableStateOf(false) }
    // The backup waiting for the restore to be confirmed
    var pendingRestore by rememberSaveable { mutableStateOf<String?>(null) }

    ActionLauncherItem(
        name = stringResource(id = R.string.save_backups),
        value = stringResource(id = R.string.save_backups_summary),
        onLaunchAction = { isListShown = true },
    )

    // Read each time the list opens, also when it comes back open after the screen was recreated
    LaunchedEffect(isListShown) {
        if (isListShown) {
            onShown()
        }
    }

    if (isListShown) {
        SaveBackupsDialog(
            saveBackupsUiState = saveBackupsUiState,
            onBackupClick = { pendingRestore = it.fileName },
            onDismiss = { isListShown = false },
        )
    }

    val pendingBackup = pendingRestore?.let { fileName ->
        (saveBackupsUiState as? SaveBackupsUiState.Ready)?.backups?.find { it.fileName == fileName }
    }
    if (pendingBackup != null) {
        AlertDialog(
            onDismissRequest = { pendingRestore = null },
            title = { Text(text = stringResource(id = R.string.save_backup_restore_title)) },
            text = { Text(text = stringResource(id = R.string.save_backup_restore_message, formatBackupTime(pendingBackup.time))) },
            confirmButton = {
                TextButton(
                    onClick = {
                        pendingRestore = null
                        isListShown = false
                        onRestore(pendingBackup.fileName)
                    },
                ) {
                    Text(text = stringResource(id = R.string.save_backup_restore))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingRestore = null }) {
                    Text(text = stringResource(id = R.string.cancel))
                }
            },
        )
    }
}

@Composable
private fun SaveBackupsDialog(
    saveBackupsUiState: SaveBackupsUiState,
    onBackupClick: (SaveBackupUiModel) -> Unit,
    onDismiss: () -> Unit,
) {
    BaseDialog(
        title = stringResource(id = R.string.save_backups),
        onDismiss = onDismiss,
        content = { padding ->
            when (saveBackupsUiState) {
                SaveBackupsUiState.Loading -> {
                    Box(modifier = Modifier.fillMaxWidth().padding(16.dp), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator(color = MaterialTheme.colorScheme.onSurface)
                    }
                }
                SaveBackupsUiState.Failed -> {
                    Text(modifier = Modifier.padding(padding), text = stringResource(id = R.string.save_backups_unreadable))
                }
                is SaveBackupsUiState.Ready -> {
                    if (saveBackupsUiState.backups.isEmpty()) {
                        Text(modifier = Modifier.padding(padding), text = stringResource(id = R.string.save_backups_empty))
                    } else {
                        Column {
                            saveBackupsUiState.backups.forEach { backup ->
                                SaveBackupRow(backup = backup, padding = padding, onClick = { onBackupClick(backup) })
                            }
                        }
                    }
                }
            }
        },
        buttons = {
            DialogButton(text = stringResource(id = R.string.close), onClick = onDismiss)
        },
    )
}

@Composable
private fun SaveBackupRow(backup: SaveBackupUiModel, padding: PaddingValues, onClick: () -> Unit) {
    // Rounded up, so a small save doesn't show as 0 KB
    val sizeInKb = (backup.sizeBytes + 1023) / 1024
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .heightIn(min = 56.dp)
            .padding(padding)
            .padding(vertical = 8.dp),
    ) {
        Text(text = formatBackupTime(backup.time), style = MaterialTheme.typography.bodyLarge)
        CaptionText(text = stringResource(id = R.string.save_backup_size, sizeInKb))
    }
}

/**
 * The backup's date and time in the user's locale
 */
@Composable
private fun formatBackupTime(time: LocalDateTime): String {
    val formatter = remember { DateTimeFormatter.ofLocalizedDateTime(FormatStyle.MEDIUM) }
    return remember(time, formatter) { time.format(formatter) }
}

@MelonPreviewSet
@Composable
private fun PreviewSaveBackupsDialog() {
    MelonTheme {
        SaveBackupsDialog(
            saveBackupsUiState = SaveBackupsUiState.Ready(
                listOf(
                    SaveBackupUiModel("Game.sav.20261119-114707.bak", LocalDateTime.of(2026, 11, 19, 11, 47, 7), 524288),
                    SaveBackupUiModel("Game.sav.20261118-203012.bak", LocalDateTime.of(2026, 11, 18, 20, 30, 12), 524288),
                ),
            ),
            onBackupClick = { },
            onDismiss = { },
        )
    }
}
