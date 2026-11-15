package me.magnum.melonds.ui.settings.fragments

import android.app.ActivityManager
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
    private lateinit var renderingCategory: PreferenceCategory
    // Threaded 3D rendering is only used by the software renderer
    private lateinit var threadedRenderingPreference: SwitchPreference
    // Only the OpenGL and compute renderers can draw the in-between frames
    private lateinit var smooth30FpsPreference: Preference

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.pref_video, rootKey)
        renderingCategory = findPreference(PreferenceKeys.CATEGORY_DISPLAY_3D_RENDERING)!!

        internalResolutionPreference = findPreference("video_internal_resolution")!!
        hiresCapturePreference = findPreference(PreferenceKeys.VIDEO_HIRES_CAPTURE)!!
        filter2dPreference = findPreference(PreferenceKeys.VIDEO_FILTER_2D)!!
        threadedRenderingPreference = findPreference("enable_threaded_rendering")!!
        smooth30FpsPreference = findPreference(PreferenceKeys.FRAME_PACING_SMOOTH_30FPS)!!

        val rendererPreference = findPreference<ListPreference>("video_renderer")!!

        val activityManager = requireContext().getSystemService<ActivityManager>()

        rendererPreference.apply {
            val deviceGlesVersion = activityManager?.deviceConfigurationInfo?.reqGlEsVersion ?: 0
            if (deviceGlesVersion >= GLES_3_2) {
                // Compute runs on any GLES 3.2 GPU. It used to be Qualcomm-only, until the Mali
                // uvec4 atomic workaround (core 4b58305d) fixed the missing blocks on Mali
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
        smooth30FpsPreference.isEnabled = !isSoftwareRenderer
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