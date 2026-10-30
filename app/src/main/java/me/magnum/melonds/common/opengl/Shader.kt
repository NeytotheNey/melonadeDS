package me.magnum.melonds.common.opengl

import android.opengl.GLES30

private const val INVALID_ATTRIBUTE = -1
private const val INVALID_UNIFORM = -1

class Shader(
    private val vertexShaderId: Int,
    private val fragmentShaderId: Int,
    private val programId: Int,
    val textureFiltering: Int,
) {
    val attribUv: Int
    val attribPos: Int
    val attribAlpha: Int
    val uniformTex: Int
    // Only declared by the filters that need to know the real size of the frame texture
    private val uniformTexSize: Int
    // Only declared by the filters that need to know the frame's width in DS pixels (see ShaderProgramSource)
    private val uniformTopScreenWidth: Int
    // Only declared by the filters that keep their samples off the parts beside a screen's middle (see ShaderProgramSource.screenColumns)
    private val uniformBottomScreenWide: Int

    init {
        GLES30.glUseProgram(programId)
        attribUv = GLES30.glGetAttribLocation(programId, "vUV")
        attribPos = GLES30.glGetAttribLocation(programId, "vPos")
        attribAlpha = GLES30.glGetAttribLocation(programId, "vAlpha")
        uniformTex = GLES30.glGetUniformLocation(programId, "tex")
        uniformTexSize = GLES30.glGetUniformLocation(programId, "texSize")
        uniformTopScreenWidth = GLES30.glGetUniformLocation(programId, "topScreenWidth")
        uniformBottomScreenWide = GLES30.glGetUniformLocation(programId, "bottomScreenWide")
        GLES30.glUseProgram(0)
    }

    fun use() {
        GLES30.glUseProgram(programId)
        GLES30.glEnableVertexAttribArray(attribUv)
        GLES30.glEnableVertexAttribArray(attribPos)
        if (attribAlpha != INVALID_ATTRIBUTE) {
            GLES30.glEnableVertexAttribArray(attribAlpha)
        }
    }

    /**
     * Sets the size of the texture being drawn, in texels. Must be called after [use]. Does nothing if the shader doesn't need it.
     */
    fun setTextureSize(width: Int, height: Int) {
        if (uniformTexSize != INVALID_UNIFORM) {
            GLES30.glUniform2f(uniformTexSize, width.toFloat(), height.toFloat())
        }
    }

    /**
     * Sets the width of the frame's top screen in DS pixels (PresentFrameWrapper.topScreenWidth). Must be called after [use]. Does nothing if
     * the shader doesn't need it.
     */
    fun setTopScreenWidth(width: Int) {
        if (uniformTopScreenWidth != INVALID_UNIFORM) {
            GLES30.glUniform1f(uniformTopScreenWidth, width.toFloat())
        }
    }

    /**
     * Sets which screen is drawn wide, with the parts of its half of the frame beside its middle SCREEN_WIDTH columns: the bottom one when the
     * screens are swapped, otherwise the top one. Must be called after [use]. Does nothing if the shader doesn't need it.
     */
    fun setBottomScreenWide(isBottomScreenWide: Boolean) {
        if (uniformBottomScreenWide != INVALID_UNIFORM) {
            GLES30.glUniform1f(uniformBottomScreenWide, if (isBottomScreenWide) 1f else 0f)
        }
    }

    fun delete() {
        GLES30.glDeleteShader(vertexShaderId)
        GLES30.glDeleteShader(fragmentShaderId)
        GLES30.glDeleteProgram(programId)
    }
}