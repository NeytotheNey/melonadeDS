package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import me.magnum.melonds.domain.model.SCREEN_GAP
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
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
    // Declared by the filters that need the frame's rows (ShaderProgramSource's frameLayout): where the screens' halves start and end
    private val uniformFrameLayout: Int
    // Only declared by the filters that keep their samples off the parts beside a screen's middle (see ShaderProgramSource.screenColumns)
    private val uniformBottomScreenWide: Int
    // Only declared by the tilt-shift pre-passes (see ShaderProgramSource.tiltShiftPrePassShader)
    private val uniformTiltShift: Int
    // Only declared by the tilt-shift pre-passes that apply the color boost: the blur's last one and the color-only one
    private val uniformTiltShiftBoost: Int
    // Only declared by the bloom pre-passes (see ShaderProgramSource.BloomBrightPrePassShader and the ones after it): the bloom's strength,
    // threshold and radius, the vignette's strength and size, and the blurred glow's texture, on unit 1
    private val uniformBloom: Int
    private val uniformVignette: Int
    private val uniformGlowTex: Int
    // Only declared by the depth of field pre-passes (see ShaderProgramSource.depthOfFieldPrePassShader): its radius, range and falloff, its
    // focus, and the frame's depth texture, on unit 1
    private val uniformDepthOfField: Int
    private val uniformDepthFocus: Int
    private val uniformDepthTex: Int
    // Only declared by ShaderProgramSource.NoFilterHalvesAt1xShader, with its coordinate textures (VideoFilterPipeline.bindHalvesAt1xCoordinates)
    private val uniformHalvesAt1x: Int
    private val uniformCoordXTex: Int
    private val uniformCoordYTex: Int

    init {
        GLES30.glUseProgram(programId)
        attribUv = GLES30.glGetAttribLocation(programId, "vUV")
        attribPos = GLES30.glGetAttribLocation(programId, "vPos")
        attribAlpha = GLES30.glGetAttribLocation(programId, "vAlpha")
        uniformTex = GLES30.glGetUniformLocation(programId, "tex")
        uniformTexSize = GLES30.glGetUniformLocation(programId, "texSize")
        uniformTopScreenWidth = GLES30.glGetUniformLocation(programId, "topScreenWidth")
        uniformFrameLayout = GLES30.glGetUniformLocation(programId, "frameLayout")
        uniformBottomScreenWide = GLES30.glGetUniformLocation(programId, "bottomScreenWide")
        uniformTiltShift = GLES30.glGetUniformLocation(programId, "tiltShift")
        uniformTiltShiftBoost = GLES30.glGetUniformLocation(programId, "tiltShiftBoost")
        uniformBloom = GLES30.glGetUniformLocation(programId, "bloom")
        uniformVignette = GLES30.glGetUniformLocation(programId, "vignette")
        uniformGlowTex = GLES30.glGetUniformLocation(programId, "glowTex")
        uniformDepthOfField = GLES30.glGetUniformLocation(programId, "depthOfField")
        uniformDepthFocus = GLES30.glGetUniformLocation(programId, "depthFocus")
        uniformDepthTex = GLES30.glGetUniformLocation(programId, "depthTex")
        uniformHalvesAt1x = GLES30.glGetUniformLocation(programId, "halvesAt1x")
        uniformCoordXTex = GLES30.glGetUniformLocation(programId, "coordXTex")
        uniformCoordYTex = GLES30.glGetUniformLocation(programId, "coordYTex")
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
     * Sets the height of the frame's top screen's half in DS pixels (PresentFrameWrapper.topScreenHeight: SCREEN_HEIGHT plus the WideMelon
     * camera's margin on both sides); call after [use].
     */
    fun setTopScreenHeight(height: Int) {
        if (uniformFrameLayout != INVALID_UNIFORM) {
            // As v coordinates: the top screen's half's end, the bottom screen's start and the middle of the gap between, then the frame's rows.
            // Worked out here in float like the constants they replace (192 / 386, 194 / 386, 0.5 and 386 without the camera's margin)
            val frameRows = (height + SCREEN_GAP + SCREEN_HEIGHT).toFloat()
            GLES30.glUniform4f(
                uniformFrameLayout,
                height / frameRows,
                (height + SCREEN_GAP) / frameRows,
                (height + SCREEN_GAP / 2) / frameRows,
                frameRows,
            )
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

    /**
     * Sets the bloom's strength, threshold and radius, the vignette's strength and size, and that the glow is on unit 1; call after [use].
     */
    fun setBloom(bloom: Bloom) {
        if (uniformBloom != INVALID_UNIFORM) {
            GLES30.glUniform3f(uniformBloom, bloom.strength, bloom.threshold, bloom.radius)
        }
        if (uniformVignette != INVALID_UNIFORM) {
            GLES30.glUniform2f(uniformVignette, bloom.vignette, bloom.vignetteSize)
        }
        if (uniformGlowTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformGlowTex, 1)
        }
    }

    /**
     * Sets the depth of field's radius at its strength, range and falloff, its focus point as [focusU] and [focusV] of the depth texture (read
     * when it has no focus depth), and that the depth texture is on unit 1; call after [use].
     */
    fun setDepthOfField(depthOfField: DepthOfField, focusU: Float, focusV: Float) {
        if (uniformDepthOfField != INVALID_UNIFORM) {
            GLES30.glUniform3f(uniformDepthOfField, depthOfField.appliedRadius, depthOfField.range, depthOfField.falloff)
        }
        if (uniformDepthFocus != INVALID_UNIFORM) {
            GLES30.glUniform3f(uniformDepthFocus, depthOfField.focusDepth, focusU, focusV)
        }
        if (uniformDepthTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformDepthTex, 1)
        }
    }

    /**
     * Sets which screens the frame has at 1x, its internal scale and the scale those screens were drawn at (PresentFrameWrapper.halvesAt1x,
     * scaleAt1x and halfScaleAt1x), and that the coordinate textures are on units 1 and 2 (VideoFilterPipeline.bindHalvesAt1xCoordinates); call
     * after [use].
     */
    fun setHalvesAt1x(halvesAt1x: Int, scale: Int, halfScale: Int) {
        if (uniformHalvesAt1x != INVALID_UNIFORM) {
            GLES30.glUniform3i(uniformHalvesAt1x, halvesAt1x, scale, halfScale)
        }
        if (uniformCoordXTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformCoordXTex, 1)
        }
        if (uniformCoordYTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformCoordYTex, 2)
        }
    }

    fun delete() {
        GLES30.glDeleteShader(vertexShaderId)
        GLES30.glDeleteShader(fragmentShaderId)
        GLES30.glDeleteProgram(programId)
    }
}