package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import me.magnum.melonds.domain.model.SCREEN_GAP
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
import me.magnum.melonds.domain.model.render.FrameGuard
import me.magnum.melonds.domain.model.render.Shade
import me.magnum.melonds.domain.model.render.Sun
import me.magnum.melonds.domain.model.render.TiltShift
import kotlin.math.sqrt

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
    // Declared by the filters that keep their samples off the parts beside a screen's middle and by the bloom's combine pass (its vignette): the
    // sun shadows' guard band in DS pixels (PresentFrameWrapper.topScreenGuard), which says where the native screens are
    private val uniformTopScreenGuard: Int
    // Only declared by the tilt-shift pre-passes (see ShaderProgramSource.tiltShiftPrePassShader)
    private val uniformTiltShift: Int
    // Only declared by the tilt-shift pre-passes that apply the color boost: the blur's last one and the color-only one
    private val uniformTiltShiftBoost: Int
    // Only declared by the bloom pre-passes (see ShaderProgramSource.BloomBrightPrePassShader and the ones after it): the bloom's strength,
    // threshold and radius, the vignette's strength and size, the blurred glow's texture, on unit 1, and the haze's strength, start, falloff
    // and color, with the frame's depth texture on unit 2
    private val uniformBloom: Int
    private val uniformVignette: Int
    private val uniformGlowTex: Int
    private val uniformHaze: Int
    private val uniformHazeColor: Int
    private val uniformHazeDepthTex: Int
    // Only declared by the bloom's bright pass and the shading's and sun shadows' apply passes with LIGHTS defined: the lights' threshold and
    // strength
    private val uniformBloomLights: Int
    // Only declared by the depth of field pre-passes (see ShaderProgramSource.depthOfFieldPrePassShader and DepthOfFieldFocusShader): its radius,
    // range and falloff, its focus point (the focus pass), the frame's projection's depth terms (ShaderProgramSource's DEPTH_DISTANCE_FUNCTIONS),
    // the frame's depth texture on unit 1 and the picked focus on unit 2 (the blur passes).
    // The bloom's 3D-only bright pass declares the depth texture too, also on unit 1
    private val uniformDepthOfField: Int
    private val uniformDepthFocus: Int
    private val uniformDepthProjection: Int
    private val uniformDepthTex: Int
    private val uniformFocusTex: Int
    // Only declared by the shading's pre-passes (see ShaderProgramSource.ShadeDownsamplePrePassShader and the ones after it): its strength,
    // radius and range, its tint, and for its last pass the frame's depth texture on unit 1 and the blurred depth on unit 2
    private val uniformShade: Int
    private val uniformShadeColor: Int
    private val uniformShadeDepthTex: Int
    private val uniformShadeBlurTex: Int
    // Only declared by the sun shadows' pre-passes (see ShaderProgramSource.SunMarchPrePassShader and the ones after it): their values and
    // camera, and for the last pass the frame's depth texture on unit 1 and the shadow on unit 2
    private val uniformSun: Int
    private val uniformSunDirection: Int
    private val uniformSunSoftness: Int
    private val uniformSunColor: Int
    private val uniformSunProjection: Int
    private val uniformSunNear: Int
    private val uniformSunInverse: Int
    private val uniformSunViewport: Int
    private val uniformSunDepthTex: Int
    private val uniformSunMaskTex: Int
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
        uniformTopScreenGuard = GLES30.glGetUniformLocation(programId, "topScreenGuard")
        uniformTiltShift = GLES30.glGetUniformLocation(programId, "tiltShift")
        uniformTiltShiftBoost = GLES30.glGetUniformLocation(programId, "tiltShiftBoost")
        uniformBloom = GLES30.glGetUniformLocation(programId, "bloom")
        uniformVignette = GLES30.glGetUniformLocation(programId, "vignette")
        uniformGlowTex = GLES30.glGetUniformLocation(programId, "glowTex")
        uniformHaze = GLES30.glGetUniformLocation(programId, "haze")
        uniformHazeColor = GLES30.glGetUniformLocation(programId, "hazeColor")
        uniformHazeDepthTex = GLES30.glGetUniformLocation(programId, "hazeDepthTex")
        uniformBloomLights = GLES30.glGetUniformLocation(programId, "bloomLights")
        uniformDepthOfField = GLES30.glGetUniformLocation(programId, "depthOfField")
        uniformDepthFocus = GLES30.glGetUniformLocation(programId, "depthFocus")
        uniformDepthProjection = GLES30.glGetUniformLocation(programId, "depthProjection")
        uniformDepthTex = GLES30.glGetUniformLocation(programId, "depthTex")
        uniformFocusTex = GLES30.glGetUniformLocation(programId, "focusTex")
        uniformShade = GLES30.glGetUniformLocation(programId, "shade")
        uniformShadeColor = GLES30.glGetUniformLocation(programId, "shadeColor")
        uniformShadeDepthTex = GLES30.glGetUniformLocation(programId, "shadeDepthTex")
        uniformShadeBlurTex = GLES30.glGetUniformLocation(programId, "shadeBlurTex")
        uniformSun = GLES30.glGetUniformLocation(programId, "sun")
        uniformSunDirection = GLES30.glGetUniformLocation(programId, "sunDirection")
        uniformSunSoftness = GLES30.glGetUniformLocation(programId, "sunSoftness")
        uniformSunColor = GLES30.glGetUniformLocation(programId, "sunColor")
        uniformSunProjection = GLES30.glGetUniformLocation(programId, "sunProjection")
        uniformSunNear = GLES30.glGetUniformLocation(programId, "sunNear")
        uniformSunInverse = GLES30.glGetUniformLocation(programId, "sunInverse")
        uniformSunViewport = GLES30.glGetUniformLocation(programId, "sunViewport")
        uniformSunDepthTex = GLES30.glGetUniformLocation(programId, "sunDepthTex")
        uniformSunMaskTex = GLES30.glGetUniformLocation(programId, "sunMaskTex")
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
     * Sets the sun shadows' guard band in DS pixels (PresentFrameWrapper.topScreenGuard): left, right, top and bottom, so the native screens'
     * columns are found where they are and the vignette goes over the top screen as it's shown, without it; call after [use].
     */
    fun setTopScreenGuard(guard: FrameGuard) {
        if (uniformTopScreenGuard != INVALID_UNIFORM) {
            GLES30.glUniform4f(uniformTopScreenGuard, guard.left.toFloat(), guard.right.toFloat(), guard.top.toFloat(), guard.bottom.toFloat())
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
     * Sets the bloom's strength, threshold and radius, the vignette's strength and size, the haze's strength, start, falloff and color, the
     * lights' threshold and strength, that the glow is on unit 1, and that the depth texture is on unit 1 for the 3D-only and lights bright
     * passes and on unit 2 for the haze; call after [use].
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
        if (uniformDepthTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformDepthTex, 1)
        }
        if (uniformHaze != INVALID_UNIFORM) {
            GLES30.glUniform3f(uniformHaze, bloom.haze, bloom.hazeStart, bloom.hazeFalloff)
        }
        if (uniformHazeColor != INVALID_UNIFORM) {
            val color = bloom.hazeColor
            GLES30.glUniform3f(
                uniformHazeColor, (color shr 16 and 0xFF) / 255f, (color shr 8 and 0xFF) / 255f, (color and 0xFF) / 255f,
            )
        }
        if (uniformHazeDepthTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformHazeDepthTex, 2)
        }
        if (uniformBloomLights != INVALID_UNIFORM) {
            GLES30.glUniform2f(uniformBloomLights, bloom.lightThreshold, bloom.lightStrength)
        }
    }

    /**
     * Sets the depth of field's radius at its strength, range and falloff, the frame's fixed [focusDepth] (negative for none) and its focus
     * point as [focusU] and [focusV] of the depth texture (read when there's no focus depth), whether this frame ages a held focus
     * ([focusAgeStep], 0 or 1), the frame's projection's [depthProjection] terms (P10, P11, P14 and P15), and that the depth texture is on
     * unit 1 and the picked focus on unit 2; call after [use].
     */
    fun setDepthOfField(depthOfField: DepthOfField, focusDepth: Float, focusU: Float, focusV: Float, focusAgeStep: Float, depthProjection: FloatArray) {
        if (uniformDepthOfField != INVALID_UNIFORM) {
            GLES30.glUniform3f(uniformDepthOfField, depthOfField.appliedRadius, depthOfField.range, depthOfField.falloff)
        }
        if (uniformDepthFocus != INVALID_UNIFORM) {
            GLES30.glUniform4f(uniformDepthFocus, focusDepth, focusU, focusV, focusAgeStep)
        }
        if (uniformDepthProjection != INVALID_UNIFORM) {
            GLES30.glUniform4f(uniformDepthProjection, depthProjection[0], depthProjection[1], depthProjection[2], depthProjection[3])
        }
        if (uniformDepthTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformDepthTex, 1)
        }
        if (uniformFocusTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformFocusTex, 2)
        }
    }

    /**
     * Sets the shading's strength, radius, range and tint, and that the frame's depth texture is on unit 1 and the blurred depth on unit 2;
     * call after [use].
     */
    fun setShade(shade: Shade) {
        if (uniformShade != INVALID_UNIFORM) {
            GLES30.glUniform3f(uniformShade, shade.strength, shade.radius, shade.range)
        }
        if (uniformShadeColor != INVALID_UNIFORM) {
            val color = shade.color
            GLES30.glUniform3f(
                uniformShadeColor, (color shr 16 and 0xFF) / 255f, (color shr 8 and 0xFF) / 255f, (color and 0xFF) / 255f,
            )
        }
        if (uniformShadeDepthTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformShadeDepthTex, 1)
        }
        if (uniformShadeBlurTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformShadeBlurTex, 2)
        }
    }

    /**
     * Sets the sun shadows' values and the depth camera they're marched with: [projection] and [inverse] (its inverse), 16 floats each
     * column-major, and [viewport] (left, top, width, height in DS pixels of the depth texture's top half); and that the frame's depth texture
     * is on unit 1 and the shadow on unit 2. The direction is normalized here. Call after [use].
     */
    fun setSun(sun: Sun, projection: FloatArray, inverse: FloatArray, viewport: FloatArray) {
        if (uniformSun != INVALID_UNIFORM) {
            GLES30.glUniform4f(uniformSun, sun.strength, sun.length, sun.steps.toFloat(), sun.bias)
        }
        if (uniformSunDirection != INVALID_UNIFORM) {
            val length = sqrt(sun.directionX * sun.directionX + sun.directionY * sun.directionY + sun.directionZ * sun.directionZ)
            val scale = if (length > 0f) 1f / length else 0f
            GLES30.glUniform3f(uniformSunDirection, sun.directionX * scale, sun.directionY * scale, sun.directionZ * scale)
        }
        if (uniformSunSoftness != INVALID_UNIFORM) {
            GLES30.glUniform1f(uniformSunSoftness, sun.softness)
        }
        if (uniformSunColor != INVALID_UNIFORM) {
            val color = sun.color
            GLES30.glUniform3f(uniformSunColor, (color shr 16 and 0xFF) / 255f, (color shr 8 and 0xFF) / 255f, (color and 0xFF) / 255f)
        }
        if (uniformSunProjection != INVALID_UNIFORM) {
            GLES30.glUniformMatrix4fv(uniformSunProjection, 1, false, projection, 0)
        }
        if (uniformSunNear != INVALID_UNIFORM) {
            GLES30.glUniform1f(uniformSunNear, sun.near)
        }
        if (uniformSunInverse != INVALID_UNIFORM) {
            GLES30.glUniformMatrix4fv(uniformSunInverse, 1, false, inverse, 0)
        }
        if (uniformSunViewport != INVALID_UNIFORM) {
            GLES30.glUniform4f(uniformSunViewport, viewport[0], viewport[1], viewport[2], viewport[3])
        }
        if (uniformSunDepthTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformSunDepthTex, 1)
        }
        if (uniformSunMaskTex != INVALID_UNIFORM) {
            GLES30.glUniform1i(uniformSunMaskTex, 2)
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