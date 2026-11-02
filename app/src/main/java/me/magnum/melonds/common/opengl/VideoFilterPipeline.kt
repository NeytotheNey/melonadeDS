package me.magnum.melonds.common.opengl

import android.opengl.GLES30
import android.util.Log
import me.magnum.melonds.domain.model.FRAME_TEXTURE_HEIGHT
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The texture that a [VideoFilterPipeline]'s screen shader should draw, and its size in texels. The pipeline owns one instance and
 * updates it on every [VideoFilterPipeline.renderPrePasses] call, so read it before the next one.
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
 * The GL objects of a [VideoFilter]: the compiled screen shader and, if the filter has pre-passes, their shaders and offscreen textures. Must be
 * created, used and deleted on the thread that owns the GL context. Filters without pre-passes create nothing else and cost nothing extra.
 * [colorCorrection] turns on the DS screen colours in the screen shader. The pre-passes always work on the raw colours.
 */
class VideoFilterPipeline(filter: VideoFilter, colorCorrection: Boolean = false) {

    private companion object {
        const val TAG = "VideoFilterPipeline"
        val COLOR_ATTACHMENTS = intArrayOf(GLES30.GL_COLOR_ATTACHMENT0)
    }

    // A screen shader that fails to build falls back to the no-filter shader. A pre-pass that fails is left out, so the screen shader gets its
    // input instead, like when a pre-pass's framebuffer can't be used
    val screenShader: Shader = ShaderFactory.createScreenShaderProgram(filter.screenShader, colorCorrection)
    private val prePasses = filter.prePasses.mapNotNull { prePass ->
        ShaderFactory.createShaderProgramOrNull(prePass.source)?.let { PrePass(it, prePass.scale, prePass.nativeResolutionOnly) }
    }

    // Returned by renderPrePasses(). Reused so that no garbage is created on every frame
    private val output = FilterOutput()

    // Full-target quad shared by all the pre-passes. Created on first use
    private var quadVao = 0
    private var quadVbo = 0

    // Frame that the pre-pass textures currently hold. With frame pacing the same frame is often presented more than once
    private var lastFrameTextureId = 0
    private var lastFrameSequence = 0L
    private var lastFrameWidth = 0
    private var lastFrameHeight = 0
    // Swapping the screens changes which half's sides the passes keep, so the same frame is filtered again
    private var lastFrameBottomScreenWide = false
    // What renderPrePasses() returned for that frame
    private var lastOutputTextureId = 0
    private var lastOutputWidth = 0
    private var lastOutputHeight = 0

    // GL state saved around the pre-passes. Allocated once to avoid garbage on every frame
    private val savedViewport = IntArray(4)
    private val savedInt = IntArray(1)
    private var savedDrawFramebuffer = 0
    private var savedReadFramebuffer = 0
    private var savedBlendEnabled = false

    /**
     * Runs the pre-passes over the frame and returns the texture that [screenShader] should draw, with its size. That is the frame texture
     * and its size when the filter has no pre-passes, or none of them run for this frame. The texture always has the frame texture's layout,
     * so the screen UVs don't change, but a pass with a scale makes it bigger: set the screen shader's texSize from the returned size. The
     * returned object is reused, so read it before the next call. Call it before drawing anything to the surface: on tiled GPUs, switching
     * framebuffers in the middle of a frame makes the driver store the surface and load it back. It restores the framebuffers, the viewport
     * and blending (the renderer draws its background with whatever blending is enabled). The program, the VAO and array buffer, and the
     * texture bound to unit 0 (which it leaves active) are left changed, so set them before drawing. Call it for invalid frames too, so the
     * cache is cleared.
     */
    fun renderPrePasses(frame: PresentFrameWrapper, isBottomScreenWide: Boolean): FilterOutput {
        output.set(frame.textureId, frame.textureWidth, frame.textureHeight)

        if (!frame.isValidFrame) {
            // There's always an invalid frame between emulator sessions. The new session restarts the frame sequence and can reuse the old
            // texture names, so a cached result could otherwise match the first frames of the new game
            lastFrameSequence = 0L
            return output
        }

        if (prePasses.isEmpty()) {
            return output
        }

        val width = frame.textureWidth
        val height = frame.textureHeight
        if (width <= 0 || height <= 0) {
            return output
        }

        val isSameFrame = frame.frameSequence != 0L && frame.frameSequence == lastFrameSequence && frame.textureId == lastFrameTextureId &&
                width == lastFrameWidth && height == lastFrameHeight && isBottomScreenWide == lastFrameBottomScreenWide
        if (isSameFrame) {
            output.set(lastOutputTextureId, lastOutputWidth, lastOutputHeight)
            return output
        }

        // The width can't tell, since widescreen makes the frame wider without upscaling it
        val isUpscaled = height > FRAME_TEXTURE_HEIGHT
        var inputTexture = frame.textureId
        var inputWidth = width
        var inputHeight = height
        var isStateSaved = false
        var isComplete = true
        // Indexed so that no iterator is created on every frame
        for (i in prePasses.indices) {
            val pass = prePasses[i]
            if (pass.nativeResolutionOnly && isUpscaled) {
                continue
            }

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
                isComplete = false
                break
            }
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            pass.render(inputTexture, inputWidth, inputHeight, frame.topScreenWidth, isBottomScreenWide)
            inputTexture = pass.texture
            inputWidth = outputWidth
            inputHeight = outputHeight
        }

        if (isStateSaved) {
            restoreState()
        }

        if (!isComplete) {
            // Draw the frame without the pre-passes rather than nothing. The output still holds the frame texture and its size
            lastFrameSequence = 0L
            return output
        }

        lastFrameTextureId = frame.textureId
        lastFrameSequence = frame.frameSequence
        lastFrameWidth = width
        lastFrameHeight = height
        lastFrameBottomScreenWide = isBottomScreenWide
        lastOutputTextureId = inputTexture
        lastOutputWidth = inputWidth
        lastOutputHeight = inputHeight
        output.set(inputTexture, inputWidth, inputHeight)
        return output
    }

    fun delete() {
        screenShader.delete()
        prePasses.forEach { it.delete() }
        if (quadVao != 0) {
            GLES30.glDeleteVertexArrays(1, intArrayOf(quadVao), 0)
            GLES30.glDeleteBuffers(1, intArrayOf(quadVbo), 0)
            quadVao = 0
            quadVbo = 0
        }
    }

    private fun createQuad() {
        // Covers the whole target. UV (0, 0) is at NDC (-1, -1), which maps to texel (0, 0) of the target texture, so the fragment that writes
        // target texel (x, y) gets the UV of the centre of that texel. With a scale of 1 that's also the centre of source texel (x, y), and with
        // a scale of 2 it's the centre of one quadrant of source texel (x / 2, y / 2). The output keeps the source's orientation and layout
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

    // Only the state that the renderer doesn't set again before drawing: the target (the surface),
    // its viewport and blending. The vertex attribute setup is stored in the VAOs, so the pre-passes never touch the renderer's own
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

    private class PrePass(private val shader: Shader, val scale: Int, val nativeResolutionOnly: Boolean) {
        private var framebuffer = 0
        var texture = 0
            private set
        private var width = 0
        private var height = 0
        private var isFramebufferComplete = false

        /**
         * Makes sure that the target texture has the given size, (re)creating it if needed. Returns whether the framebuffer can be drawn to.
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
         * Draws the input texture through this pass's shader into the target, with the input's size as the shader's texSize and the frame's
         * [topScreenWidth], and whether the bottom screen is the one drawn wide. The caller binds the quad and sets the viewport (to the target's
         * size) and blending.
         */
        fun render(inputTexture: Int, inputWidth: Int, inputHeight: Int, topScreenWidth: Int, isBottomScreenWide: Boolean) {
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
