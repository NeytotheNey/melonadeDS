package me.magnum.melonds.ui.settings.fragments

import android.app.ActivityManager
import android.os.Build
import android.os.Bundle
import androidx.core.content.getSystemService
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreference
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.utils.enumValueOfIgnoreCase

@AndroidEntryPoint
class VideoPreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    private companion object {
        const val GLES_3_2 = 0x30002
    }

    // Only the OpenGL and compute renderers have an internal resolution, upscaled display capture and 2D smoothing
    private lateinit var internalResolutionPreference: Preference
    private lateinit var hiresCapturePreference: Preference
    private lateinit var filter2dPreference: Preference
    // Only the compute renderer dithers color gradients
    private lateinit var colorDitheringPreference: Preference
    private lateinit var renderingCategory: PreferenceCategory
    // Threaded 3D rendering is only used by the software renderer
    private lateinit var threadedRenderingPreference: SwitchPreference
    // Only the compute renderer can draw the in-between frames
    private lateinit var smooth30FpsPreference: Preference

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_video, rootKey)
        renderingCategory = findPreference(PreferenceKeys.CATEGORY_DISPLAY_3D_RENDERING)!!

        internalResolutionPreference = findPreference("video_internal_resolution")!!
        hiresCapturePreference = findPreference(PreferenceKeys.VIDEO_HIRES_CAPTURE)!!
        filter2dPreference = findPreference(PreferenceKeys.VIDEO_FILTER_2D)!!
        colorDitheringPreference = findPreference(PreferenceKeys.VIDEO_COLOR_DITHERING)!!
        threadedRenderingPreference = findPreference("enable_threaded_rendering")!!
        smooth30FpsPreference = findPreference(PreferenceKeys.FRAME_PACING_SMOOTH_30FPS)!!

        val rendererPreference = findPreference<ListPreference>("video_renderer")!!

        val activityManager = requireContext().getSystemService<ActivityManager>()

        rendererPreference.apply {
            val deviceGlesVersion = activityManager?.deviceConfigurationInfo?.reqGlEsVersion ?: 0
            if (deviceGlesVersion >= GLES_3_2) {
                // Check if the device has a Qualcomm chipset. Only Adreno GPUs are supported for now
                val isAdrenoGpu = Build.HARDWARE.lowercase() == "qcom"
                if (!isAdrenoGpu) {
                    val computeRenderOptionIndex = rendererPreference.entryValues.indexOfFirst { it == VideoRenderer.COMPUTE.name.lowercase() }
                    rendererPreference.entries = rendererPreference.entries.filterIndexed { index, _ ->
                        index != computeRenderOptionIndex
                    }.toTypedArray()
                    rendererPreference.entryValues = rendererPreference.entryValues.filterIndexed { index, _ ->
                        index != computeRenderOptionIndex
                    }.toTypedArray()
                }

                setOnPreferenceChangeListener { _, newValue ->
                    onRendererPreferenceChanged(newValue as String)
                    true
                }
            } else {
                // GLES 3.2 is not supported. Remove the preference
                isVisible = false
            }
        }

        onRendererPreferenceChanged(rendererPreference.value)
    }

    private fun onRendererPreferenceChanged(rendererValue: String) {
        val newRenderer = enumValueOfIgnoreCase<VideoRenderer>(rendererValue)
        val isSoftwareRenderer = newRenderer == VideoRenderer.SOFTWARE
        internalResolutionPreference.isVisible = !isSoftwareRenderer
        hiresCapturePreference.isVisible = !isSoftwareRenderer
        filter2dPreference.isVisible = !isSoftwareRenderer
        colorDitheringPreference.isVisible = newRenderer == VideoRenderer.COMPUTE
        smooth30FpsPreference.isEnabled = newRenderer == VideoRenderer.COMPUTE
        threadedRenderingPreference.isEnabled = isSoftwareRenderer
        threadedRenderingPreference.setSummary(
            if (isSoftwareRenderer) R.string.threaded_rendering_summary else R.string.threaded_rendering_software_only
        )
        // The renderer option is hidden on devices without GLES 3.2, which can leave the section empty
        updateGroupVisibility(renderingCategory)
    }

    override fun getTitle(): String {
        return getString(R.string.settings_display)
    }
}