package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import android.util.Log
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.domain.model.render.Shade
import me.magnum.melonds.domain.model.render.TiltShift
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The texture that a [VideoFilterPipeline]'s screen shader should draw and its size in texels, reused by every
 * [VideoFilterPipeline.renderPrePasses] call.
 */
class FilterOutput {
    var textureId = 0
        private set
    var width = 0
        private set
    var height = 0
        private set

    internal fun set(textureId: Int, width: Int, height: Int) {
        this.textureId = textureId
        this.width = width
        this.height = height
    }
}

/**
 * The GL objects of a [VideoFilter] (screen shader, pre-pass shaders and textures), created, used and deleted on the GL context's thread.
 */
class VideoFilterPipeline(private val filter: VideoFilter, private val colorCorrection: Boolean = false) {

    private companion object {
        const val TAG = "VideoFilterPipeline"
        const val DEBAND_DEFINE = "DEBAND"
        // The bloom's last pass without the glow, and with the haze (ShaderProgramSource.BloomCombinePrePassShader)
        const val NO_GLOW_DEFINE = "NO_GLOW"
        const val HAZE_DEFINE = "HAZE"
        // The bloom's bright pass that only takes 3D pixels (ShaderProgramSource.BloomBrightPrePassShader)
        const val ONLY_3D_DEFINE = "ONLY_3D"
        // The combine pass's variants, as bits of an index into combinePasses; -1 is no combine pass
        const val COMBINE_GLOW = 1
        const val COMBINE_HAZE = 2
        const val COMBINE_VARIANTS = 4
        const val NO_COMBINE = -1
        val COLOR_ATTACHMENTS = intArrayOf(GLES30.GL_COLOR_ATTACHMENT0)
    }

    // A pre-pass that fails to build is left out, so the screen shader gets its input instead
    // Whether the filter's screen shader didn't build, so screenShader is the no-filter one, which can't take deband
    private var isScreenShaderFallback = false
    val screenShader: Shader = ShaderFactory.createScreenShaderProgram(filter.screenShader, colorCorrection) { isScreenShaderFallback = true }
    // With VideoFilter.deband, the deband pre-pass, which runs before the filter's own unless renderPrePasses() is told the screen shader does it
    private val debandPass = if (filter.deband) {
        ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.DebandPrePassShader)?.let { PrePass(it, 1, false) }
    } else {
        null
    }
    private val prePasses = filter.prePasses.mapNotNull { prePass ->
        ShaderFactory.createShaderProgramOrNull(prePass.source)?.let { PrePass(it, prePass.scale, prePass.nativeResolutionOnly) }
    }

    // debug.melonade.debandfold: the screen shader with DEBAND defined, built the first time a frame folds deband in (see shouldFoldDeband), and
    // null if it doesn't build, which leaves the pre-pass
    private var debandScreenShader: Shader? = null
    private var isDebandScreenShaderBuilt = false

    /**
     * Whether deband can run inside the screen shader instead of the pre-pass: deband is on and its pre-pass built, the filter takes it
     * ([VideoFilter.canFoldDeband]) and its screen shader built, and the `DEBAND` screen shader didn't fail to build. Only looks at state, so it's
     * free to ask on every frame.
     */
    val canFoldDeband: Boolean
        get() = debandPass != null && filter.canFoldDeband && !isScreenShaderFallback && !(isDebandScreenShaderBuilt && debandScreenShader == null)

    /**
     * The screen shader with deband in it, with this pipeline's color correction, built the first time it's asked for; null if it couldn't be
     * built (then the pre-pass debands as before). Ask it before [renderPrePasses], and pass `foldDeband` only when it isn't null.
     */
    fun getDebandScreenShader(): Shader? {
        if (!isDebandScreenShaderBuilt) {
            isDebandScreenShaderBuilt = true
            debandScreenShader = ShaderFactory.createShaderProgramOrNull(filter.screenShader, colorCorrection, listOf(DEBAND_DEFINE))
            if (debandScreenShader == null) {
                Log.e(TAG, "The deband screen shader failed to build, so deband stays a pre-pass")
            }
        }
        return debandScreenShader
    }

    // debug.melonade.comp1x: frames with screens the compositor drew at 1x (PresentFrameWrapper.halvesAt1x). This pipeline draws them with the 1x
    // shader when it's only the no-filter shader (see canDrawHalvesAt1x); otherwise renderPrePasses() first expands them into the full frame with
    // the expand pass. Both are built the first time such a frame needs them, so nothing is built while there are none, and stay null if they
    // don't build
    private val isNoFilter = filter.screenShader === ShaderProgramSource.NoFilterShader && debandPass == null && prePasses.isEmpty()
    private var halvesAt1xShader: Shader? = null
    private var isHalvesAt1xShaderBuilt = false
    private var expandPass: PrePass? = null
    private var isExpandPassBuilt = false
    // The frame the expand pass's target holds, as frame pacing presents the same frame more than once (and again and again while paused)
    private var expandedFrameTextureId = 0
    private var expandedFrameSequence = 0L
    private var expandedHalvesAt1x = 0
    private var expandedHalfScaleAt1x = 1
    private var expandedWidth = 0
    private var expandedHeight = 0
    // The 1x shader's coordinate textures (R16UI, see bindHalvesAt1xCoordinates), for frames of coordinatesWidth x coordinatesHeight
    private val coordinateTextures = IntArray(2)
    private var coordinatesWidth = 0
    private var coordinatesHeight = 0

    /**
     * debug.melonade.comp1x: binds the 1x shader's coordinate textures for a frame of [width] x [height] texels on units 1 (the frame's width x 1,
     * texel x holding x) and 2 (1 x its height, texel y holding y), made again when the size changes, and leaves unit 0 active. Sampled nearest at
     * uv, they give the texel the frame's nearest sample there picks (see ShaderProgramSource.NoFilterHalvesAt1xShader).
     */
    fun bindHalvesAt1xCoordinates(width: Int, height: Int) {
        if (coordinateTextures[0] == 0) {
            GLES30.glGenTextures(2, coordinateTextures, 0)
        }
        val isResized = width != coordinatesWidth || height != coordinatesHeight
        val coordinates = if (isResized) {
            val count = maxOf(width, height, 1)
            ByteBuffer.allocateDirect(count * Short.SIZE_BYTES).order(ByteOrder.nativeOrder()).asShortBuffer().apply {
                for (i in 0 until count) {
                    put(i.toShort())
                }
                position(0)
            }
        } else {
            null
        }
        val savedAlignment = IntArray(1)
        if (coordinates != null) {
            GLES30.glGetIntegerv(GLES30.GL_UNPACK_ALIGNMENT, savedAlignment, 0)
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 2)
        }
        for (axis in 0..1) {
            GLES30.glActiveTexture(GLES30.GL_TEXTURE1 + axis)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, coordinateTextures[axis])
            if (coordinates != null) {
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
                GLES30.glTexImage2D(
                    GLES30.GL_TEXTURE_2D, 0, GLES30.GL_R16UI, if (axis == 0) width else 1, if (axis == 0) 1 else height, 0, GLES30.GL_RED_INTEGER,
                    GLES30.GL_UNSIGNED_SHORT, coordinates,
                )
            }
        }
        if (coordinates != null) {
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, savedAlignment[0])
            coordinatesWidth = width
            coordinatesHeight = height
        }
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    /**
     * Whether a frame with screens at 1x drawn through [getHalvesAt1xShader] looks the same as the full frame through this pipeline: only the
     * no-filter shader with no pre-pass, as long as the 1x shader didn't fail to build. A Lua tilt-shift, bloom or vignette adds passes of its
     * own, which the caller checks. Only looks at state, so it's free to ask on every frame.
     */
    val canDrawHalvesAt1x: Boolean
        get() = isNoFilter && !(isHalvesAt1xShaderBuilt && halvesAt1xShader == null)

    /**
     * The shader that draws a frame with screens at 1x, nearest and without any pre-pass, with this pipeline's color correction, built the first
     * time it's asked for; null if it couldn't be built (then renderPrePasses() expands the frame and the screen shader draws that).
     */
    fun getHalvesAt1xShader(): Shader? {
        if (!isHalvesAt1xShaderBuilt) {
            isHalvesAt1xShaderBuilt = true
            halvesAt1xShader = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.NoFilterHalvesAt1xShader, colorCorrection)
            if (halvesAt1xShader == null) {
                Log.e(TAG, "The 1x screen shader failed to build")
            }
        }
        return halvesAt1xShader
    }

    private fun getExpandPass(): PrePass? {
        if (!isExpandPassBuilt) {
            isExpandPassBuilt = true
            expandPass = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.ExpandHalvesAt1xPrePassShader)?.let { PrePass(it, 1, false) }
            if (expandPass == null) {
                Log.e(TAG, "The 1x expand shader failed to build")
            }
        }
        return expandPass
    }

    /**
     * debug.melonade.comp1x: [frame]'s texture with its screens at 1x expanded, texel for texel, into the full frame the compositor would have
     * drawn, in the expand pass's target (the frame's size); the frame's own texture if that can't be done. Saves and restores the GL state it
     * changes, like the other passes.
     */
    private fun expandHalvesAt1x(frame: PresentFrameWrapper): Int {
        val width = frame.textureWidth
        val height = frame.textureHeight
        val pass = getExpandPass()
        if (pass == null || width <= 0 || height <= 0) {
            return frame.textureId
        }

        val isExpanded = frame.frameSequence != 0L && frame.frameSequence == expandedFrameSequence && frame.textureId == expandedFrameTextureId &&
                frame.halvesAt1x == expandedHalvesAt1x && frame.halfScaleAt1x == expandedHalfScaleAt1x && width == expandedWidth && height == expandedHeight
        if (isExpanded) {
            return pass.texture
        }

        saveState()
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        if (quadVao == 0) {
            createQuad()
        }
        GLES30.glBindVertexArray(quadVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, quadVbo)
        val isReady = pass.prepareTarget(width, height)
        if (isReady) {
            GLES30.glViewport(0, 0, width, height)
            pass.render(
                frame.textureId, width, height, frame.topScreenWidth, frame.topScreenHeight, false, null, frame.halvesAt1x, frame.scaleAt1x,
                frame.halfScaleAt1x,
            )
        }
        restoreState()

        if (!isReady) {
            expandedFrameSequence = 0L
            return frame.textureId
        }
        expandedFrameTextureId = frame.textureId
        expandedFrameSequence = frame.frameSequence
        expandedHalvesAt1x = frame.halvesAt1x
        expandedHalfScaleAt1x = frame.halfScaleAt1x
        expandedWidth = width
        expandedHeight = height
        return pass.texture
    }

    // Built the first time a script asks for the blur, so that everyone else pays nothing for it; stays null if it can't be built
    private var tiltShiftPasses: TiltShiftPasses? = null
    private var isTiltShiftBuilt = false
    // The same for a script that only asks for the color boost right now, which needs none of the above
    private var tiltShiftColorPass: PrePass? = null
    private var isTiltShiftColorBuilt = false
    // The same for a script's glow, its 3D-only bright pass, and each variant of the combine pass that adds the glow, the haze and the
    // vignette (by COMBINE_GLOW and COMBINE_HAZE bits); a target that can't be made is only logged once
    private var bloomPasses: BloomPasses? = null
    private var isBloomBuilt = false
    private var onlyThreeDBrightPass: PrePass? = null
    private var isOnlyThreeDBrightBuilt = false
    private val combinePasses = arrayOfNulls<PrePass>(COMBINE_VARIANTS)
    private val isCombineBuilt = BooleanArray(COMBINE_VARIANTS)
    private var hasLoggedBloomTarget = false
    // The same for a script's depth of field
    private var depthOfFieldPasses: DepthOfFieldPasses? = null
    private var isDepthOfFieldBuilt = false
    // The same for a script's shading; a target that can't be made is only logged once
    private var shadePasses: ShadePasses? = null
    private var isShadeBuilt = false
    private var hasLoggedShadeTarget = false

    // Returned by renderPrePasses(), reused so that no garbage is created on every frame
    private val output = FilterOutput()

    // Full-target quad shared by all the pre-passes, created on first use
    private var quadVao = 0
    private var quadVbo = 0

    // Frame that the pre-pass textures currently hold, since frame pacing often presents the same frame more than once
    private var lastFrameTextureId = 0
    private var lastFrameSequence = 0L
    private var lastFrameWidth = 0
    private var lastFrameHeight = 0
    // Swapping the screens changes which half's sides the passes keep, so the same frame is filtered again
    private var lastFrameBottomScreenWide = false
    // A fade changes the blur without a new frame (and scripts can stop it while paused), so the same frame is filtered again
    private var lastFrameTiltShift: TiltShift? = null
    private var lastFrameBloom: Bloom? = null
    private var lastFrameDepthOfField: DepthOfField? = null
    private var lastFrameShade: Shade? = null
    private var lastFrameDepthTextureId = 0
    // Whether the deband pre-pass ran for that frame, since the screen shader can take it over (or hand it back) on the same frame
    private var lastFrameRanDeband = false
    // What renderPrePasses() returned for that frame
    private var lastOutputTextureId = 0
    private var lastOutputWidth = 0
    private var lastOutputHeight = 0

    // GL state saved around the pre-passes, allocated once to avoid garbage on every frame
    private val savedViewport = IntArray(4)
    private val savedInt = IntArray(1)
    private var savedDrawFramebuffer = 0
    private var savedReadFramebuffer = 0
    private var savedBlendEnabled = false

    // The next pass's input and whether the state above is saved yet, while renderPrePasses() runs
    private var inputTexture = 0
    private var inputWidth = 0
    private var inputHeight = 0
    // The frame's top screen half height in DS pixels (PresentFrameWrapper.topScreenHeight), for the passes' shaders
    private var inputTopScreenHeight = 0
    private var isStateSaved = false

    /**
     * Runs the pre-passes, then the [shade], the [depthOfField], the [bloom] and the [tiltShift] if there are any (in that order, so the depth
     * of field blurs the shading, the glow is of the focused picture and the tilt-shift's blur softens the glow too), and returns the texture
     * the screen shader should draw, whose size the caller sets as the shader's texSize (a scaled pass makes it bigger); call it before drawing
     * to the surface and for invalid frames too, and set the program, VAO, array buffer and texture unit 0 again afterwards. The shading, the
     * depth of field, the haze and a 3D-only glow need the frame's depth, so a frame without one is drawn unshaded, sharp, without haze, and
     * with a glow on every pixel. With [foldDeband] the deband pre-pass is left out, for a frame drawn with [getDebandScreenShader].
     */
    fun renderPrePasses(
        frame: PresentFrameWrapper,
        isBottomScreenWide: Boolean,
        tiltShift: TiltShift?,
        bloom: Bloom?,
        depthOfField: DepthOfField?,
        shade: Shade?,
        foldDeband: Boolean = false,
    ): FilterOutput {
        output.set(frame.textureId, frame.textureWidth, frame.textureHeight)

        if (!frame.isValidFrame) {
            // Cleared on the invalid frame between sessions, since a new session restarts the frame sequence and can reuse texture names. The
            // depth of field's focus too, so a new session doesn't start on a focus held from the last one
            lastFrameSequence = 0L
            expandedFrameSequence = 0L
            depthOfFieldPasses?.isFocusStale = true
            return output
        }

        // debug.melonade.comp1x: a frame with screens at 1x that comes here (any filter but the no-filter shader alone, a Lua effect, or the
        // 1x shader didn't build) becomes the full frame first, which then goes through everything below exactly as the full frame would
        val frameTextureId = if (frame.halvesAt1x != 0) expandHalvesAt1x(frame) else frame.textureId
        output.set(frameTextureId, frame.textureWidth, frame.textureHeight)

        // Without a blur, or if its passes can't be built, only the color boost is left to apply, in one pass that takes one sample per texel. So the
        // boost doesn't jump as the bands reach or leave 0 even then. Each is only tried once
        val tiltShiftPasses = if (tiltShift != null && tiltShift.hasBlur) getTiltShiftPasses() else null
        val tiltShiftColorPass = if (tiltShift != null && tiltShiftPasses == null && tiltShift.hasColorBoost) getTiltShiftColorPass() else null
        // The same for the bloom: without a glow, or if its passes can't be built, only the haze and the vignette are left, in a combine pass
        // that reads no glow. The haze needs the frame's depth, and so does a 3D-only glow, which takes every pixel without it
        val hasDepth = frame.depthTextureId != 0
        val bloomPasses = if (bloom != null && bloom.hasBloom) getBloomPasses() else null
        val combineVariant = if (bloom != null) pickCombineVariant(bloomPasses != null, bloom.hasHaze && hasDepth, bloom.hasVignette) else NO_COMBINE
        val glowPasses = if (combineVariant != NO_COMBINE && (combineVariant and COMBINE_GLOW) != 0) bloomPasses else null
        val onlyThreeDBrightPass = if (bloom != null && glowPasses != null && bloom.onlyThreeD && hasDepth) getOnlyThreeDBrightPass() else null
        val usesHaze = combineVariant != NO_COMBINE && (combineVariant and COMBINE_HAZE) != 0
        val depthOfFieldPasses = if (depthOfField != null && !depthOfField.isOff && hasDepth) getDepthOfFieldPasses() else null
        if (depthOfFieldPasses == null) {
            // The focus it held is from before the depth of field stopped, so turning it back on starts fresh
            this.depthOfFieldPasses?.isFocusStale = true
        }
        val shadePasses = if (shade != null && !shade.isOff && hasDepth) getShadePasses() else null
        val runsDeband = debandPass != null && !foldDeband
        val hasLuaPasses = tiltShiftPasses != null || tiltShiftColorPass != null || combineVariant != NO_COMBINE || depthOfFieldPasses != null ||
                shadePasses != null
        if (!runsDeband && prePasses.isEmpty() && !hasLuaPasses) {
            return output
        }

        val width = frame.textureWidth
        val height = frame.textureHeight
        if (width <= 0 || height <= 0) {
            return output
        }

        val appliedTiltShift = if (tiltShiftPasses != null || tiltShiftColorPass != null) tiltShift else null
        val appliedBloom = if (combineVariant != NO_COMBINE) bloom else null
        val appliedDepthOfField = if (depthOfFieldPasses != null) depthOfField else null
        val appliedShade = if (shadePasses != null) shade else null
        val depthTextureId = if (depthOfFieldPasses != null || onlyThreeDBrightPass != null || usesHaze || shadePasses != null) {
            frame.depthTextureId
        } else {
            0
        }
        val isSameFrame = frame.frameSequence != 0L && frame.frameSequence == lastFrameSequence && frame.textureId == lastFrameTextureId &&
                width == lastFrameWidth && height == lastFrameHeight && isBottomScreenWide == lastFrameBottomScreenWide &&
                appliedTiltShift == lastFrameTiltShift && appliedBloom == lastFrameBloom && appliedDepthOfField == lastFrameDepthOfField &&
                appliedShade == lastFrameShade && depthTextureId == lastFrameDepthTextureId && runsDeband == lastFrameRanDeband
        if (isSameFrame) {
            output.set(lastOutputTextureId, lastOutputWidth, lastOutputHeight)
            return output
        }

        // The width can't tell, since widescreen makes the frame wider without upscaling it
        val isUpscaled = height > frame.frameHeight
        inputTexture = frameTextureId
        inputWidth = width
        inputHeight = height
        inputTopScreenHeight = frame.topScreenHeight
        isStateSaved = false
        var isComplete = true
        if (runsDeband) {
            isComplete = renderPass(debandPass, frame.topScreenWidth, isBottomScreenWide, null)
        }
        // Indexed so that no iterator is created on every frame
        for (i in prePasses.indices) {
            if (!isComplete) {
                break
            }
            val pass = prePasses[i]
            if (pass.nativeResolutionOnly && isUpscaled) {
                continue
            }

            if (!renderPass(pass, frame.topScreenWidth, isBottomScreenWide, null)) {
                isComplete = false
                break
            }
        }

        if (isComplete && shadePasses != null && appliedShade != null) {
            renderShade(shadePasses, appliedShade, frame, isBottomScreenWide)
        }
        if (isComplete && depthOfFieldPasses != null && appliedDepthOfField != null) {
            isComplete = renderDepthOfField(depthOfFieldPasses, appliedDepthOfField, frame, isBottomScreenWide)
        }
        if (isComplete && appliedBloom != null) {
            renderBloom(glowPasses, onlyThreeDBrightPass, combineVariant, appliedBloom, frame, isBottomScreenWide)
        }
        if (isComplete && tiltShiftPasses != null) {
            isComplete = renderPass(tiltShiftPasses.horizontal, frame.topScreenWidth, isBottomScreenWide, appliedTiltShift) &&
                    renderPass(tiltShiftPasses.vertical, frame.topScreenWidth, isBottomScreenWide, appliedTiltShift)
        } else if (isComplete && tiltShiftColorPass != null) {
            isComplete = renderPass(tiltShiftColorPass, frame.topScreenWidth, isBottomScreenWide, appliedTiltShift)
        }

        if (isStateSaved) {
            restoreState()
        }

        if (!isComplete) {
            // Draw the frame without the pre-passes rather than nothing
            lastFrameSequence = 0L
            return output
        }

        lastFrameTextureId = frame.textureId
        lastFrameSequence = frame.frameSequence
        lastFrameWidth = width
        lastFrameHeight = height
        lastFrameBottomScreenWide = isBottomScreenWide
        lastFrameTiltShift = appliedTiltShift
        lastFrameBloom = appliedBloom
        lastFrameDepthOfField = appliedDepthOfField
        lastFrameShade = appliedShade
        lastFrameDepthTextureId = depthTextureId
        lastFrameRanDeband = runsDeband
        lastOutputTextureId = inputTexture
        lastOutputWidth = inputWidth
        lastOutputHeight = inputHeight
        output.set(inputTexture, inputWidth, inputHeight)
        return output
    }

    /**
     * Draws the current input through [pass], whose target becomes the next input, and returns false if its target can't be drawn to.
     */
    private fun renderPass(pass: PrePass, topScreenWidth: Int, isBottomScreenWide: Boolean, tiltShift: TiltShift?): Boolean {
        beginPasses()
        val outputWidth = inputWidth * pass.scale
        val outputHeight = inputHeight * pass.scale
        if (!pass.prepareTarget(outputWidth, outputHeight)) {
            return false
        }
        GLES30.glViewport(0, 0, outputWidth, outputHeight)
        pass.render(inputTexture, inputWidth, inputHeight, topScreenWidth, inputTopScreenHeight, isBottomScreenWide, tiltShift)
        inputTexture = pass.texture
        inputWidth = outputWidth
        inputHeight = outputHeight
        return true
    }

    /**
     * Saves the GL state the passes change and binds their quad, before a frame's first pass.
     */
    private fun beginPasses() {
        if (isStateSaved) {
            return
        }
        saveState()
        isStateSaved = true
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        if (quadVao == 0) {
            createQuad()
        }
        GLES30.glBindVertexArray(quadVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, quadVbo)
    }

    /**
     * Draws [bloom] over the current input, whose size it keeps, through the combine pass [variant] (from [pickCombineVariant]): with
     * COMBINE_GLOW the glow through [passes] first, its bright pass [onlyThreeDBright] when that isn't null, and with COMBINE_HAZE the haze
     * from the frame's depth on unit 2. A glow target that can't be made leaves the glow out (the haze and vignette stay), and a combine target
     * that can't be made leaves the bloom out, logged once, so the frame is still drawn.
     */
    private fun renderBloom(passes: BloomPasses?, onlyThreeDBright: PrePass?, variant: Int, bloom: Bloom, frame: PresentFrameWrapper, isBottomScreenWide: Boolean) {
        beginPasses()
        val glowTexture = if (passes != null && (variant and COMBINE_GLOW) != 0) {
            renderGlow(passes, onlyThreeDBright, bloom, frame, isBottomScreenWide)
        } else {
            0
        }
        val combineVariant = if (glowTexture != 0) variant else variant and COMBINE_GLOW.inv()
        // Without the glow, it's only worth a pass with the haze or the vignette
        val combine = if (combineVariant != 0 || bloom.hasVignette) getCombinePass(combineVariant) else null
        if (combine == null) {
            return
        }
        if (!combine.prepareTarget(inputWidth, inputHeight)) {
            logBloomTarget()
            return
        }

        // The glow's target is linear from when it was made, and no pass reads it with anything else
        if (glowTexture != 0) {
            GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glowTexture)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        }
        val hasHaze = (combineVariant and COMBINE_HAZE) != 0
        if (hasHaze) {
            bindDepthTexture(GLES30.GL_TEXTURE2, frame.depthTextureId)
        }
        GLES30.glViewport(0, 0, inputWidth, inputHeight)
        combine.render(inputTexture, inputWidth, inputHeight, frame.topScreenWidth, inputTopScreenHeight, isBottomScreenWide, null, bloom = bloom)
        if (glowTexture != 0) {
            unbindTexture(GLES30.GL_TEXTURE1)
        }
        if (hasHaze) {
            unbindTexture(GLES30.GL_TEXTURE2)
        }
        inputTexture = combine.texture
    }

    /**
     * The current input's bright parts, blurred, at DS-native resolution (the frame's size in DS pixels): the texture of the glow's last pass,
     * or 0 if one of its targets can't be made. With [onlyThreeDBright] that's the bright pass, reading the frame's depth on unit 1.
     */
    private fun renderGlow(passes: BloomPasses, onlyThreeDBright: PrePass?, bloom: Bloom, frame: PresentFrameWrapper, isBottomScreenWide: Boolean): Int {
        val width = frame.topScreenWidth
        val height = frame.frameHeight
        val bright = onlyThreeDBright ?: passes.bright
        val isReady = width > 0 && bright.prepareTarget(width, height) && passes.horizontal.prepareTarget(width, height) &&
                passes.vertical.prepareTarget(width, height)
        if (!isReady) {
            logBloomTarget()
            return 0
        }

        GLES30.glViewport(0, 0, width, height)
        if (onlyThreeDBright != null) {
            bindDepthTexture(GLES30.GL_TEXTURE1, frame.depthTextureId)
        }
        bright.render(inputTexture, inputWidth, inputHeight, width, inputTopScreenHeight, isBottomScreenWide, null, bloom = bloom)
        if (onlyThreeDBright != null) {
            unbindTexture(GLES30.GL_TEXTURE1)
        }
        passes.horizontal.render(bright.texture, width, height, width, inputTopScreenHeight, isBottomScreenWide, null, bloom = bloom)
        passes.vertical.render(passes.horizontal.texture, width, height, width, inputTopScreenHeight, isBottomScreenWide, null, bloom = bloom)
        return passes.vertical.texture
    }

    /**
     * Binds the frame's depth texture [textureId] on [unit], nearest since its values can't be mixed, and leaves unit 0 active.
     */
    private fun bindDepthTexture(unit: Int, textureId: Int) {
        GLES30.glActiveTexture(unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textureId)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    private fun unbindTexture(unit: Int) {
        GLES30.glActiveTexture(unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    /**
     * The combine pass variant for a frame with a glow whose passes built ([hasGlow]), the haze with the frame's depth ([hasHaze]) and the
     * vignette, building it on first use, or NO_COMBINE for nothing to draw. A variant that doesn't build drops the glow first, then the haze, so
     * what's left is still drawn.
     */
    private fun pickCombineVariant(hasGlow: Boolean, hasHaze: Boolean, hasVignette: Boolean): Int {
        var variant = (if (hasGlow) COMBINE_GLOW else 0) or (if (hasHaze) COMBINE_HAZE else 0)
        if ((variant and COMBINE_GLOW) != 0 && getCombinePass(variant) == null) {
            variant = variant and COMBINE_GLOW.inv()
        }
        if ((variant and COMBINE_HAZE) != 0 && getCombinePass(variant) == null) {
            variant = variant and COMBINE_HAZE.inv()
        }
        if (variant == 0 && (!hasVignette || getCombinePass(0) == null)) {
            return NO_COMBINE
        }
        return variant
    }

    /**
     * Blurs the current input by [depthOfField] through [passes], with the frame's depth texture on unit 1 (nearest, as its values can't be
     * mixed): first the focus pass picks this frame's focus into a 1 x 1 target from the depth and the last frame's focus (so it can hold
     * while something covers the player), then the two blur passes read it on unit 2. Returns false if a target can't be drawn to. Only called
     * for a new frame, so a frame presented again doesn't move the focus on.
     */
    private fun renderDepthOfField(passes: DepthOfFieldPasses, depthOfField: DepthOfField, frame: PresentFrameWrapper, isBottomScreenWide: Boolean): Boolean {
        beginPasses()
        val outputWidth = inputWidth
        val outputHeight = inputHeight
        val lastFocus = passes.lastFocus
        val focus = passes.nextFocus
        val isReady = passes.horizontal.prepareTarget(outputWidth, outputHeight) && passes.vertical.prepareTarget(outputWidth, outputHeight) &&
                lastFocus.prepareTarget(1, 1) && focus.prepareTarget(1, 1)
        if (!isReady) {
            return false
        }
        if (passes.isFocusStale) {
            // No focus, and the hold expired (A = 1 is age 255), so the first frame picks its focus from the depth alone
            lastFocus.clear()
            focus.clear()
            passes.isFocusStale = false
        }

        // The focus point in the depth texture, which is the top screen's half of the frame: the wings come before the 256 view's x 0, and the
        // margin before its y 0
        val topScreenWidth = frame.topScreenWidth.coerceAtLeast(1)
        val focusU = (depthOfField.focusX + (topScreenWidth - SCREEN_WIDTH) / 2f) / topScreenWidth
        val focusV = (depthOfField.focusY + frame.topScreenMargin) / frame.topScreenHeight
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, frame.depthTextureId)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        renderFocus(passes, depthOfField, frame.topScreenWidth, isBottomScreenWide, focusU, focusV)

        GLES30.glViewport(0, 0, outputWidth, outputHeight)
        passes.horizontal.render(
            inputTexture, inputWidth, inputHeight, frame.topScreenWidth, inputTopScreenHeight, isBottomScreenWide, null,
            depthOfField = depthOfField, focusU = focusU, focusV = focusV,
        )
        passes.vertical.render(
            passes.horizontal.texture, inputWidth, inputHeight, frame.topScreenWidth, inputTopScreenHeight, isBottomScreenWide, null,
            depthOfField = depthOfField, focusU = focusU, focusV = focusV,
        )
        inputTexture = passes.vertical.texture
        passes.swapFocus()
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE2)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        return true
    }

    /**
     * Draws this frame's focus into [passes]'s next focus target from the last one, with the depth texture already on unit 1, and binds it on
     * unit 2 for the blur passes; both targets are already prepared at 1 x 1.
     */
    private fun renderFocus(passes: DepthOfFieldPasses, depthOfField: DepthOfField, topScreenWidth: Int, isBottomScreenWide: Boolean, focusU: Float, focusV: Float) {
        val focus = passes.nextFocus
        // A hold ages every other frame, so its 255 steps last about 8.5 s at 60 fps
        val focusAgeStep = if ((passes.focusFrames and 1) == 0) 1f else 0f
        passes.focusFrames++
        GLES30.glViewport(0, 0, 1, 1)
        focus.render(
            passes.lastFocus.texture, 1, 1, topScreenWidth, inputTopScreenHeight, isBottomScreenWide, null,
            depthOfField = depthOfField, focusU = focusU, focusV = focusV, focusAgeStep = focusAgeStep,
        )
        // Nearest, as the focus's bytes can't be mixed either
        GLES30.glActiveTexture(GLES30.GL_TEXTURE2)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, focus.texture)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    private fun getDepthOfFieldPasses(): DepthOfFieldPasses? {
        if (!isDepthOfFieldBuilt) {
            isDepthOfFieldBuilt = true
            val horizontal = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.DepthOfFieldHorizontalPrePassShader)
            val vertical = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.DepthOfFieldVerticalPrePassShader)
            // Two of the focus pass, since each frame's reads the last one's
            val focusA = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.DepthOfFieldFocusShader)
            val focusB = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.DepthOfFieldFocusShader)
            if (horizontal != null && vertical != null && focusA != null && focusB != null) {
                depthOfFieldPasses = DepthOfFieldPasses(
                    PrePass(horizontal, 1, false), PrePass(vertical, 1, false), PrePass(focusA, 1, false), PrePass(focusB, 1, false),
                )
            } else {
                // The frame is drawn sharp rather than not at all
                Log.e(TAG, "Depth of field shaders failed to build, so the blur is left out")
                horizontal?.delete()
                vertical?.delete()
                focusA?.delete()
                focusB?.delete()
            }
        }
        return depthOfFieldPasses
    }

    /**
     * Shades the current input, whose size it keeps, by [shade] through [passes]: the frame's depth downsampled to DS resolution and blurred
     * both ways there, then the apply pass compares each pixel's own depth with it, with the depth texture on unit 1 and the blurred depth on
     * unit 2 (both nearest). A target that can't be made leaves the shading out, logged once, so the frame is still drawn.
     */
    private fun renderShade(passes: ShadePasses, shade: Shade, frame: PresentFrameWrapper, isBottomScreenWide: Boolean) {
        beginPasses()
        // The depth texture is the frame's width by the top screen's half's rows, at the internal scale (FrameQueue::validateDepthTexture), so
        // over that scale it's the top screen's half in DS pixels, wings and margin included, whatever the filter scaled the input to
        val scale = if (frame.frameHeight > 0) frame.textureHeight / frame.frameHeight else 0
        val dsWidth = frame.topScreenWidth
        val dsHeight = frame.topScreenHeight
        val depthWidth = frame.textureWidth
        val depthHeight = dsHeight * scale
        val isReady = scale > 0 && dsWidth > 0 && passes.downsample.prepareTarget(dsWidth, dsHeight) &&
                passes.horizontal.prepareTarget(dsWidth, dsHeight) && passes.vertical.prepareTarget(dsWidth, dsHeight) &&
                passes.apply.prepareTarget(inputWidth, inputHeight)
        if (!isReady) {
            if (!hasLoggedShadeTarget) {
                hasLoggedShadeTarget = true
                Log.e(TAG, "A shading target can't be made, so the shading is left out")
            }
            return
        }

        GLES30.glViewport(0, 0, dsWidth, dsHeight)
        passes.downsample.render(frame.depthTextureId, depthWidth, depthHeight, dsWidth, inputTopScreenHeight, isBottomScreenWide, null, shade = shade)
        passes.horizontal.render(passes.downsample.texture, dsWidth, dsHeight, dsWidth, inputTopScreenHeight, isBottomScreenWide, null, shade = shade)
        passes.vertical.render(passes.horizontal.texture, dsWidth, dsHeight, dsWidth, inputTopScreenHeight, isBottomScreenWide, null, shade = shade)

        bindDepthTexture(GLES30.GL_TEXTURE1, frame.depthTextureId)
        // Nearest too, as its bytes are a depth that can't be mixed
        bindDepthTexture(GLES30.GL_TEXTURE2, passes.vertical.texture)
        GLES30.glViewport(0, 0, inputWidth, inputHeight)
        passes.apply.render(inputTexture, inputWidth, inputHeight, frame.topScreenWidth, inputTopScreenHeight, isBottomScreenWide, null, shade = shade)
        unbindTexture(GLES30.GL_TEXTURE1)
        unbindTexture(GLES30.GL_TEXTURE2)
        inputTexture = passes.apply.texture
    }

    private fun getShadePasses(): ShadePasses? {
        if (!isShadeBuilt) {
            isShadeBuilt = true
            val downsample = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.ShadeDownsamplePrePassShader)
            val horizontal = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.ShadeBlurHorizontalPrePassShader)
            val vertical = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.ShadeBlurVerticalPrePassShader)
            val apply = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.ShadeApplyPrePassShader)
            if (downsample != null && horizontal != null && vertical != null && apply != null) {
                shadePasses = ShadePasses(
                    PrePass(downsample, 1, false), PrePass(horizontal, 1, false), PrePass(vertical, 1, false), PrePass(apply, 1, false),
                )
            } else {
                // The frame is drawn unshaded rather than not at all
                Log.e(TAG, "Shading shaders failed to build, so the shading is left out")
                downsample?.delete()
                horizontal?.delete()
                vertical?.delete()
                apply?.delete()
            }
        }
        return shadePasses
    }

    private fun logBloomTarget() {
        if (!hasLoggedBloomTarget) {
            hasLoggedBloomTarget = true
            Log.e(TAG, "A bloom target can't be made, so the bloom is left out")
        }
    }

    private fun getBloomPasses(): BloomPasses? {
        if (!isBloomBuilt) {
            isBloomBuilt = true
            val bright = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.BloomBrightPrePassShader)
            val horizontal = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.BloomBlurHorizontalPrePassShader)
            val vertical = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.BloomBlurVerticalPrePassShader)
            if (bright != null && horizontal != null && vertical != null) {
                bloomPasses = BloomPasses(PrePass(bright, 1, false), PrePass(horizontal, 1, false), PrePass(vertical, 1, false))
            } else {
                // The frame is drawn without the glow rather than not at all
                Log.e(TAG, "Bloom shaders failed to build, so the glow is left out")
                bright?.delete()
                horizontal?.delete()
                vertical?.delete()
            }
        }
        return bloomPasses
    }

    private fun getOnlyThreeDBrightPass(): PrePass? {
        if (!isOnlyThreeDBrightBuilt) {
            isOnlyThreeDBrightBuilt = true
            val shader = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.BloomBrightPrePassShader, extraDefines = listOf(ONLY_3D_DEFINE))
            if (shader != null) {
                onlyThreeDBrightPass = PrePass(shader, 1, false)
            } else {
                // The glow takes every pixel, as it does on a frame without depth, rather than being left out
                Log.e(TAG, "3D-only bloom shader failed to build, so the glow takes 2D too")
            }
        }
        return onlyThreeDBrightPass
    }

    /**
     * The combine pass [variant] (COMBINE_GLOW and COMBINE_HAZE bits), built the first time it's asked for; null if it couldn't be built.
     */
    private fun getCombinePass(variant: Int): PrePass? {
        if (!isCombineBuilt[variant]) {
            isCombineBuilt[variant] = true
            val defines = buildList {
                if ((variant and COMBINE_GLOW) == 0) {
                    add(NO_GLOW_DEFINE)
                }
                if ((variant and COMBINE_HAZE) != 0) {
                    add(HAZE_DEFINE)
                }
            }
            val shader = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.BloomCombinePrePassShader, extraDefines = defines)
            if (shader != null) {
                combinePasses[variant] = PrePass(shader, 1, false)
            } else {
                // The frame is drawn without what this variant adds rather than not at all
                Log.e(TAG, "Bloom combine shader with defines $defines failed to build, so what it adds is left out")
            }
        }
        return combinePasses[variant]
    }

    private fun getTiltShiftPasses(): TiltShiftPasses? {
        if (!isTiltShiftBuilt) {
            isTiltShiftBuilt = true
            val horizontal = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.TiltShiftHorizontalPrePassShader)
            val vertical = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.TiltShiftVerticalPrePassShader)
            if (horizontal != null && vertical != null) {
                tiltShiftPasses = TiltShiftPasses(PrePass(horizontal, 1, false), PrePass(vertical, 1, false))
            } else {
                // The frame is drawn without the blur rather than not at all
                Log.e(TAG, "Tilt-shift shaders failed to build, so the blur is left out")
                horizontal?.delete()
                vertical?.delete()
            }
        }
        return tiltShiftPasses
    }

    private fun getTiltShiftColorPass(): PrePass? {
        if (!isTiltShiftColorBuilt) {
            isTiltShiftColorBuilt = true
            val shader = ShaderFactory.createShaderProgramOrNull(ShaderProgramSource.TiltShiftColorPrePassShader)
            if (shader != null) {
                tiltShiftColorPass = PrePass(shader, 1, false)
            } else {
                // The frame is drawn without the color boost rather than not at all
                Log.e(TAG, "Tilt-shift color shader failed to build, so the color boost is left out")
            }
        }
        return tiltShiftColorPass
    }

    fun delete() {
        screenShader.delete()
        halvesAt1xShader?.delete()
        halvesAt1xShader = null
        if (coordinateTextures[0] != 0) {
            GLES30.glDeleteTextures(2, coordinateTextures, 0)
            coordinateTextures.fill(0)
            coordinatesWidth = 0
            coordinatesHeight = 0
        }
        expandPass?.delete()
        expandPass = null
        debandScreenShader?.delete()
        debandScreenShader = null
        debandPass?.delete()
        prePasses.forEach { it.delete() }
        tiltShiftPasses?.horizontal?.delete()
        tiltShiftPasses?.vertical?.delete()
        tiltShiftPasses = null
        tiltShiftColorPass?.delete()
        tiltShiftColorPass = null
        bloomPasses?.let {
            it.bright.delete()
            it.horizontal.delete()
            it.vertical.delete()
        }
        bloomPasses = null
        onlyThreeDBrightPass?.delete()
        onlyThreeDBrightPass = null
        for (i in combinePasses.indices) {
            combinePasses[i]?.delete()
            combinePasses[i] = null
        }
        depthOfFieldPasses?.let {
            it.horizontal.delete()
            it.vertical.delete()
            it.focusA.delete()
            it.focusB.delete()
        }
        depthOfFieldPasses = null
        shadePasses?.let {
            it.downsample.delete()
            it.horizontal.delete()
            it.vertical.delete()
            it.apply.delete()
        }
        shadePasses = null
        if (quadVao != 0) {
            GLES30.glDeleteVertexArrays(1, intArrayOf(quadVao), 0)
            GLES30.glDeleteBuffers(1, intArrayOf(quadVbo), 0)
            quadVao = 0
            quadVbo = 0
        }
    }

    private fun createQuad() {
        // Full-target quad whose UVs map target texel centers onto source texel (quadrant) centers
        val vertexData = floatArrayOf(
            // Position  UVs
            -1f, -1f,    0f, 0f,
            1f, -1f,     1f, 0f,
            -1f, 1f,     0f, 1f,
            1f, 1f,      1f, 1f,
        )

        val vertexBufferSize = vertexData.size * Float.SIZE_BYTES
        val vertexBuffer = ByteBuffer.allocateDirect(vertexBufferSize)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(vertexData)
            .position(0)

        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        quadVao = ids[0]
        GLES30.glGenBuffers(1, ids, 0)
        quadVbo = ids[0]

        GLES30.glBindVertexArray(quadVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, quadVbo)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, vertexBufferSize, vertexBuffer, GLES30.GL_STATIC_DRAW)
    }

    // Only the state that the renderer doesn't set again before drawing: the target (the surface), its viewport and blending
    private fun saveState() {
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, savedInt, 0)
        savedDrawFramebuffer = savedInt[0]
        GLES30.glGetIntegerv(GLES30.GL_READ_FRAMEBUFFER_BINDING, savedInt, 0)
        savedReadFramebuffer = savedInt[0]
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, savedViewport, 0)
        savedBlendEnabled = GLES30.glIsEnabled(GLES30.GL_BLEND)
    }

    private fun restoreState() {
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, savedDrawFramebuffer)
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, savedReadFramebuffer)
        GLES30.glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3])
        if (savedBlendEnabled) {
            GLES30.glEnable(GLES30.GL_BLEND)
        }
    }

    private class TiltShiftPasses(val horizontal: PrePass, val vertical: PrePass)

    // The blur's two passes, and the focus pass twice: each frame's focus is drawn into one from the last frame's in the other, then they swap
    private class DepthOfFieldPasses(val horizontal: PrePass, val vertical: PrePass, val focusA: PrePass, val focusB: PrePass) {
        private var isNextFocusA = true
        // Focus passes drawn, whose even ones age a hold
        var focusFrames = 0
        // Whether both focus targets have to be cleared before the next one is drawn: they hold nothing yet when built, and what they hold
        // is from before the depth of field stopped (or a new session started)
        var isFocusStale = true

        val nextFocus get() = if (isNextFocusA) focusA else focusB
        val lastFocus get() = if (isNextFocusA) focusB else focusA

        fun swapFocus() {
            isNextFocusA = !isNextFocusA
        }
    }

    // The glow's passes at DS-native resolution; a combine pass (combinePasses) adds it at the input's
    private class BloomPasses(val bright: PrePass, val horizontal: PrePass, val vertical: PrePass)

    // The shading's downsample and two blur passes at DS resolution, then the apply pass at the input's
    private class ShadePasses(val downsample: PrePass, val horizontal: PrePass, val vertical: PrePass, val apply: PrePass)

    private class PrePass(private val shader: Shader, val scale: Int, val nativeResolutionOnly: Boolean) {
        private var framebuffer = 0
        var texture = 0
            private set
        private var width = 0
        private var height = 0
        private var isFramebufferComplete = false

        /**
         * Makes sure that the target texture has the given size, (re)creating it if needed, and returns whether the framebuffer can be drawn to.
         */
        fun prepareTarget(width: Int, height: Int): Boolean {
            if (texture != 0 && width == this.width && height == this.height) {
                return isFramebufferComplete
            }

            deleteTarget()
            this.width = width
            this.height = height

            val ids = IntArray(1)
            GLES30.glGenTextures(1, ids, 0)
            texture = ids[0]
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture)
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, width, height, 0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)

            GLES30.glGenFramebuffers(1, ids, 0)
            framebuffer = ids[0]
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, framebuffer)
            GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0, GLES30.GL_TEXTURE_2D, texture, 0)

            val status = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER)
            isFramebufferComplete = status == GLES30.GL_FRAMEBUFFER_COMPLETE
            if (!isFramebufferComplete) {
                Log.e(TAG, "Pre-pass framebuffer incomplete (${width}x$height): status=0x${status.toString(16)}")
            }

            return isFramebufferComplete
        }

        /**
         * Draws the input texture through this pass's shader into the target; the caller binds the quad and sets the viewport and blending.
         */
        fun render(
            inputTexture: Int,
            inputWidth: Int,
            inputHeight: Int,
            topScreenWidth: Int,
            topScreenHeight: Int,
            isBottomScreenWide: Boolean,
            tiltShift: TiltShift?,
            halvesAt1x: Int = 0,
            scaleAt1x: Int = 1,
            halfScaleAt1x: Int = 1,
            bloom: Bloom? = null,
            depthOfField: DepthOfField? = null,
            focusU: Float = 0f,
            focusV: Float = 0f,
            focusAgeStep: Float = 0f,
            shade: Shade? = null,
        ) {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, framebuffer)
            // The quad covers every texel, so the old contents never have to be loaded
            GLES30.glInvalidateFramebuffer(GLES30.GL_FRAMEBUFFER, 1, COLOR_ATTACHMENTS, 0)

            shader.use()
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, inputTexture)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, shader.textureFiltering)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, shader.textureFiltering)
            GLES30.glVertexAttribPointer(shader.attribPos, 2, GLES30.GL_FLOAT, false, 4 * Float.SIZE_BYTES, 0)
            GLES30.glVertexAttribPointer(shader.attribUv, 2, GLES30.GL_FLOAT, false, 4 * Float.SIZE_BYTES, 2 * Float.SIZE_BYTES)
            GLES30.glUniform1i(shader.uniformTex, 0)
            shader.setTextureSize(inputWidth, inputHeight)
            shader.setTopScreenWidth(topScreenWidth)
            shader.setTopScreenHeight(topScreenHeight)
            shader.setBottomScreenWide(isBottomScreenWide)
            if (tiltShift != null) {
                shader.setTiltShift(tiltShift)
            }
            if (bloom != null) {
                shader.setBloom(bloom)
            }
            if (depthOfField != null) {
                shader.setDepthOfField(depthOfField, focusU, focusV, focusAgeStep)
            }
            if (shade != null) {
                shader.setShade(shade)
            }
            shader.setHalvesAt1x(halvesAt1x, scaleAt1x, halfScaleAt1x)
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
        }

        /**
         * Fills the target with opaque black (0, 0, 0, 1), for a pass that reads its own last output; call after [prepareTarget]. That's the
         * clear color DSRenderer sets once for the surface, so it's left set rather than saved and restored.
         */
        fun clear() {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, framebuffer)
            GLES30.glClearColor(0f, 0f, 0f, 1f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        }

        fun delete() {
            shader.delete()
            deleteTarget()
        }

        private fun deleteTarget() {
            if (framebuffer != 0) {
                GLES30.glDeleteFramebuffers(1, intArrayOf(framebuffer), 0)
                framebuffer = 0
            }
            if (texture != 0) {
                GLES30.glDeleteTextures(1, intArrayOf(texture), 0)
                texture = 0
            }
            isFramebufferComplete = false
        }
    }
}
