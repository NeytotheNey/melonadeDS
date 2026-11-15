package me.magnum.melonds.domain.model

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

class RendererConfigurationTest {

    // Gradle runs unit tests from the module directory; the fallback is for a runner started from the project root
    private val moduleDir = listOf(File("."), File("app")).first { File(it, "src/main/res/xml").isDirectory }

    private fun configuration(renderer: VideoRenderer, scaling: Int, deband: Boolean = false) = RendererConfiguration(
        renderer = renderer,
        videoFiltering = VideoFiltering.NONE,
        deband = deband,
        dsScreenColors = false,
        screenShadow = false,
        threadedRendering = false,
        threaded2dRendering = false,
        hiresCapture = false,
        filter2d = false,
        internalResolutionScaling = scaling,
    )

    // "Smooth gradients" is one switch: deband on every renderer, and compute's color dithering with it
    @Test
    fun colorDitheringFollowsDebandOnComputeOnly() {
        assertEquals(true, configuration(VideoRenderer.COMPUTE, 4, deband = true).colorDithering)
        assertEquals(false, configuration(VideoRenderer.COMPUTE, 4, deband = false).colorDithering)
        assertEquals(false, configuration(VideoRenderer.OPENGL, 4, deband = true).colorDithering)
        assertEquals(false, configuration(VideoRenderer.SOFTWARE, 1, deband = true).colorDithering)
    }

    // The native side reads colorDithering as a boolean field by name, so it has to stay a field, not just a getter
    @Test
    fun colorDitheringIsABooleanField() {
        val field = RendererConfiguration::class.java.getDeclaredField("colorDithering")
        assertEquals(Boolean::class.javaPrimitiveType, field.type)
    }

    @Test
    fun openGlKeepsEveryScale() {
        listOf(1, 8, 12, 16).forEach {
            assertEquals(it, configuration(VideoRenderer.OPENGL, it).resolutionScaling)
        }
    }

    @Test
    fun computeStopsAt8x() {
        assertEquals(4, configuration(VideoRenderer.COMPUTE, 4).resolutionScaling)
        assertEquals(8, configuration(VideoRenderer.COMPUTE, 8).resolutionScaling)
        assertEquals(8, configuration(VideoRenderer.COMPUTE, 12).resolutionScaling)
        assertEquals(8, configuration(VideoRenderer.COMPUTE, 16).resolutionScaling)
    }

    @Test
    fun softwareIsAlways1x() {
        assertEquals(1, configuration(VideoRenderer.SOFTWARE, 16).resolutionScaling)
    }

    // Each internal resolution option has a value, and the values parse as the scales the repository reads (toIntOrNull)
    @Test
    fun internalResolutionOptionsAndValuesMatch() {
        val arrays = File(moduleDir, "src/main/res/values/arrays.xml").readText()
        val strings = File(moduleDir, "src/main/res/values/strings.xml").readText()

        val values = items(arrays, "video_internal_resolution_values")
        val options = items(strings, "video_internal_resolution_options")

        assertEquals(listOf(1, 2, 3, 4, 5, 6, 7, 8, 12, 16), values.map { it.toInt() })
        assertEquals(values.size, options.size)
        values.zip(options).forEach { (value, option) ->
            assertEquals("${value}x native (${256 * value.toInt()}x${192 * value.toInt()})", option)
        }
    }

    private fun items(xml: String, arrayName: String): List<String> {
        val array = Regex("<string-array name=\"$arrayName\">(.*?)</string-array>", RegexOption.DOT_MATCHES_ALL).find(xml)!!.groupValues[1]
        return Regex("<item>(.*?)</item>").findAll(array).map { it.groupValues[1].trim() }.toList()
    }
}
