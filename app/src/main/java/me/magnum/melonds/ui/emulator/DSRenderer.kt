package me.magnum.melonds.ui.emulator

import android.content.Context
import android.graphics.BitmapFactory
import android.opengl.GLES30
import android.opengl.GLUtils
import me.magnum.melonds.common.opengl.Shader
import me.magnum.melonds.common.opengl.ShaderFactory
import me.magnum.melonds.common.opengl.ShaderProgramSource
import me.magnum.melonds.common.opengl.VideoFilterPipeline
import me.magnum.melonds.common.opengl.VideoFilterShaderProvider
import me.magnum.melonds.domain.model.FRAME_TEXTURE_HEIGHT
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.VideoFiltering
import me.magnum.melonds.domain.model.layout.BackgroundMode
import me.magnum.melonds.domain.model.layout.buildCentredRect
import me.magnum.melonds.domain.model.layout.getLetterboxedRect
import me.magnum.melonds.domain.model.layout.getWideTopScreenRect
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_BOTTOM
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_TOP
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration
import me.magnum.melonds.ui.emulator.render.EmulatorRenderer
import me.magnum.melonds.utils.BitmapUtils
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

// Screen shadow: its radius is this share of the screen's shorter side, but never less than the minimum (in surface pixels). The strength is
// the shadow's alpha at the screen's edge
private const val SHADOW_RADIUS_FACTOR = 0.06f
private const val SHADOW_MIN_RADIUS = 8
private const val SHADOW_STRENGTH = 0.6f

// Screen and shadow vertices: position (2), UVs or distance outside the screen (2) and alpha (1). Background vertices have no alpha
private const val FLOATS_PER_VERTEX = 5
private const val BACKGROUND_FLOATS_PER_VERTEX = 4
private const val VERTICES_PER_SCREEN = 6

class DSRenderer(private val context: Context) : EmulatorRenderer {

    private val configurationLock = Any()
    private var rendererConfiguration: RuntimeRendererConfiguration? = null
    private var mustUpdateConfiguration = false
    private var isBackgroundPositionDirty = false
    private var isBackgroundLoaded = false

    private var backgroundTexture = 0

    private var screenFilter: VideoFilterPipeline? = null
    // Settings the screen filter was built with. It's only rebuilt when they change, or after the GL objects are recreated. Only accessed on the
    // GL thread
    private var screenFilterFiltering: VideoFiltering? = null
    private var screenFilterDsScreenColors = false
    private var mustRebuildScreenFilter = true
    private lateinit var backgroundShader: Shader
    private lateinit var shadowShader: Shader
    private lateinit var overlayShader: Shader
    // Whether the shaders above exist. Program names are shared with the emulator's context, so deleting them twice could free a program that
    // reused the name
    private var areShadersCreated = false

    private var screensVbo = 0
    private var screensVao = 0
    private var screenIndices = 0
    // The DS screen of each quad in screensVbo, in draw order (OVERLAY_SCREEN_TOP or OVERLAY_SCREEN_BOTTOM), so that each one's Lua drawings
    // can go right after it. The Lua drawings' own quads follow the screens' ones in the buffer, in the same order
    private var screenQuadScreens = IntArray(0)
    // PresentFrameWrapper.topScreenWidth of the frame that the quads were built for. They're built again when a frame of another width comes,
    // so a SCREEN_WIDTH frame (widescreen off, another renderer, or a frame from before the width changed) always draws as without widescreen.
    // Only accessed on the GL thread
    private var frameTopScreenWidth = SCREEN_WIDTH

    private var shadowsVbo = 0
    private var shadowsVao = 0
    private var shadowIndices = 0

    private var backgroundVbo = 0
    private var backgroundVao = 0

    // Lock used when manipulating background properties
    private val backgroundLock = Any()
    private var background: RuntimeBackground? = null
    private var mustLoadBackground = false
    private var topScreenRect: Rect? = null
    private var bottomScreenRect: Rect? = null
    private var topAlpha: Float = 1f
    private var bottomAlpha: Float = 1f
    private var topOnTop: Boolean = false
    private var bottomOnTop: Boolean = false
    // Whether the layout is a wide one (LayoutConfiguration.isWide), whose top rects are placed by the player for each variant
    private var isWideLayout: Boolean = false

    private var width = 0f
    private var height = 0f

    private var backgroundWidth = 0
    private var backgroundHeight = 0

    override fun updateRendererConfiguration(newRendererConfiguration: RuntimeRendererConfiguration?) {
        synchronized(configurationLock) {
            rendererConfiguration = newRendererConfiguration
            mustUpdateConfiguration = true
        }
    }

    override fun setLeftRotationEnabled(enabled: Boolean) {
    }

    fun updateScreenAreas(
        topScreenRect: Rect?,
        bottomScreenRect: Rect?,
        topAlpha: Float,
        bottomAlpha: Float,
        topOnTop: Boolean,
        bottomOnTop: Boolean,
        isWideLayout: Boolean,
    ) {
        synchronized(configurationLock) {
            this.topScreenRect = topScreenRect
            this.bottomScreenRect = bottomScreenRect
            this.topAlpha = topAlpha
            this.bottomAlpha = bottomAlpha
            this.topOnTop = topOnTop
            this.bottomOnTop = bottomOnTop
            this.isWideLayout = isWideLayout
            mustUpdateConfiguration = true
        }
    }

    fun setBackground(background: RuntimeBackground) {
        synchronized(backgroundLock) {
            this.background = background
            mustLoadBackground = true
            isBackgroundPositionDirty = true
            isBackgroundLoaded = false
        }
    }

    private fun screenXToViewportX(x: Int): Float {
        return (x / width) * 2f - 1f
    }

    private fun screenYToViewportY(y: Int): Float {
        return 1f - y / height * 2f
    }

    override fun onSurfaceCreated() {
        GLES30.glClearColor(0f, 0f, 0f, 1f)
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
        GLES30.glDisable(GLES30.GL_CULL_FACE)

        // Called again when the render mode or the surface size changes, in the same GL context. Free what the previous call created
        deleteGlObjects()

        // Setup textures
        val textures = IntArray(1)
        GLES30.glGenTextures(1, textures, 0)
        backgroundTexture = textures[0]

        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, backgroundTexture)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)

        // Setup vertex buffers
        val vbos = IntArray(3)
        val vaos = IntArray(3)
        GLES30.glGenBuffers(3, vbos, 0)
        GLES30.glGenVertexArrays(3, vaos, 0)
        screensVbo = vbos[0]
        screensVao = vaos[0]

        backgroundVbo = vbos[1]
        backgroundVao = vaos[1]

        shadowsVbo = vbos[2]
        shadowsVao = vaos[2]

        // Create background, shadow and Lua overlay shaders
        backgroundShader = ShaderFactory.createShaderProgram(ShaderProgramSource.BackgroundShader)
        shadowShader = ShaderFactory.createShaderProgram(ShaderProgramSource.ScreenShadowShader)
        overlayShader = ShaderFactory.createShaderProgram(ShaderProgramSource.OverlayShader)
        areShadersCreated = true

        mustRebuildScreenFilter = true
        synchronized(configurationLock) {
            mustUpdateConfiguration = true
        }

        // The new texture and buffer are empty, so the background has to be loaded into them again
        synchronized(backgroundLock) {
            isBackgroundLoaded = false
            isBackgroundPositionDirty = true
            mustLoadBackground = background != null
        }
    }

    private fun deleteGlObjects() {
        if (backgroundTexture != 0) {
            GLES30.glDeleteTextures(1, intArrayOf(backgroundTexture), 0)
            backgroundTexture = 0
        }
        if (screensVbo != 0) {
            GLES30.glDeleteBuffers(3, intArrayOf(screensVbo, backgroundVbo, shadowsVbo), 0)
            GLES30.glDeleteVertexArrays(3, intArrayOf(screensVao, backgroundVao, shadowsVao), 0)
            screensVbo = 0
            screensVao = 0
            backgroundVbo = 0
            backgroundVao = 0
            shadowsVbo = 0
            shadowsVao = 0
        }
        if (areShadersCreated) {
            backgroundShader.delete()
            shadowShader.delete()
            overlayShader.delete()
            areShadersCreated = false
        }
        // Rebuilt on the next frame, since mustRebuildScreenFilter is set whenever the GL objects are created again
        screenFilter?.delete()
        screenFilter = null
    }

    override fun release() {
        deleteGlObjects()
    }

    private fun applyRendererConfiguration() {
        updateScreenCoordinates()
        updateShader()
    }

    private fun updateScreenCoordinates() {
        // Indices:
        // 1                         2
        //   +-----------------------+ 4
        //   |                       |
        //   |                       |
        //   |                       |
        //   |                       |
        // 0 +-----------------------+
        //   3                         5
        // Texture is vertically flipped

        // The texture has 2 empty lines between the screens (SCREEN_GAP), one on each side of the middle. Take that into account when computing
        // UVs
        val lineRelativeSize = 1f / FRAME_TEXTURE_HEIGHT.toFloat()
        val topScreenBottomV = 0.5f - lineRelativeSize
        val bottomScreenTopV = 0.5f + lineRelativeSize

        // The top screen takes the frame's whole width. In a frame wider than SCREEN_WIDTH (widescreen), the bottom screen is the middle
        // SCREEN_WIDTH of it, with black bars beside it. The Lua drawings are always SCREEN_WIDTH wide
        val bottomScreenLeftU = (frameTopScreenWidth - SCREEN_WIDTH) / (2f * frameTopScreenWidth)
        val topUvs = buildScreenUvs(0f, 1f, 0f, topScreenBottomV)
        val bottomUvs = buildScreenUvs(bottomScreenLeftU, 1f - bottomScreenLeftU, bottomScreenTopV, 1f)
        val overlayBottomUvs = buildScreenUvs(0f, 1f, bottomScreenTopV, 1f)

        // A wide top screen is drawn wider than its rect in the layout. Its Lua drawings stay on its middle SCREEN_WIDTH part, where the 2D is
        val drawnTopScreenRect = topScreenRect?.let { getTopScreenDrawRect(it, bottomScreenRect, frameTopScreenWidth) }
        val topScreenVertexData = drawnTopScreenRect?.let { buildScreenVertexData(it, topUvs, topAlpha) }
        val bottomScreenVertexData = bottomScreenRect?.let { buildScreenVertexData(it, bottomUvs, bottomAlpha) }
        val topOverlayVertexData = drawnTopScreenRect?.let { buildScreenVertexData(getNativeTopScreenRect(it, frameTopScreenWidth), topUvs, topAlpha) }
        val bottomOverlayVertexData = bottomScreenRect?.let { buildScreenVertexData(it, overlayBottomUvs, bottomAlpha) }

        val (overScreen, underScreen) = if (bottomOnTop) OVERLAY_SCREEN_BOTTOM to OVERLAY_SCREEN_TOP else OVERLAY_SCREEN_TOP to OVERLAY_SCREEN_BOTTOM
        screenQuadScreens = intArrayOf(underScreen, overScreen).filter {
            (if (it == OVERLAY_SCREEN_TOP) topScreenVertexData else bottomScreenVertexData) != null
        }.toIntArray()
        val screenVertexData = screenQuadScreens.map { if (it == OVERLAY_SCREEN_TOP) topScreenVertexData!! else bottomScreenVertexData!! }
        val overlayVertexData = screenQuadScreens.map { if (it == OVERLAY_SCREEN_TOP) topOverlayVertexData!! else bottomOverlayVertexData!! }

        val vertexBufferSize = (screenVertexData.sumOf { it.size } + overlayVertexData.sumOf { it.size }) * Float.SIZE_BYTES
        val vertexBuffer = ByteBuffer.allocateDirect(vertexBufferSize)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .apply {
                screenVertexData.forEach { put(it) }
                overlayVertexData.forEach { put(it) }
                position(0)
            }

        GLES30.glBindVertexArray(screensVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, screensVbo)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, vertexBufferSize, vertexBuffer, GLES30.GL_STATIC_DRAW)
        screenIndices = screenQuadScreens.size * VERTICES_PER_SCREEN

        updateShadowCoordinates(drawnTopScreenRect)
    }

    /**
     * Where the top screen is drawn when the frame's top screen is [topScreenWidth] DS pixels wide. In a wide layout the frame is fitted in
     * [rect] at its own aspect ratio, whatever its width, so a frame whose width doesn't match the variant yet (just after a fold, or while
     * paused) never spreads beyond the rect. Otherwise a SCREEN_WIDTH frame takes [rect] itself, and a wider one is drawn at its own aspect
     * ratio, placed by getWideTopScreenRect. Stored layouts and touch never change
     */
    private fun getTopScreenDrawRect(rect: Rect, bottomRect: Rect?, topScreenWidth: Int): Rect {
        if (isWideLayout) {
            return getLetterboxedRect(rect, topScreenWidth.coerceAtLeast(SCREEN_WIDTH) / SCREEN_HEIGHT.toFloat())
        }
        if (topScreenWidth <= SCREEN_WIDTH) {
            return rect
        }

        return getWideTopScreenRect(rect, bottomRect, topScreenWidth / SCREEN_HEIGHT.toFloat(), width)
    }

    /**
     * The middle SCREEN_WIDTH part of a top screen that's [topScreenWidth] DS pixels wide and drawn at [drawRect]
     */
    private fun getNativeTopScreenRect(drawRect: Rect, topScreenWidth: Int): Rect {
        if (topScreenWidth <= SCREEN_WIDTH) {
            return drawRect
        }

        val nativeWidth = drawRect.width * SCREEN_WIDTH / topScreenWidth.toFloat()
        return buildCentredRect(drawRect.x + drawRect.width / 2f, drawRect.y.toFloat(), nativeWidth, drawRect.height.toFloat())
    }

    private fun buildScreenUvs(left: Float, right: Float, top: Float, bottom: Float): FloatArray {
        // Same vertex order as the screens
        return floatArrayOf(
            left, bottom,
            left, top,
            right, top,
            left, bottom,
            right, top,
            right, bottom,
        )
    }

    /**
     * @param drawnTopScreenRect Where the top screen is drawn, which is wider than its rect while widescreen is on
     */
    private fun updateShadowCoordinates(drawnTopScreenRect: Rect?) {
        // Empty when the setting is off, so nothing is drawn
        val shadowVertexData = if (rendererConfiguration?.screenShadow == true) {
            listOfNotNull(
                drawnTopScreenRect?.let { buildShadowVertexData(it, topAlpha) },
                bottomScreenRect?.let { buildShadowVertexData(it, bottomAlpha) },
            )
        } else {
            emptyList()
        }

        val vertexBufferSize = shadowVertexData.sumOf { it.size } * Float.SIZE_BYTES
        val vertexBuffer = ByteBuffer.allocateDirect(vertexBufferSize)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .apply {
                shadowVertexData.forEach { put(it) }
                position(0)
            }

        GLES30.glBindVertexArray(shadowsVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, shadowsVbo)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, vertexBufferSize, vertexBuffer, GLES30.GL_STATIC_DRAW)
        shadowIndices = vertexBuffer.capacity() / FLOATS_PER_VERTEX
    }

    private fun buildShadowVertexData(rect: Rect, alpha: Float): FloatArray {
        val radius = (minOf(rect.width, rect.height) * SHADOW_RADIUS_FACTOR).roundToInt().coerceAtLeast(SHADOW_MIN_RADIUS)
        val shadowAlpha = alpha * SHADOW_STRENGTH

        // The shadow is a 3x3 grid of cells around the screen, without the middle one: the screen covers it, and leaving it out keeps a
        // semi-transparent screen from being darkened by its own shadow. Each vertex gets how far it is outside the screen on each axis, in
        // radii: 0 on the screen's edges and 1 on the shadow's. That distance is linear within every cell, so the interpolated value is exact
        // and the shader only needs its length to get the distance to the screen's rectangle
        val xs = intArrayOf(rect.x - radius, rect.x, rect.x + rect.width, rect.x + rect.width + radius)
        val ys = intArrayOf(rect.y - radius, rect.y, rect.y + rect.height, rect.y + rect.height + radius)
        val distances = floatArrayOf(1f, 0f, 0f, 1f)

        val vertexData = FloatArray(8 * 6 * FLOATS_PER_VERTEX) // 8 cells, 6 vertices each
        var offset = 0
        for (row in 0 until 3) {
            for (column in 0 until 3) {
                if (row == 1 && column == 1) {
                    continue
                }

                val left = screenXToViewportX(xs[column])
                val right = screenXToViewportX(xs[column + 1])
                val top = screenYToViewportY(ys[row])
                val bottom = screenYToViewportY(ys[row + 1])
                val leftDistance = distances[column]
                val rightDistance = distances[column + 1]
                val topDistance = distances[row]
                val bottomDistance = distances[row + 1]

                // Same vertex order as the screens
                floatArrayOf(
                    // Position    Distance outside                Alpha
                    left, bottom,  leftDistance, bottomDistance,   shadowAlpha,
                    left, top,     leftDistance, topDistance,      shadowAlpha,
                    right, top,    rightDistance, topDistance,     shadowAlpha,
                    left, bottom,  leftDistance, bottomDistance,   shadowAlpha,
                    right, top,    rightDistance, topDistance,     shadowAlpha,
                    right, bottom, rightDistance, bottomDistance,  shadowAlpha,
                ).copyInto(vertexData, offset)
                offset += 6 * FLOATS_PER_VERTEX
            }
        }
        return vertexData
    }

    private fun buildScreenVertexData(rect: Rect, uvs: FloatArray, alpha: Float): FloatArray {
        val left = screenXToViewportX(rect.x)
        val right = screenXToViewportX(rect.x + rect.width)
        val top = screenYToViewportY(rect.y)
        val bottom = screenYToViewportY(rect.y + rect.height)

        // TODO: Apply rotation. Apply to background as well. Rename this class to something more generic
        return floatArrayOf(
            // Position    UVs               Alpha
            left, bottom,  uvs[0], uvs[1],   alpha,
            left, top,     uvs[2], uvs[3],   alpha,
            right, top,    uvs[4], uvs[5],   alpha,
            left, bottom,  uvs[6], uvs[7],   alpha,
            right, top,    uvs[8], uvs[9],   alpha,
            right, bottom, uvs[10], uvs[11], alpha,
        )
    }

    private fun updateShader() {
        val filtering = rendererConfiguration?.videoFiltering ?: VideoFiltering.NONE
        val dsScreenColors = rendererConfiguration?.dsScreenColors ?: false
        // Fold and layout changes also end up here. Building the filter compiles its shaders, so it's only done when it changes
        if (!mustRebuildScreenFilter && filtering == screenFilterFiltering && dsScreenColors == screenFilterDsScreenColors) {
            return
        }

        // Delete previous shader
        screenFilter?.delete()

        screenFilter = VideoFilterPipeline(VideoFilterShaderProvider.getFilter(filtering), dsScreenColors)
        screenFilterFiltering = filtering
        screenFilterDsScreenColors = dsScreenColors
        mustRebuildScreenFilter = false
    }

    override fun onSurfaceChanged(width: Int, height: Int) {
        synchronized(configurationLock) {
            this.width = width.toFloat()
            this.height = height.toFloat()
            mustUpdateConfiguration = true
        }

        synchronized(backgroundLock) {
            isBackgroundPositionDirty = true
        }
    }

    override fun drawFrame(presentFrameWrapper: PresentFrameWrapper) {
        synchronized(configurationLock) {
            // The screens' UVs and the top screen's rect follow the width of the frame being presented
            if (presentFrameWrapper.isValidFrame && presentFrameWrapper.topScreenWidth != frameTopScreenWidth) {
                frameTopScreenWidth = presentFrameWrapper.topScreenWidth
                mustUpdateConfiguration = true
            }
            if (mustUpdateConfiguration) {
                applyRendererConfiguration()
                mustUpdateConfiguration = false
            }
        }

        // Runs before anything is drawn to the surface. Returns the frame texture itself for filters without pre-passes. Also called for
        // invalid frames, which clear its cache
        val screenTexture = screenFilter?.renderPrePasses(presentFrameWrapper)

        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        if (!presentFrameWrapper.isValidFrame) {
            return
        }

        synchronized(backgroundLock) {
            renderBackground()
        }

        if (shadowIndices > 0) {
            renderScreenShadows()
        }

        screenFilter?.screenShader?.let { shader ->
            GLES30.glDisable(GLES30.GL_DEPTH_TEST)
            GLES30.glEnable(GLES30.GL_BLEND)
            GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA)

            GLES30.glBindVertexArray(screensVao)
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, screensVbo)

            val screenTextureId = screenTexture?.textureId ?: presentFrameWrapper.textureId
            // A pre-pass with a scale makes the texture bigger than the frame
            val screenTextureWidth = screenTexture?.width ?: presentFrameWrapper.textureWidth
            val screenTextureHeight = screenTexture?.height ?: presentFrameWrapper.textureHeight
            val overlayScreens = if (presentFrameWrapper.overlayTextureId != 0) presentFrameWrapper.overlayScreens else 0

            if (overlayScreens == 0) {
                useScreenShader(shader, screenTextureId, screenTextureWidth, screenTextureHeight, presentFrameWrapper.topScreenWidth)
                GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, screenIndices)
            } else {
                // Each screen, then its Lua drawings, so that a screen drawn over the other one also covers the other one's drawings
                screenQuadScreens.forEachIndexed { index, screen ->
                    val firstVertex = index * VERTICES_PER_SCREEN
                    useScreenShader(shader, screenTextureId, screenTextureWidth, screenTextureHeight, presentFrameWrapper.topScreenWidth)
                    GLES30.glDrawArrays(GLES30.GL_TRIANGLES, firstVertex, VERTICES_PER_SCREEN)

                    if (overlayScreens and screen != 0) {
                        useOverlayShader(presentFrameWrapper.overlayTextureId)
                        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE_MINUS_SRC_ALPHA)
                        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, screenIndices + firstVertex, VERTICES_PER_SCREEN)
                        // Everything else (the other screen, and the next frame's background and shadows) is blended with straight alpha
                        GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA)
                    }
                }
            }

            GLES30.glBindVertexArray(0)
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, 0)
        }
    }

    private fun useScreenShader(shader: Shader, textureId: Int, textureWidth: Int, textureHeight: Int, topScreenWidth: Int) {
        shader.use()

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textureId)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, shader.textureFiltering)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, shader.textureFiltering)

        setScreenVertexAttributes(shader)
        GLES30.glUniform1i(shader.uniformTex, 0)
        shader.setTextureSize(textureWidth, textureHeight)
        shader.setTopScreenWidth(topScreenWidth)
    }

    private fun useOverlayShader(overlayTextureId: Int) {
        overlayShader.use()

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, overlayTextureId)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, overlayShader.textureFiltering)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, overlayShader.textureFiltering)

        setScreenVertexAttributes(overlayShader)
        GLES30.glUniform1i(overlayShader.uniformTex, 0)
    }

    private fun setScreenVertexAttributes(shader: Shader) {
        GLES30.glVertexAttribPointer(shader.attribPos, 2, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 0)
        GLES30.glVertexAttribPointer(shader.attribUv, 2, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 2 * Float.SIZE_BYTES)
        GLES30.glVertexAttribPointer(shader.attribAlpha, 1, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 4 * Float.SIZE_BYTES)
    }

    private fun renderBackground() {
        if (mustLoadBackground) {
            loadBackground()
            mustLoadBackground = false
        }

        if (!isBackgroundLoaded) {
            return
        }

        if (isBackgroundPositionDirty) {
            updateBackgroundPosition()
        }

        GLES30.glBindVertexArray(backgroundVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, backgroundVbo)

        backgroundShader.use()

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, backgroundTexture)

        GLES30.glVertexAttribPointer(backgroundShader.attribPos, 2, GLES30.GL_FLOAT, false, BACKGROUND_FLOATS_PER_VERTEX * Float.SIZE_BYTES, 0)
        GLES30.glVertexAttribPointer(backgroundShader.attribUv, 2, GLES30.GL_FLOAT, false, BACKGROUND_FLOATS_PER_VERTEX * Float.SIZE_BYTES, 2 * Float.SIZE_BYTES)
        GLES30.glUniform1i(backgroundShader.uniformTex, 0)

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 6)
    }

    private fun renderScreenShadows() {
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA)

        GLES30.glBindVertexArray(shadowsVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, shadowsVbo)

        shadowShader.use()

        GLES30.glVertexAttribPointer(shadowShader.attribPos, 2, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 0)
        GLES30.glVertexAttribPointer(shadowShader.attribUv, 2, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 2 * Float.SIZE_BYTES)
        GLES30.glVertexAttribPointer(shadowShader.attribAlpha, 1, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 4 * Float.SIZE_BYTES)

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, shadowIndices)
    }

    private fun loadBackground() {
        val background = background ?: return

        background.background?.uri?.let {
            val backgroundSampleSize = BitmapUtils.calculateMinimumSampleSize(context, it, width.roundToInt(), height.roundToInt())

            val bitmapResult = runCatching {
                context.contentResolver.openInputStream(it)?.let { stream ->
                    val options = BitmapFactory.Options().apply {
                        inSampleSize = backgroundSampleSize
                    }

                    BitmapFactory.decodeStream(stream, null, options)
                }
            }

            val bitmap = bitmapResult.getOrNull() ?: return

            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, backgroundTexture)
            GLUtils.texImage2D(GLES30.GL_TEXTURE_2D, 0, bitmap, 0)
            bitmap.recycle()

            backgroundWidth = bitmap.width
            backgroundHeight = bitmap.height

            isBackgroundLoaded = true
            isBackgroundPositionDirty = true
        }
    }

    private fun updateBackgroundPosition() {
        val background = background ?: return
        val coords = getBackgroundCoords(background.mode, backgroundWidth, backgroundHeight)

        val vertexData = floatArrayOf(
            // Position             UVs
            coords[0], coords[1],   0f, 1f,
            coords[2], coords[3],   0f, 0f,
            coords[4], coords[5],   1f, 0f,
            coords[6], coords[7],   0f, 1f,
            coords[8], coords[9],   1f, 0f,
            coords[10], coords[11], 1f, 1f,
        )

        val vertexBufferSize = vertexData.size * Float.SIZE_BYTES
        val vertexBuffer = ByteBuffer.allocateDirect(vertexBufferSize)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(vertexData)
            .position(0)

        GLES30.glBindVertexArray(backgroundVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, backgroundVbo)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, vertexBufferSize, vertexBuffer, GLES30.GL_STATIC_DRAW)

        isBackgroundPositionDirty = false
    }

    private fun getBackgroundCoords(backgroundMode: BackgroundMode, backgroundWidth: Int, backgroundHeight: Int): Array<Float> {
        val backgroundAspectRatio = backgroundWidth / backgroundHeight.toFloat()
        val screenAspectRatio = width / height

        return when (backgroundMode) {
            BackgroundMode.STRETCH -> {
                arrayOf(
                    -1f, -1f,
                    -1f, 1f,
                    1f, 1f,
                    -1f, -1f,
                    1f, 1f,
                    1f, -1f
                )
            }
            BackgroundMode.FIT_CENTER -> {
                if (screenAspectRatio > backgroundAspectRatio) {
                    val scaleFactor = width / backgroundWidth
                    val relativeBackgroundWidth = height / (backgroundHeight * scaleFactor) * 2f
                    arrayOf(
                        -(relativeBackgroundWidth / 2f), -1f,
                        -(relativeBackgroundWidth / 2f), 1f,
                        relativeBackgroundWidth / 2f, 1f,
                        -(relativeBackgroundWidth / 2f), -1f,
                        relativeBackgroundWidth / 2f, 1f,
                        relativeBackgroundWidth / 2f, -1f
                    )
                } else {
                    val scaleFactor = height / backgroundHeight
                    val relativeBackgroundHeight = width / (backgroundWidth * scaleFactor) * 2f
                    arrayOf(
                        -1f, -(relativeBackgroundHeight / 2f),
                        -1f, relativeBackgroundHeight / 2f,
                        1f, relativeBackgroundHeight / 2f,
                        -1f, -(relativeBackgroundHeight / 2f),
                        1f, relativeBackgroundHeight / 2f,
                        1f, -(relativeBackgroundHeight / 2f)
                    )
                }
            }
            BackgroundMode.FIT_LEFT -> {
                if (screenAspectRatio > backgroundAspectRatio) {
                    val scaleFactor = width / backgroundWidth
                    val relativeBackgroundWidth = height / (backgroundHeight * scaleFactor) * 2f
                    arrayOf(
                        -1f, -1f,
                        -1f, 1f,
                        -1 + relativeBackgroundWidth, 1f,
                        -1f, -1f,
                        -1 + relativeBackgroundWidth, 1f,
                        -1 + relativeBackgroundWidth, -1f
                    )
                } else {
                    val scaleFactor = height / backgroundHeight
                    val relativeBackgroundHeight = width / (backgroundWidth * scaleFactor) * 2f
                    arrayOf(
                        -1f, -(relativeBackgroundHeight / 2f),
                        -1f, relativeBackgroundHeight / 2f,
                        1f, relativeBackgroundHeight / 2f,
                        -1f, -(relativeBackgroundHeight / 2f),
                        1f, relativeBackgroundHeight / 2f,
                        1f, -(relativeBackgroundHeight / 2f)
                    )
                }
            }
            BackgroundMode.FIT_RIGHT -> {
                if (screenAspectRatio > backgroundAspectRatio) {
                    val scaleFactor = width / backgroundWidth
                    val relativeBackgroundWidth = height / (backgroundHeight * scaleFactor) * 2f
                    arrayOf(
                        1f - relativeBackgroundWidth, -1f,
                        1f - relativeBackgroundWidth, 1f,
                        1f, 1f,
                        1f - relativeBackgroundWidth, -1f,
                        1f, 1f,
                        1f, -1f
                    )
                } else {
                    val scaleFactor = height / backgroundHeight
                    val relativeBackgroundHeight = width / (backgroundWidth * scaleFactor) * 2f
                    arrayOf(
                        -1f, -(relativeBackgroundHeight / 2f),
                        -1f, relativeBackgroundHeight / 2f,
                        1f, relativeBackgroundHeight / 2f,
                        -1f, -(relativeBackgroundHeight / 2f),
                        1f, relativeBackgroundHeight / 2f,
                        1f, -(relativeBackgroundHeight / 2f)
                    )
                }
            }
            BackgroundMode.FIT_TOP -> {
                if (screenAspectRatio > backgroundAspectRatio) {
                    val scaleFactor = width / backgroundWidth
                    val relativeBackgroundWidth = height / (backgroundHeight * scaleFactor) * 2f
                    arrayOf(
                        -(relativeBackgroundWidth / 2f), -1f,
                        -(relativeBackgroundWidth / 2f), 1f,
                        relativeBackgroundWidth / 2f, 1f,
                        -(relativeBackgroundWidth / 2f), -1f,
                        relativeBackgroundWidth / 2f, 1f,
                        relativeBackgroundWidth / 2f, -1f
                    )
                } else {
                    val scaleFactor = height / backgroundHeight
                    val relativeBackgroundHeight = width / (backgroundWidth * scaleFactor) * 2f
                    arrayOf(
                        -1f, 1f - relativeBackgroundHeight,
                        -1f, 1f,
                        1f, 1f,
                        -1f, 1f - relativeBackgroundHeight,
                        1f, 1f,
                        1f, 1f - relativeBackgroundHeight
                    )
                }
            }
            BackgroundMode.FIT_BOTTOM -> {
                if (screenAspectRatio > backgroundAspectRatio) {
                    val scaleFactor = width / backgroundWidth
                    val relativeBackgroundWidth = height / (backgroundHeight * scaleFactor) * 2f
                    arrayOf(
                        -(relativeBackgroundWidth / 2f), -1f,
                        -(relativeBackgroundWidth / 2f), 1f,
                        relativeBackgroundWidth / 2f, 1f,
                        -(relativeBackgroundWidth / 2f), -1f,
                        relativeBackgroundWidth / 2f, 1f,
                        relativeBackgroundWidth / 2f, -1f
                    )
                } else {
                    val scaleFactor = height / backgroundHeight
                    val relativeBackgroundHeight = width / (backgroundWidth * scaleFactor) * 2f
                    arrayOf(
                        -1f, -1f,
                        -1f, -1f + relativeBackgroundHeight,
                        1f, -1f + relativeBackgroundHeight,
                        -1f, -1f,
                        1f, -1f + relativeBackgroundHeight,
                        1f, -1f
                    )
                }
            }
        }
    }
}