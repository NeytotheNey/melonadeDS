package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import android.text.InputType
import androidx.preference.EditTextPreference
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreference
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.R
import me.magnum.melonds.common.DirectoryAccessValidator
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.common.UriPermissionManager
import me.magnum.melonds.extensions.isSustainedPerformanceModeAvailable
import me.magnum.melonds.impl.diagnostics.DebugSwitches
import me.magnum.melonds.impl.diagnostics.LogFolder
import me.magnum.melonds.ui.settings.PreferenceFragmentHelper
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.preferences.StoragePickerPreference
import me.magnum.melonds.utils.supportsJit
import javax.inject.Inject

@AndroidEntryPoint
class PerformancePreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    private val helper by lazy { PreferenceFragmentHelper(this, uriPermissionManager, directoryAccessValidator) }
    @Inject lateinit var uriPermissionManager: UriPermissionManager
    @Inject lateinit var directoryAccessValidator: DirectoryAccessValidator

    override fun getTitle() = getString(R.string.settings_performance)

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_performance, rootKey)
        val jitPreference = findPreference<SwitchPreference>("enable_jit")!!
        val sustainedPerformancePreference = findPreference<SwitchPreference>("enable_sustained_performance")!!
        val deviceCategory = findPreference<PreferenceCategory>(PreferenceKeys.CATEGORY_PERFORMANCE_DEVICE)!!

        if (!supportsJit()) {
            jitPreference.isEnabled = false
            jitPreference.isChecked = false
            jitPreference.setSummary(R.string.jit_not_supported)
        }

        sustainedPerformancePreference.isVisible = requireContext().isSustainedPerformanceModeAvailable()
        updateGroupVisibility(deviceCategory)

        setupPerformanceLogDirectoryPreference()
        setupDebugSwitchesPreference()
    }

    // Debug builds only: the switches the next game start applies (DebugSwitches), shown as they'll be read
    private fun setupDebugSwitchesPreference() {
        val debugSwitchesPreference = findPreference<EditTextPreference>(PreferenceKeys.DEBUG_SWITCHES)!!
        debugSwitchesPreference.isVisible = BuildConfig.DEBUG
        if (!BuildConfig.DEBUG) {
            return
        }

        debugSwitchesPreference.setOnBindEditTextListener { editText ->
            editText.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            editText.isSingleLine = false
            editText.minLines = 2
        }
        debugSwitchesPreference.setSummaryProvider { preference ->
            val result = DebugSwitches.parse((preference as EditTextPreference).text)
            val switches = result.describe().ifEmpty { getString(R.string.debug_switches_none) }
            if (result.errors.isEmpty()) {
                switches
            } else {
                getString(R.string.debug_switches_invalid, switches, result.errors.joinToString(", "))
            }
        }
    }

    private fun setupPerformanceLogDirectoryPreference() {
        val logDirectoryPreference = findPreference<StoragePickerPreference>(PreferenceKeys.PERF_LOG_DIR)!!
        val performanceLoggingPreference = findPreference<SwitchPreference>(PreferenceKeys.ENABLE_PERF_LOGGING)!!
        val debugLogPreference = findPreference<SwitchPreference>(PreferenceKeys.ENABLE_DEBUG_LOG)!!
        helper.setupStoragePickerPreference(logDirectoryPreference)

        // Both logs share the folder, so it's enabled while either is on; the listener runs before the value is saved, hence the argument
        fun updateLogDirectoryEnabled(isPerformanceLoggingOn: Boolean, isDebugLogOn: Boolean) {
            logDirectoryPreference.isEnabled = isPerformanceLoggingOn || isDebugLogOn
        }
        updateLogDirectoryEnabled(performanceLoggingPreference.isChecked, debugLogPreference.isChecked)
        performanceLoggingPreference.setOnPreferenceChangeListener { _, newValue ->
            updateLogDirectoryEnabled(newValue as Boolean, debugLogPreference.isChecked)
            true
        }
        debugLogPreference.setOnPreferenceChangeListener { _, newValue ->
            updateLogDirectoryEnabled(performanceLoggingPreference.isChecked, newValue as Boolean)
            true
        }

        // A picked folder can't be cleared, so the default location only needs to be shown while no folder has been picked
        if (logDirectoryPreference.getPersistedStringSet(null).isNullOrEmpty()) {
            val defaultDirectory = LogFolder.getDefaultLogDirectory(requireContext())
            logDirectoryPreference.summary = getString(R.string.performance_log_directory_default, defaultDirectory.absolutePath)
        }
    }
}
