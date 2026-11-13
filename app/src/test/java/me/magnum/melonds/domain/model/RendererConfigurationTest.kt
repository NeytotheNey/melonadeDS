package me.magnum.melonds.domain.model

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

class RendererConfigurationTest {

    // Gradle runs unit tests from the module directory; the fallback is for a runner started from the project root
    private val moduleDir = listOf(File("."), File("app")).first { File(it, "src/main/res/xml").isDirectory }

    private fun configuration(renderer: VideoRenderer, scaling: Int) = RendererConfiguration(
        renderer = renderer,
        videoFiltering = VideoFiltering.NONE,
        deband = false,
        dsScreenColors = false,
        screenShadow = false,
        threadedRendering = false,
        threaded2dRendering = false,
        hiresCapture = false,
        filter2d = false,
        colorDithering = false,
        internalResolutionScaling = scaling,
    )

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
