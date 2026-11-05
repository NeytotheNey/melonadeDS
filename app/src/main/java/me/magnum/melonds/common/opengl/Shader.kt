package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import me.magnum.melonds.domain.model.render.TiltShift

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
    // Only declared by the tilt-shift pre-passes (see ShaderProgramSource.tiltShiftPrePassShader)
    private val uniformTiltShift: Int
    // Only declared by the last of those pre-passes, which applies the color boost
    private val uniformTiltShiftBoost: Int

    init {
        GLES30.glUseProgram(programId)
        attribUv = GLES30.glGetAttribLocation(programId, "vUV")
        attribPos = GLES30.glGetAttribLocation(programId, "vPos")
        attribAlpha = GLES30.glGetAttribLocation(programId, "vAlpha")
        uniformTex = GLES30.glGetUniformLocation(programId, "tex")
        uniformTexSize = GLES30.glGetUniformLocation(programId, "texSize")
        uniformTopScreenWidth = GLES30.glGetUniformLocation(programId, "topScreenWidth")
        uniformBottomScreenWide = GLES30.glGetUniformLocation(programId, "bottomScreenWide")
        uniformTiltShift = GLES30.glGetUniformLocation(programId, "tiltShift")
        uniformTiltShiftBoost = GLES30.glGetUniformLocation(programId, "tiltShiftBoost")
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
     * Sets the size of the texture being drawn, in texels; call after [use].
     */
    fun setTextureSize(width: Int, height: Int) {
        if (uniformTexSize != INVALID_UNIFORM) {
            GLES30.glUniform2f(uniformTexSize, width.toFloat(), height.toFloat())
        }
    }

    /**
     * Sets the width of the frame's top screen in DS pixels (PresentFrameWrapper.topScreenWidth); call after [use].
     */
    fun setTopScreenWidth(width: Int) {
        if (uniformTopScreenWidth != INVALID_UNIFORM) {
            GLES30.glUniform1f(uniformTopScreenWidth, width.toFloat())
        }
    }

    /**
     * Sets whether the bottom screen (when swapped) rather than the top one is drawn wide; call after [use].
     */
    fun setBottomScreenWide(isBottomScreenWide: Boolean) {
        if (uniformBottomScreenWide != INVALID_UNIFORM) {
            GLES30.glUniform1f(uniformBottomScreenWide, if (isBottomScreenWide) 1f else 0f)
        }
    }

    /**
     * Sets the sharp band's edges, the blur's radius on each side of it and the color boost; call after [use].
     */
    fun setTiltShift(tiltShift: TiltShift) {
        if (uniformTiltShift != INVALID_UNIFORM) {
            GLES30.glUniform4f(uniformTiltShift, tiltShift.bandTop, tiltShift.bandBottom, tiltShift.topRadius, tiltShift.bottomRadius)
        }
        if (uniformTiltShiftBoost != INVALID_UNIFORM) {
            GLES30.glUniform2f(uniformTiltShiftBoost, tiltShift.appliedSaturation, tiltShift.appliedContrast)
        }
    }

    fun delete() {
        GLES30.glDeleteShader(vertexShaderId)
        GLES30.glDeleteShader(fragmentShaderId)
        GLES30.glDeleteProgram(programId)
    }
}