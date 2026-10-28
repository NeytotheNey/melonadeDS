package me.magnum.melonds.ui.settings.fragments

import android.app.ActivityManager
import android.os.Build
import android.os.Bundle
import androidx.core.content.getSystemService
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.VideoRenderer
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.utils.enumValueOfIgnoreCase

@AndroidEntryPoint
class VideoPreferencesFragment : BasePreferenceFragment(), PreferenceFragmentTitleProvider {

    private companion object {
        const val GLES_3_2 = 0x30002
    }

    // Only the OpenGL and compute renderers have an internal resolution
    private lateinit var internalResolutionPreference: Preference
    private lateinit var renderingCategory: PreferenceCategory

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_video, rootKey)
        renderingCategory = findPreference("category_display_3d_rendering")!!

        internalResolutionPreference = findPreference("video_internal_resolution")!!

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
        internalResolutionPreference.isVisible = newRenderer != VideoRenderer.SOFTWARE
        // The renderer option is hidden on devices without GLES 3.2, which can leave the section empty
        updateGroupVisibility(renderingCategory)
    }

    override fun getTitle(): String {
        return getString(R.string.settings_display)
    }
}