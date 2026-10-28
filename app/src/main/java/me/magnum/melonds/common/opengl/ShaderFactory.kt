package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import android.util.Log

object ShaderFactory {
    private const val TAG = "ShaderFactory"

    /**
     * Compiles and links the given shaders. With [colorCorrection], COLOR_CORRECTION is defined in the fragment shader, which turns on the DS
     * screen colours in the screen shaders' finishColor(). Shaders that don't use it are unaffected. If a shader doesn't compile or the program
     * doesn't link, the error is logged and the program is still returned, but it draws nothing. Screen shaders use [createScreenShaderProgram]
     * instead.
     */
    fun createShaderProgram(source: ShaderProgramSource, colorCorrection: Boolean = false): Shader {
        return buildShaderProgram(source, colorCorrection, deleteOnFailure = false)!!
    }

    /**
     * Like [createShaderProgram], but returns null if a shader doesn't compile or the program doesn't link. The error is logged, and nothing is
     * left behind.
     */
    fun createShaderProgramOrNull(source: ShaderProgramSource, colorCorrection: Boolean = false): Shader? {
        return buildShaderProgram(source, colorCorrection, deleteOnFailure = true)
    }

    /**
     * Like [createShaderProgram], for the shader that draws the screens. If it doesn't compile or link, the no-filter shader is used instead, so
     * that the screens are still drawn.
     */
    fun createScreenShaderProgram(source: ShaderProgramSource, colorCorrection: Boolean = false): Shader {
        if (source === ShaderProgramSource.NoFilterShader) {
            return createShaderProgram(source, colorCorrection)
        }

        return createShaderProgramOrNull(source, colorCorrection) ?: run {
            Log.e(TAG, "Screen shader failed to build. Falling back to no filter")
            createShaderProgram(ShaderProgramSource.NoFilterShader, colorCorrection)
        }
    }

    private fun buildShaderProgram(source: ShaderProgramSource, colorCorrection: Boolean, deleteOnFailure: Boolean): Shader? {
        val vertexShader = createShader(GLES30.GL_VERTEX_SHADER, source.vertexShaderSource)
        val fragmentShaderSource = if (colorCorrection) {
            addDefine(source.fragmentShaderSource, "COLOR_CORRECTION")
        } else {
            source.fragmentShaderSource
        }
        val fragmentShader = createShader(GLES30.GL_FRAGMENT_SHADER, fragmentShaderSource)
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

    private fun addDefine(code: String, name: String): String {
        // Nothing but comments and whitespace can come before #version, so the define goes on the line after it
        if (code.startsWith("#version")) {
            val versionLineEnd = code.indexOf('\n')
            if (versionLineEnd >= 0) {
                return code.substring(0, versionLineEnd + 1) + "#define $name\n" + code.substring(versionLineEnd + 1)
            }
        }
        return "#define $name\n$code"
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
