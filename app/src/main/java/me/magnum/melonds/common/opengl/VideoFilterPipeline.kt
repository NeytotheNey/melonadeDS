package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import android.util.Log
import me.magnum.melonds.domain.model.FRAME_TEXTURE_HEIGHT
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
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
class VideoFilterPipeline(filter: VideoFilter, colorCorrection: Boolean = false) {

    private companion object {
        const val TAG = "VideoFilterPipeline"
        val COLOR_ATTACHMENTS = intArrayOf(GLES30.GL_COLOR_ATTACHMENT0)
    }

    // A pre-pass that fails to build is left out, so the screen shader gets its input instead
    val screenShader: Shader = ShaderFactory.createScreenShaderProgram(filter.screenShader, colorCorrection)
    private val prePasses = filter.prePasses.mapNotNull { prePass ->
        ShaderFactory.createShaderProgramOrNull(prePass.source)?.let { PrePass(it, prePass.scale, prePass.nativeResolutionOnly) }
    }

    // Built the first time a script asks for the blur, so that everyone else pays nothing for it; stays null if it can't be built
    private var tiltShiftPasses: TiltShiftPasses? = null
    private var isTiltShiftBuilt = false

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
    private var isStateSaved = false

    /**
     * Runs the pre-passes, then the [tiltShift] blur if there's one, and returns the texture the screen shader should draw, whose size the caller
     * sets as the shader's texSize (a scaled pass makes it bigger); call it before drawing to the surface and for invalid frames too, and set the
     * program, VAO, array buffer and texture unit 0 again afterwards.
     */
    fun renderPrePasses(frame: PresentFrameWrapper, isBottomScreenWide: Boolean, tiltShift: TiltShift?): FilterOutput {
        output.set(frame.textureId, frame.textureWidth, frame.textureHeight)

        if (!frame.isValidFrame) {
            // Cleared on the invalid frame between sessions, since a new session restarts the frame sequence and can reuse texture names
            lastFrameSequence = 0L
            return output
        }

        val tiltShiftPasses = if (tiltShift != null) getTiltShiftPasses() else null
        if (prePasses.isEmpty() && tiltShiftPasses == null) {
            return output
        }

        val width = frame.textureWidth
        val height = frame.textureHeight
        if (width <= 0 || height <= 0) {
            return output
        }

        val appliedTiltShift = if (tiltShiftPasses != null) tiltShift else null
        val isSameFrame = frame.frameSequence != 0L && frame.frameSequence == lastFrameSequence && frame.textureId == lastFrameTextureId &&
                width == lastFrameWidth && height == lastFrameHeight && isBottomScreenWide == lastFrameBottomScreenWide &&
                appliedTiltShift == lastFrameTiltShift
        if (isSameFrame) {
            output.set(lastOutputTextureId, lastOutputWidth, lastOutputHeight)
            return output
        }

        // The width can't tell, since widescreen makes the frame wider without upscaling it
        val isUpscaled = height > FRAME_TEXTURE_HEIGHT
        inputTexture = frame.textureId
        inputWidth = width
        inputHeight = height
        isStateSaved = false
        var isComplete = true
        // Indexed so that no iterator is created on every frame
        for (i in prePasses.indices) {
            val pass = prePasses[i]
            if (pass.nativeResolutionOnly && isUpscaled) {
                continue
            }

            if (!renderPass(pass, frame.topScreenWidth, isBottomScreenWide, null)) {
                isComplete = false
                break
            }
        }

        if (isComplete && tiltShiftPasses != null) {
            isComplete = renderPass(tiltShiftPasses.horizontal, frame.topScreenWidth, isBottomScreenWide, appliedTiltShift) &&
                    renderPass(tiltShiftPasses.vertical, frame.topScreenWidth, isBottomScreenWide, appliedTiltShift)
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
        if (!isStateSaved) {
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

        val outputWidth = inputWidth * pass.scale
        val outputHeight = inputHeight * pass.scale
        if (!pass.prepareTarget(outputWidth, outputHeight)) {
            return false
        }
        GLES30.glViewport(0, 0, outputWidth, outputHeight)
        pass.render(inputTexture, inputWidth, inputHeight, topScreenWidth, isBottomScreenWide, tiltShift)
        inputTexture = pass.texture
        inputWidth = outputWidth
        inputHeight = outputHeight
        return true
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

    fun delete() {
        screenShader.delete()
        prePasses.forEach { it.delete() }
        tiltShiftPasses?.horizontal?.delete()
        tiltShiftPasses?.vertical?.delete()
        tiltShiftPasses = null
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
        fun render(inputTexture: Int, inputWidth: Int, inputHeight: Int, topScreenWidth: Int, isBottomScreenWide: Boolean, tiltShift: TiltShift?) {
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
            shader.setBottomScreenWide(isBottomScreenWide)
            if (tiltShift != null) {
                shader.setTiltShift(tiltShift)
            }
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
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
