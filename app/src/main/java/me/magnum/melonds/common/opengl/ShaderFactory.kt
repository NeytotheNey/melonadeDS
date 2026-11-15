package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import android.util.Log

object ShaderFactory {
    private const val TAG = "ShaderFactory"

    /**
     * Compiles and links [source]; with [colorCorrection] `COLOR_CORRECTION` is defined in the fragment shader, and a failed build is logged but
     * still returned.
     */
    fun createShaderProgram(source: ShaderProgramSource, colorCorrection: Boolean = false): Shader {
        return buildShaderProgram(source, colorCorrection, deleteOnFailure = false)!!
    }

    /**
     * Like [createShaderProgram], but returns null (deleting what was built) if a shader doesn't compile or the program doesn't link. Each of
     * [extraDefines] is defined in the fragment shader too, after `COLOR_CORRECTION`.
     */
    fun createShaderProgramOrNull(source: ShaderProgramSource, colorCorrection: Boolean = false, extraDefines: List<String> = emptyList()): Shader? {
        return buildShaderProgram(source, colorCorrection, deleteOnFailure = true, extraDefines)
    }

    /**
     * Like [createShaderProgram], for the screen shader, falling back to the no-filter shader if it doesn't build (calling [onFallback] first).
     */
    fun createScreenShaderProgram(source: ShaderProgramSource, colorCorrection: Boolean = false, onFallback: (() -> Unit)? = null): Shader {
        if (source === ShaderProgramSource.NoFilterShader) {
            return createShaderProgram(source, colorCorrection)
        }

        return createShaderProgramOrNull(source, colorCorrection) ?: run {
            Log.e(TAG, "Screen shader failed to build. Falling back to no filter")
            onFallback?.invoke()
            createShaderProgram(ShaderProgramSource.NoFilterShader, colorCorrection)
        }
    }

    private fun buildShaderProgram(
        source: ShaderProgramSource,
        colorCorrection: Boolean,
        deleteOnFailure: Boolean,
        extraDefines: List<String> = emptyList(),
    ): Shader? {
        val vertexShader = createShader(GLES30.GL_VERTEX_SHADER, source.vertexShaderSource)
        val fragmentCode = fragmentShaderSource(source, colorCorrection, extraDefines)
        val fragmentShader = createShader(GLES30.GL_FRAGMENT_SHADER, fragmentCode)
        val shaderProgram = createShaderProgram(vertexShader, fragmentShader)

        if (deleteOnFailure && !(isShaderCompiled(vertexShader) && isShaderCompiled(fragmentShader) && isProgramLinked(shaderProgram))) {
            GLES30.glDeleteShader(vertexShader)
            GLES30.glDeleteShader(fragmentShader)
            GLES30.glDeleteProgram(shaderProgram)
            return null
        }

        val textureFilter = when (source.textureFiltering) {
            ShaderProgramSource.TextureFiltering.NEAREST -> GLES30.GL_NEAREST
            ShaderProgramSource.TextureFiltering.LINEAR -> GLES30.GL_LINEAR
        }

        return Shader(vertexShader, fragmentShader, shaderProgram, textureFilter)
    }

    /**
     * [source]'s fragment shader as it's compiled: `COLOR_CORRECTION` defined with [colorCorrection], then each of [extraDefines], in order.
     * Pure, so tests can check what a variant assembles to.
     */
    internal fun fragmentShaderSource(source: ShaderProgramSource, colorCorrection: Boolean, extraDefines: List<String> = emptyList()): String {
        val names = if (colorCorrection) listOf("COLOR_CORRECTION") + extraDefines else extraDefines
        return addDefines(source.fragmentShaderSource, names)
    }

    private fun addDefines(code: String, names: List<String>): String {
        if (names.isEmpty()) {
            return code
        }
        val defines = names.joinToString("") { "#define $it\n" }
        // Nothing but comments and whitespace can come before #version, so the defines go on the line after it
        if (code.startsWith("#version")) {
            val versionLineEnd = code.indexOf('\n')
            if (versionLineEnd >= 0) {
                return code.substring(0, versionLineEnd + 1) + defines + code.substring(versionLineEnd + 1)
            }
        }
        return defines + code
    }

    private fun createShaderProgram(vertexShader: Int, fragmentShader: Int): Int {
        val program = GLES30.glCreateProgram()
        GLES30.glAttachShader(program, vertexShader)
        GLES30.glAttachShader(program, fragmentShader)
        GLES30.glLinkProgram(program)

        if (!isProgramLinked(program)) {
            Log.e(TAG, GLES30.glGetProgramInfoLog(program))
        }

        return program
    }

    private fun createShader(shaderType: Int, code: String): Int {
        val shader = GLES30.glCreateShader(shaderType)
        GLES30.glShaderSource(shader, code)
        GLES30.glCompileShader(shader)

        if (!isShaderCompiled(shader)) {
            Log.e(TAG, GLES30.glGetShaderInfoLog(shader))
        }

        return shader
    }

    private fun isShaderCompiled(shader: Int): Boolean {
        val result = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, result, 0)
        return result[0] != GLES30.GL_FALSE
    }

    private fun isProgramLinked(program: Int): Boolean {
        val result = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, result, 0)
        return result[0] != GLES30.GL_FALSE
    }
}
