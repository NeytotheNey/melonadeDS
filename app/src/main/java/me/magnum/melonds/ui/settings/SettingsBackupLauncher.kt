package me.magnum.melonds.ui.settings

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.R
import me.magnum.melonds.impl.SettingsBackupManager

/**
 * Folder pickers plus the off-main-thread backup and restore and their result dialogs; create it while [fragment] is being created (it registers
 * activity results), while [settingsBackupManager] is only called after a folder is picked so it can return a later-injected property.
 */
class SettingsBackupLauncher(
    private val fragment: Fragment,
    private val settingsBackupManager: () -> SettingsBackupManager,
) {

    private val backupLauncher = fragment.registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            backUp(uri)
        }
    }

    private val restoreLauncher = fragment.registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            restore(uri, fragment.requireContext().applicationContext)
        }
    }

    fun launchBackup() {
        backupLauncher.launch(null)
    }

    /**
     * Asks for confirmation first, since the restore replaces all settings and restarts the app.
     */
    fun launchRestore() {
        MaterialAlertDialogBuilder(fragment.requireContext())
            .setTitle(R.string.restore_settings)
            .setMessage(R.string.settings_restore_confirmation)
            .setPositiveButton(R.string.choose_folder) { _, _ -> restoreLauncher.launch(null) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun backUp(uri: Uri) {
        fragment.lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatchingNonCancellation { settingsBackupManager().backup(uri) }
            }
            if (result.isSuccess) {
                MaterialAlertDialogBuilder(fragment.requireContext())
                    .setMessage(R.string.settings_backup_success)
                    .setPositiveButton(android.R.string.ok, null)
                    .show()
            } else {
                Toast.makeText(fragment.requireContext(), R.string.settings_backup_error, Toast.LENGTH_SHORT).show()
            }
        }
    }

    @OptIn(DelicateCoroutinesApi::class)
    private fun restore(uri: Uri, appContext: Context) {
        // Not tied to the screen, so a configuration change can't cancel the restart below
        GlobalScope.launch(Dispatchers.Main) {
            val result = withContext(Dispatchers.IO) {
                runCatchingNonCancellation { settingsBackupManager().restore(uri) }
            }
            result.onSuccess {
                // The restore is only staged, to be applied when the app starts again
                restartApp(appContext)
            }.onFailure {
                if (it is SettingsBackupManager.BackupFromNewerVersionException) {
                    Toast.makeText(appContext, R.string.settings_restore_newer_version, Toast.LENGTH_LONG).show()
                } else {
                    Toast.makeText(appContext, R.string.settings_restore_error, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun restartApp(context: Context) {
        val launchComponent = context.packageManager.getLaunchIntentForPackage(context.packageName)?.component
        if (launchComponent != null) {
            // makeRestartActivityTask() adds FLAG_ACTIVITY_NEW_TASK, so this can be started from the application context
            context.startActivity(Intent.makeRestartActivityTask(launchComponent))
        }
        Runtime.getRuntime().exit(0)
    }

    /**
     * [runCatching] that rethrows [CancellationException] and still catches Errors such as a StackOverflowError from a deeply nested file.
     */
    private inline fun runCatchingNonCancellation(block: () -> Unit): Result<Unit> {
        return try {
            Result.success(block())
        } catch (e: CancellationException) {
            throw e
        } catch (e: Throwable) {
            Result.failure(e)
        }
    }
}
