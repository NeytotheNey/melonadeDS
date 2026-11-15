package me.magnum.melonds.common.opengl

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * debug.melonade.debandfold: the Sharp and Linear screen shaders with DEBAND defined, and the deband pre-pass, as they're compiled. There's no
 * GLSL compiler on the JVM, so this runs the #ifdef blocks and checks what's left: deband defined once and called once, no name declared
 * twice (COLOR_CORRECTION and DEBAND both on), and the names deband needs declared before they're used.
 */
class DebandShaderSourceTest {

    private val screenShaders = listOf("Sharp" to ShaderProgramSource.SharpShader, "Linear" to ShaderProgramSource.LinearShader)

    @Test
    fun screenShadersAssembleWithDeband() {
        for ((name, source) in screenShaders) {
            for (colorCorrection in listOf(false, true)) {
                val code = compiledSource(source, colorCorrection, listOf("DEBAND"))
                val label = "$name, color correction $colorCorrection"
                assertEquals(label, 1, Regex("""\bvec3 deband\(""").findAll(code).count())
                // The definition and the one call in main()
                assertEquals(label, 2, Regex("""\bdeband\(""").findAll(code).count())
                assertEquals(label, colorCorrection, code.contains("pow("))
                assertNoDuplicateNames(label, code)
                assertDeclaredBeforeUse(label, code)
            }
        }
    }

    @Test
    fun screenShadersWithoutDebandHaveNoneOfIt() {
        for ((name, source) in screenShaders) {
            val code = compiledSource(source, colorCorrection = true, emptyList())
            assertFalse(name, code.contains("deband", ignoreCase = true))
            assertNoDuplicateNames(name, code)
        }
    }

    @Test
    fun prePassStillAssembles() {
        val code = compiledSource(ShaderProgramSource.DebandPrePassShader, colorCorrection = false, emptyList())
        assertEquals(1, Regex("""\bvec3 deband\(""").findAll(code).count())
        assertEquals(2, Regex("""\bdeband\(""").findAll(code).count())
        assertNoDuplicateNames("pre-pass", code)
        assertDeclaredBeforeUse("pre-pass", code)
    }

    @Test
    fun eachShaderUsesItsOwnTextureFunction() {
        // Sharp is GLSL ES 3.00 (texture()), Linear and the pre-pass are 1.00 (texture2D())
        val sharp = compiledSource(ShaderProgramSource.SharpShader, colorCorrection = false, listOf("DEBAND"))
        assertFalse(sharp.contains("texture2D("))
        for (source in listOf(ShaderProgramSource.LinearShader, ShaderProgramSource.DebandPrePassShader)) {
            val code = compiledSource(source, colorCorrection = false, listOf("DEBAND"))
            assertFalse(Regex("""\btexture\(""").containsMatchIn(code))
        }
    }

    @Test
    fun definesGoAfterTheVersionLineInOrder() {
        val lines = ShaderFactory.fragmentShaderSource(ShaderProgramSource.SharpShader, colorCorrection = true, listOf("DEBAND")).lines()
        assertEquals(listOf("#version 300 es", "#define COLOR_CORRECTION", "#define DEBAND"), lines.take(3))
        val linear = ShaderFactory.fragmentShaderSource(ShaderProgramSource.LinearShader, colorCorrection = true, listOf("DEBAND")).lines()
        assertEquals(listOf("#define COLOR_CORRECTION", "#define DEBAND"), linear.take(2))
    }

    @Test
    fun colorCorrectionAloneIsAsBefore() {
        val source = ShaderProgramSource.SharpShader.fragmentShaderSource
        val versionEnd = source.indexOf('\n') + 1
        val expected = source.substring(0, versionEnd) + "#define COLOR_CORRECTION\n" + source.substring(versionEnd)
        assertEquals(expected, ShaderFactory.fragmentShaderSource(ShaderProgramSource.SharpShader, colorCorrection = true))
        assertEquals(source, ShaderFactory.fragmentShaderSource(ShaderProgramSource.SharpShader, colorCorrection = false))
    }

    private fun compiledSource(source: ShaderProgramSource, colorCorrection: Boolean, extraDefines: List<String>): String {
        return preprocess(ShaderFactory.fragmentShaderSource(source, colorCorrection, extraDefines))
    }

    // The #define / #ifdef / #ifndef / #else / #endif subset the shaders use; GL_FRAGMENT_PRECISION_HIGH counts as defined
    private fun preprocess(code: String): String {
        val defined = mutableSetOf("GL_FRAGMENT_PRECISION_HIGH")
        // Whether each open block is taken
        val taken = ArrayDeque<Boolean>()
        val output = StringBuilder()
        for (line in code.lines()) {
            val trimmed = line.trim()
            val isActive = taken.all { it }
            when {
                trimmed.startsWith("#ifdef ") -> taken.addLast(trimmed.removePrefix("#ifdef ").trim() in defined)
                trimmed.startsWith("#ifndef ") -> taken.addLast(trimmed.removePrefix("#ifndef ").trim() !in defined)
                trimmed == "#else" -> taken.addLast(!taken.removeLast())
                trimmed == "#endif" -> taken.removeLast()
                trimmed.startsWith("#define ") -> if (isActive) defined.add(trimmed.removePrefix("#define ").trim())
                trimmed.startsWith("#") && !trimmed.startsWith("#version") -> fail("Unexpected directive: $trimmed")
                // Comments dropped, so a name in one doesn't count as a use
                isActive -> output.append(line.substringBefore("//")).append('\n')
            }
        }
        assertTrue("Unbalanced #ifdef", taken.isEmpty())
        return output.toString()
    }

    private val functionDefinition = Regex("""^\s*(?:void|bool|float|int|vec[234]|ivec[234])\s+(\w+)\s*\([^;]*\{\s*$""")
    // Top level only: globals start at the line's start
    private val globalDeclaration = Regex("""^(?:const\s+\w+|uniform\s+(?:highp\s+|mediump\s+)?\w+|varying\s+\w+|in\s+\w+|out\s+\w+)\s+(\w+)""")

    private fun declaredNames(code: String): List<Pair<String, Int>> {
        var offset = 0
        val names = ArrayList<Pair<String, Int>>()
        for (line in code.lines()) {
            (functionDefinition.find(line) ?: globalDeclaration.find(line))?.let { names.add(it.groupValues[1] to offset) }
            offset += line.length + 1
        }
        return names
    }

    private fun assertNoDuplicateNames(label: String, code: String) {
        val duplicates = declaredNames(code).groupBy { it.first }.filterValues { it.size > 1 }.keys
        assertTrue("$label declares $duplicates twice", duplicates.isEmpty())
    }

    private fun assertDeclaredBeforeUse(label: String, code: String) {
        val declarations = declaredNames(code).toMap()
        for (name in listOf("tex", "texSize", "topScreenWidth", "topScreenEnd", "bottomScreenStart", "screenColumns", "screenBand", "sampleScreen",
                "hash12", "debandRing", "deband", "DEBAND_THRESHOLD", "DEBAND_RANGE", "DEBAND_GRAIN")) {
            val declaredAt = declarations[name]
            if (declaredAt == null) {
                fail("$label doesn't declare $name")
                return
            }
            val firstUse = Regex("""\b$name\b""").find(code)!!.range.first
            assertTrue("$label uses $name before declaring it", firstUse >= declaredAt)
        }
    }
}
