package me.magnum.melonds.impl.diagnostics

import android.content.Context
import android.content.pm.ApplicationInfo
import android.os.Build
import androidx.core.content.pm.PackageInfoCompat
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.domain.model.RendererConfiguration
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.utils.PackageManagerCompat
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * The settings and the app and device facts that the performance summary and the debug log header both report, as key/value pairs with the
 * performance log's key spellings.
 */
internal object DiagnosticsInfo {

    /**
     * The settings both logs report, with [renderer] passed in because the debug log has the launched configuration's copy and the performance
     * logger the current one.
     */
    fun settings(renderer: RendererConfiguration, settingsRepository: SettingsRepository): List<Pair<String, String>> {
        return listOf(
            "video_renderer" to renderer.renderer.name,
            "internal_resolution_scaling" to renderer.resolutionScaling.toString(),
            "video_filtering" to renderer.videoFiltering.name,
            "deband" to renderer.deband.toString(),
            "ds_screen_colors" to renderer.dsScreenColors.toString(),
            "screen_shadow" to renderer.screenShadow.toString(),
            "threaded_rendering" to renderer.threadedRendering.toString(),
            "threaded_2d_rendering" to renderer.threaded2dRendering.toString(),
            "hires_capture" to renderer.hiresCapture.toString(),
            "color_dithering" to renderer.colorDithering.toString(),
            "jit_enabled" to settingsRepository.isJitEnabled().toString(),
            "sustained_performance_mode" to settingsRepository.isSustainedPerformanceModeEnabled().toString(),
            "frame_pacing_vsync" to settingsRepository.isFramePacingVsyncEnabled().toString(),
            "frame_pacing_lock_60hz" to settingsRepository.isFramePacingLock60HzEnabled().toString(),
            "frame_pacing_buffer" to settingsRepository.isFramePacingBufferEnabled().toString(),
            "fast_forward_multiplier" to settingsRepository.getFastForwardSpeedMultiplier().toString(),
            "default_console_type" to settingsRepository.getDefaultConsoleType().name,
            "sound_enabled" to settingsRepository.isSoundEnabled().toString(),
            "rewind_enabled" to settingsRepository.isRewindEnabled().toString(),
        )
    }

    /**
     * The build that is running: package, versions, flavor and install time.
     */
    fun app(context: Context): List<Pair<String, String?>> {
        val packageInfo = runCatching { PackageManagerCompat.getPackageInfo(context.packageManager, context.packageName, 0) }.getOrNull()
        val isDebuggable = (context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        return listOf(
            "app_version_name" to packageInfo?.versionName,
            "app_version_code" to packageInfo?.let { PackageInfoCompat.getLongVersionCode(it).toString() },
            "app_package" to context.packageName,
            "app_upstream_version" to BuildConfig.UPSTREAM_VERSION_NAME,
            "app_installed" to packageInfo?.let { SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(it.lastUpdateTime)) },
            "build_type" to BuildConfig.BUILD_TYPE,
            "build_flavor" to BuildConfig.FLAVOR,
            "build_debuggable" to isDebuggable.toString(),
        )
    }

    fun device(): List<Pair<String, String?>> {
        return listOfNotNull(
            "device_manufacturer" to Build.MANUFACTURER,
            "device_model" to Build.MODEL,
            "device_name" to Build.DEVICE,
            "device_hardware" to Build.HARDWARE,
            "device_board" to Build.BOARD,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) "device_soc" to "${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}" else null,
            "device_abis" to Build.SUPPORTED_ABIS.joinToString(", "),
            "device_fingerprint" to Build.FINGERPRINT,
            "android_version" to Build.VERSION.RELEASE,
            "android_sdk" to Build.VERSION.SDK_INT.toString(),
            "android_security_patch" to Build.VERSION.SECURITY_PATCH,
        )
    }
}
