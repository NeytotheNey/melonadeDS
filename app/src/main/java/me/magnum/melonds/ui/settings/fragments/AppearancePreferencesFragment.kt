package me.magnum.melonds.ui.settings.fragments

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.lifecycleScope
import androidx.preference.Preference
import androidx.preference.SwitchPreference
import com.google.android.material.color.DynamicColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.R
import me.magnum.melonds.impl.SettingsBackupManager
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import javax.inject.Inject

@AndroidEntryPoint
class AppearancePreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    @Inject lateinit var settingsBackupManager: SettingsBackupManager

    private val backupLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            lifecycleScope.launch(Dispatchers.IO) {
                runCatching { settingsBackupManager.backup(uri) }
                    .onSuccess {
                        withContext(Dispatchers.Main) {
                            MaterialAlertDialogBuilder(requireContext())
                                .setMessage(R.string.settings_backup_success)
                                .setPositiveButton(android.R.string.ok, null)
                                .show()
                        }
                    }
                    .onFailure {
                        withContext(Dispatchers.Main) {
                            Toast.makeText(requireContext(), R.string.settings_backup_error, Toast.LENGTH_SHORT).show()
                        }
                    }
            }
        }
    }

    @OptIn(DelicateCoroutinesApi::class)
    private val restoreLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            val appContext = requireContext().applicationContext
            // Not tied to this screen, so a configuration change can't cancel the restart below
            GlobalScope.launch(Dispatchers.IO) {
                runCatching { settingsBackupManager.restore(uri) }
                    .onSuccess {
                        withContext(Dispatchers.Main) {
                            // The restore is only staged. It's applied when the app starts again, before anything loads the settings
                            restartApp(appContext)
                        }
                    }
                    .onFailure {
                        withContext(Dispatchers.Main) {
                            if (it is SettingsBackupManager.BackupFromNewerVersionException) {
                                Toast.makeText(appContext, R.string.settings_restore_newer_version, Toast.LENGTH_LONG).show()
                            } else {
                                Toast.makeText(appContext, R.string.settings_restore_error, Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
            }
        }
    }

    override fun getTitle() = getString(R.string.settings_appearance_and_backup)

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_appearance, rootKey)
        addPreferencesFromResource(R.xml.pref_general_updates)

        val dynamicColorsPreference = findPreference<SwitchPreference>("dynamic_colors")!!
        // Dynamic colours are not supported by every Android 12 device, so this can't be decided from the API level alone
        dynamicColorsPreference.isVisible = DynamicColors.isDynamicColorAvailable()

        findPreference<Preference>("backup_settings")?.setOnPreferenceClickListener {
            backupLauncher.launch(null)
            true
        }
        findPreference<Preference>("restore_settings")?.setOnPreferenceClickListener {
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.restore_settings)
                .setMessage(R.string.settings_restore_confirmation)
                .setPositiveButton(R.string.choose_folder) { _, _ -> restoreLauncher.launch(null) }
                .setNegativeButton(R.string.cancel, null)
                .show()
            true
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
}
