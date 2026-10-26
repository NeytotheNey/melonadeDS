package me.magnum.melonds.ui.settings.fragments

import android.os.Build
import android.os.Bundle
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreference
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.extensions.isSustainedPerformanceModeAvailable
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.utils.enumValueOfIgnoreCase

class PerformancePreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

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
    }
}
