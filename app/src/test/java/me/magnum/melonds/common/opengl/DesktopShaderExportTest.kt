package me.magnum.melonds.common.opengl

import me.magnum.melonds.domain.model.VideoFiltering
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/**
 * The desktop app's copies of the shaders (desktop/shaders, embedded into its exe): every ShaderProgramSource, including the ones built on each
 * get() like the tilt-shift passes, as one .vert and one .frag file each, and shaders.json with each shader's texture filtering and the filter
 * table (each VideoFiltering's screen shader, pre-passes, scale, nativeResolutionOnly and canFoldDeband, from VideoFilterShaderProvider).
 *
 * Without the melonade.exportShaders system property it fails when the committed files differ from what the Kotlin gives, so the desktop's copies
 * can't drift. With it (Gradle: -Pmelonade.exportShaders, see app/build.gradle.kts) it writes them again. The folder comes from the
 * melonade.desktopShadersDir system property, which app/build.gradle.kts sets; without it the test has nothing to check and is skipped.
 */
class DesktopShaderExportTest {

    private class Export(val files: Map<String, String>)

    @Test
    fun desktopShadersMatchTheKotlin() {
        val dirPath = System.getProperty("melonade.desktopShadersDir")
        assumeTrue("melonade.desktopShadersDir isn't set, so there's nothing to check", !dirPath.isNullOrEmpty())
        val dir = File(dirPath!!)
        val export = buildExport()

        if (System.getProperty("melonade.exportShaders").toBoolean()) {
            write(dir, export)
            println("wrote ${export.files.size} files to $dir")
            return
        }

        assertTrue("$dir is missing: run the unit test with -Pmelonade.exportShaders (desktop/README.md)", dir.isDirectory)
        val problems = mutableListOf<String>()
        for ((name, content) in export.files) {
            val file = File(dir, name)
            if (!file.isFile) {
                problems += "$name is missing"
            } else if (!file.readBytes().contentEquals(content.toByteArray(Charsets.UTF_8))) {
                // Exact bytes, since the desktop embeds them as they are: a CRLF save would pass a text compare and still change what it draws
                problems += "$name differs"
            }
        }
        val stale = exportedFiles(dir).map { it.name }.filter { it !in export.files }
        stale.forEach { problems += "$it isn't a shader any more" }
        if (problems.isNotEmpty()) {
            fail("desktop/shaders doesn't match ShaderProgramSource and VideoFilterShaderProvider (export them again with " +
                "-Pmelonade.exportShaders, see desktop/README.md):\n" + problems.joinToString("\n"))
        }
    }

    @Test
    fun everyFilterNamesAnExportedShader() {
        val shaders = allShaders()
        val names = shaders.values.toSet()
        for (filtering in VideoFiltering.entries) {
            val filter = VideoFilterShaderProvider.getFilter(filtering)
            assertTrue(filtering.name, filter.screenShader in names)
            filter.prePasses.forEach { assertTrue(filtering.name, it.source in names) }
        }
        // The shaders VideoFilterPipeline and DSRenderer name themselves, which the desktop's port names the same way
        for (name in listOf("NoFilterShader", "DebandPrePassShader", "NoFilterHalvesAt1xShader", "ExpandHalvesAt1xPrePassShader", "ScreenShadowShader",
            "TiltShiftHorizontalPrePassShader", "TiltShiftVerticalPrePassShader", "TiltShiftColorPrePassShader", "BloomBrightPrePassShader",
            "BloomBlurHorizontalPrePassShader", "BloomBlurVerticalPrePassShader", "BloomCombinePrePassShader", "DepthOfFieldHorizontalPrePassShader",
            "DepthOfFieldVerticalPrePassShader", "DepthOfFieldFocusShader", "ShadeDownsamplePrePassShader", "ShadeBlurHorizontalPrePassShader",
            "ShadeBlurVerticalPrePassShader", "ShadeApplyPrePassShader")) {
            assertTrue(name, name in shaders.keys)
        }
        assertEquals(shaders.size, names.size)
    }

    // Every ShaderProgramSource the companion has, by its property's name; the get() ones are built once here
    private fun allShaders(): Map<String, ShaderProgramSource> {
        val companion = ShaderProgramSource.Companion
        return companion.javaClass.methods
            .filter { it.parameterCount == 0 && it.returnType == ShaderProgramSource::class.java && it.name.startsWith("get") }
            .associate { it.name.removePrefix("get") to it.invoke(companion) as ShaderProgramSource }
            .toSortedMap()
    }

    private fun buildExport(): Export {
        val shaders = allShaders()
        // The vals keep their instances, which the filter table refers to; a get() one is a new instance each time, which no filter uses
        val nameOf = shaders.entries.associate { (name, source) -> source to name }
        val files = sortedMapOf<String, String>()
        for ((name, source) in shaders) {
            files["$name.vert"] = source.vertexShaderSource
            files["$name.frag"] = source.fragmentShaderSource
        }

        val json = StringBuilder()
        json.append("{\n")
        json.append("  \"generatedBy\": \"app/src/test/java/me/magnum/melonds/common/opengl/DesktopShaderExportTest.kt\",\n")
        json.append("  \"shaders\": [\n")
        json.append(shaders.entries.joinToString(",\n") { (name, source) ->
            "    {\"name\": ${quote(name)}, \"textureFiltering\": ${quote(source.textureFiltering.name)}}"
        })
        json.append("\n  ],\n")
        json.append("  \"filters\": [\n")
        json.append(VideoFiltering.entries.joinToString(",\n") { filtering ->
            val filter = VideoFilterShaderProvider.getFilter(filtering)
            val prePasses = filter.prePasses.joinToString(", ") { pass ->
                "{\"shader\": ${quote(nameOf.getValue(pass.source))}, \"scale\": ${pass.scale}, \"nativeResolutionOnly\": ${pass.nativeResolutionOnly}}"
            }
            "    {\"name\": ${quote(filtering.name)}, \"screenShader\": ${quote(nameOf.getValue(filter.screenShader))}, " +
                "\"canFoldDeband\": ${filter.canFoldDeband}, \"prePasses\": [$prePasses]}"
        })
        json.append("\n  ]\n")
        json.append("}\n")
        files["shaders.json"] = json.toString()
        return Export(files)
    }

    private fun quote(text: String): String = "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\""

    private fun exportedFiles(dir: File): List<File> {
        return dir.listFiles()?.filter { it.isFile && (it.extension == "vert" || it.extension == "frag" || it.name == "shaders.json") } ?: emptyList()
    }

    private fun write(dir: File, export: Export) {
        if (!dir.isDirectory && !dir.mkdirs()) {
            fail("can't make $dir")
        }
        // A shader that's gone leaves no file behind
        exportedFiles(dir).filter { it.name !in export.files }.forEach { it.delete() }
        for ((name, content) in export.files) {
            File(dir, name).writeText(content)
        }
    }
}
