package me.magnum.melonds.impl

import android.content.ContextWrapper
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
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
}
