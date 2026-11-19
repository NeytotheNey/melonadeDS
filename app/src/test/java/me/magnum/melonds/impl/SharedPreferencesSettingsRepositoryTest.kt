package me.magnum.melonds.impl

import android.content.ContextWrapper
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.common.uridelegates.UriHandler
import me.magnum.melonds.domain.model.ControllerConfiguration
import me.magnum.melonds.impl.input.ControllerConfigurationFactory
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class SharedPreferencesSettingsRepositoryTest {

    // Gradle runs unit tests from the module directory; the fallback is for a runner started from the project root
    private val moduleDir = listOf(File("."), File("app")).first { File(it, "src/main/res/xml").isDirectory }

    private val scope = CoroutineScope(Dispatchers.Unconfined)

    @After
    fun tearDown() {
        scope.cancel()
    }

    private fun repository(preferences: FakeSharedPreferences): SharedPreferencesSettingsRepository {
        val uriHandler = object : UriHandler {
            override fun fileExists(uri: Uri) = false
            override fun createFileDocument(uri: Uri): DocumentFile? = null
            override fun getUriDocument(uri: Uri): DocumentFile? = null
            override fun getUriTreeDocument(uri: Uri): DocumentFile? = null
        }
        val controllerConfigurationFactory = object : ControllerConfigurationFactory {
            override fun buildDefaultControllerConfiguration(): ControllerConfiguration = throw AssertionError("Not used")
        }
        return SharedPreferencesSettingsRepository(ContextWrapper(null), preferences, controllerConfigurationFactory, Json, uriHandler, scope)
    }

    // "Smooth gradients" (compute's dithering) was merged into deband: an old "on" with Compute turns deband on, once, and the old key goes
    @Test
    fun colorDitheringOnWithComputeTurnsDebandOn() {
        val preferences = FakeSharedPreferences(
            mapOf("video_renderer" to "compute", PreferenceKeys.VIDEO_COLOR_DITHERING to true, PreferenceKeys.VIDEO_DEBAND to false)
        )
        repository(preferences)

        assertTrue(preferences.getBoolean(PreferenceKeys.VIDEO_DEBAND, false))
        assertFalse(preferences.contains(PreferenceKeys.VIDEO_COLOR_DITHERING))
    }

    // Dithering did nothing on the other renderers, so their deband stays as it was; the old key still goes
    @Test
    fun colorDitheringOnWithOtherRenderersLeavesDebandAlone() {
        val openGl = FakeSharedPreferences(
            mapOf("video_renderer" to "opengl", PreferenceKeys.VIDEO_COLOR_DITHERING to true, PreferenceKeys.VIDEO_DEBAND to false)
        )
        repository(openGl)
        assertFalse(openGl.getBoolean(PreferenceKeys.VIDEO_DEBAND, true))
        assertFalse(openGl.contains(PreferenceKeys.VIDEO_COLOR_DITHERING))

        // No renderer stored is software, the default
        val noRenderer = FakeSharedPreferences(mapOf(PreferenceKeys.VIDEO_COLOR_DITHERING to true))
        repository(noRenderer)
        assertFalse(noRenderer.contains(PreferenceKeys.VIDEO_DEBAND))
        assertFalse(noRenderer.contains(PreferenceKeys.VIDEO_COLOR_DITHERING))
    }

    @Test
    fun colorDitheringOffLeavesDebandAlone() {
        val off = FakeSharedPreferences(mapOf(PreferenceKeys.VIDEO_COLOR_DITHERING to false, PreferenceKeys.VIDEO_DEBAND to false))
        repository(off)
        assertFalse(off.getBoolean(PreferenceKeys.VIDEO_DEBAND, false))
        assertFalse(off.contains(PreferenceKeys.VIDEO_COLOR_DITHERING))

        val debandOn = FakeSharedPreferences(mapOf(PreferenceKeys.VIDEO_COLOR_DITHERING to false, PreferenceKeys.VIDEO_DEBAND to true))
        repository(debandOn)
        assertTrue(debandOn.getBoolean(PreferenceKeys.VIDEO_DEBAND, false))

        val never = FakeSharedPreferences()
        repository(never)
        assertFalse(never.contains(PreferenceKeys.VIDEO_DEBAND))
    }

    @Test
    fun smooth30FpsIsOffByDefault() {
        assertFalse(repository(FakeSharedPreferences()).isFramePacingSmooth30FpsEnabled())
    }

    @Test
    fun smooth30FpsFollowsThePreference() {
        val preferences = FakeSharedPreferences(mapOf(PreferenceKeys.FRAME_PACING_SMOOTH_30FPS to true))

        assertTrue(repository(preferences).isFramePacingSmooth30FpsEnabled())
    }

    // getEmulatorConfiguration() can't run on the JVM, as the JIT's default reads Build.SUPPORTED_64_BIT_ABIS, which is a null constant in android.jar's
    // stubs. So this reads the source, like PreferenceKeysTest. The native side looks the field up by name, and a wrong name would crash at game start
    @Test
    fun smooth30FpsIsPassedToTheEmulatorConfigurationAndNativeCode() {
        val repositorySource = File(moduleDir, "src/main/java/me/magnum/melonds/impl/SharedPreferencesSettingsRepository.kt").readText()
        val nativeSource = File(moduleDir, "src/main/cpp/MelonDSAndroidConfiguration.cpp").readText()

        assertTrue(repositorySource.contains("framePacingSmooth30FpsEnabled = isFramePacingSmooth30FpsEnabled()"))
        assertTrue(nativeSource.contains("\"framePacingSmooth30FpsEnabled\""))
    }

    @Test
    fun glSeamsIsOffByDefault() = runBlocking {
        assertFalse(repository(FakeSharedPreferences()).isGlSeamsEnabled().first())
    }

    @Test
    fun glSeamsFollowsThePreference() = runBlocking {
        val preferences = FakeSharedPreferences(mapOf(PreferenceKeys.VIDEO_GL_SEAMS to true))

        assertTrue(repository(preferences).isGlSeamsEnabled().first())
    }

    // The native side reads it from RendererConfiguration by name, and a wrong name would crash at game start
    @Test
    fun glSeamsIsReadByNativeCode() {
        val nativeSource = File(moduleDir, "src/main/cpp/MelonDSAndroidConfiguration.cpp").readText()

        assertTrue(nativeSource.contains("\"glSeams\", \"Z\""))
    }

    // "DS screen colors" left the settings: an old "on" reads as off, and the stored value stays as it was
    @Test
    fun dsScreenColorsReadsAsOffAndKeepsTheStoredValue() = runBlocking {
        val preferences = FakeSharedPreferences(mapOf("video_ds_screen_colors" to true))

        assertFalse(repository(preferences).isDsScreenColorsEnabled().first())
        assertTrue(preferences.getBoolean("video_ds_screen_colors", false))
    }
}
