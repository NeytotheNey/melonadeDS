package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import androidx.preference.Preference
import androidx.preference.SwitchPreference
import com.google.android.material.color.DynamicColors
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.impl.SettingsBackupManager
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.SettingsBackupLauncher
import javax.inject.Inject

@AndroidEntryPoint
class AppearancePreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    @Inject lateinit var settingsBackupManager: SettingsBackupManager

    private val settingsBackupLauncher = SettingsBackupLauncher(this) { settingsBackupManager }

    override fun getTitle() = getString(R.string.settings_appearance_and_backup)

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_appearance, rootKey)

        val dynamicColorsPreference = findPreference<SwitchPreference>(PreferenceKeys.DYNAMIC_COLORS)!!
        // Dynamic colors are not supported by every Android 12 device, so this can't be decided from the API level alone
        dynamicColorsPreference.isVisible = DynamicColors.isDynamicColorAvailable()

        findPreference<Preference>("backup_settings")?.setOnPreferenceClickListener {
            settingsBackupLauncher.launchBackup()
            true
        }
        findPreference<Preference>("restore_settings")?.setOnPreferenceClickListener {
            settingsBackupLauncher.launchRestore()
            true
        }
    }
}
