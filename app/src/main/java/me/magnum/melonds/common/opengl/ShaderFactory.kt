package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import android.util.Log

object ShaderFactory {
    /**
     * Compiles and links the given shaders. With [colorCorrection], COLOR_CORRECTION is defined in the fragment shader, which turns on the DS
     * screen colours in the screen shaders' finishColor(). Shaders that don't use it are unaffected.
     */
    fun createShaderProgram(source: ShaderProgramSource, colorCorrection: Boolean = false): Shader {
        val vertexShader = createShader(GLES30.GL_VERTEX_SHADER, source.vertexShaderSource)
        val fragmentShaderSource = if (colorCorrection) {
            addDefine(source.fragmentShaderSource, "COLOR_CORRECTION")
        } else {
            source.fragmentShaderSource
        }
        val fragmentShader = createShader(GLES30.GL_FRAGMENT_SHADER, fragmentShaderSource)
        val shaderProgram = createShaderProgram(vertexShader, fragmentShader)
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
        val result = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, result, 0)

        if (result[0] == GLES30.GL_FALSE) {
            Log.e("ShaderFactory", GLES30.glGetProgramInfoLog(program))
        }

        return program
    }

    private fun createShader(shaderType: Int, code: String): Int {
        val shader = GLES30.glCreateShader(shaderType)
        GLES30.glShaderSource(shader, code)
        GLES30.glCompileShader(shader)
        val result = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, result, 0)

        if (result[0] == GLES30.GL_FALSE) {
            Log.e("ShaderFactory", GLES30.glGetShaderInfoLog(shader))
        }

        return shader
    }
}