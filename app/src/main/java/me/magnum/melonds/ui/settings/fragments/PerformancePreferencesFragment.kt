package me.magnum.melonds.ui.settings.fragments

import android.os.Build
import android.os.Bundle
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreference
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.common.DirectoryAccessValidator
import me.magnum.melonds.common.UriPermissionManager
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.extensions.isSustainedPerformanceModeAvailable
import me.magnum.melonds.impl.perf.PerformanceLogger
import me.magnum.melonds.ui.settings.PreferenceFragmentHelper
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.preferences.StoragePickerPreference
import me.magnum.melonds.utils.enumValueOfIgnoreCase
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
        val threadedRenderingPreference = findPreference<SwitchPreference>("enable_threaded_rendering")!!
        val sustainedPerformancePreference = findPreference<SwitchPreference>("enable_sustained_performance")!!
        val deviceCategory = findPreference<PreferenceCategory>("category_performance_device")!!

        if (Build.SUPPORTED_64_BIT_ABIS.isEmpty()) {
            jitPreference.isEnabled = false
            jitPreference.isChecked = false
            jitPreference.setSummary(R.string.jit_not_supported)
        }

        // Threaded rendering is only used by the software renderer, which is picked in the Display settings
        val rendererValue = preferenceManager.sharedPreferences?.getString("video_renderer", null) ?: VideoRenderer.SOFTWARE.name
        val isSoftwareRenderer = enumValueOfIgnoreCase<VideoRenderer>(rendererValue) == VideoRenderer.SOFTWARE
        threadedRenderingPreference.isEnabled = isSoftwareRenderer
        if (!isSoftwareRenderer) {
            threadedRenderingPreference.setSummary(R.string.threaded_rendering_software_only)
        }

        sustainedPerformancePreference.isVisible = requireContext().isSustainedPerformanceModeAvailable()
        updateGroupVisibility(deviceCategory)

        setupPerformanceLogDirectoryPreference()
    }

    private fun setupPerformanceLogDirectoryPreference() {
        val logDirectoryPreference = findPreference<StoragePickerPreference>("perf_log_dir")!!
        helper.setupStoragePickerPreference(logDirectoryPreference)

        // A picked folder can't be cleared, so the default location only needs to be shown while no folder has been picked
        if (logDirectoryPreference.getPersistedStringSet(null).isNullOrEmpty()) {
            val defaultDirectory = PerformanceLogger.getDefaultLogDirectory(requireContext())
            logDirectoryPreference.summary = getString(R.string.performance_log_directory_default, defaultDirectory.absolutePath)
        }
    }
}
