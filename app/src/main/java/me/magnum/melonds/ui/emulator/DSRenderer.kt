package me.magnum.melonds.ui.emulator

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
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
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.VideoFiltering
import me.magnum.melonds.domain.model.layout.BackgroundMode
import me.magnum.melonds.domain.model.layout.getScreensDrawing
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_BOTTOM
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_TOP
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.domain.model.render.TiltShift
import me.magnum.melonds.impl.diagnostics.SvgTimingLog
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration
import me.magnum.melonds.ui.emulator.render.EmulatorRenderer
import me.magnum.melonds.utils.BackgroundDecoder
import me.magnum.melonds.utils.BitmapUtils
import me.magnum.melonds.utils.SvgBackground
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

// Screen shadow: radius as a share of the screen's shorter side with a minimum in surface pixels, and alpha at the screen's edge
private const val SHADOW_RADIUS_FACTOR = 0.06f
private const val SHADOW_MIN_RADIUS = 8
private const val SHADOW_STRENGTH = 0.6f

// Screen and shadow vertices: position (2), UVs or distance outside the screen (2) and alpha (1); background vertices have no alpha
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
    // Settings the screen filter was built with, so it's only rebuilt when they change; GL thread only
    private var screenFilterFiltering: VideoFiltering? = null
    private var screenFilterDsScreenColors = false
    private var screenFilterDeband = false
    private var mustRebuildScreenFilter = true
    // The blur scripts want, or null for none; here rather than in the screen filter, which is rebuilt when the player's filter changes
    @Volatile
    var tiltShift: TiltShift? = null
    private lateinit var backgroundShader: Shader
    private lateinit var shadowShader: Shader
    private lateinit var overlayShader: Shader
    // Whether the shaders above exist; program names are shared with the emulator's context, so deleting one twice could free a reused name
    private var areShadersCreated = false

    private var screensVbo = 0
    private var screensVao = 0
    private var screenIndices = 0
    // The DS screen (OVERLAY_SCREEN_TOP or OVERLAY_SCREEN_BOTTOM) of each quad in screensVbo in draw order, so its Lua drawings can go right after it
    private var screenQuadScreens = IntArray(0)
    // Top-screen width of the frame the quads were built for; GL thread writes, touch input reads
    @Volatile
    var frameTopScreenWidth = SCREEN_WIDTH
        private set

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
    // Whether the screens are swapped, so bottomScreenRect is the layout's top rect and the bottom screen is the one drawn wide
    private var areScreensSwapped: Boolean = false
    // areScreensSwapped as it was when the quads were built, given to the filters so they keep the same screen's sides; GL thread only
    private var isBottomScreenWideInQuads = false

    private var width = 0f
    private var height = 0f

    private var backgroundWidth = 0
    private var backgroundHeight = 0

    // The parsed SVG of the current background and the URI it came from, kept to rasterize it again when the surface size changes. Null for a
    // raster background. Guarded by backgroundLock
    private var svgBackground: SvgBackground? = null
    private var svgBackgroundUri: Uri? = null
    // Surface size the SVG background was last rasterized for; guarded by backgroundLock
    private var svgRasterizedWidth = 0
    private var svgRasterizedHeight = 0
    // GL_MAX_TEXTURE_SIZE, read on the GL thread the first time an SVG is rasterized
    private var maxTextureSize = 0

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
        areScreensSwapped: Boolean,
    ) {
        synchronized(configurationLock) {
            this.topScreenRect = topScreenRect
            this.bottomScreenRect = bottomScreenRect
            this.topAlpha = topAlpha
            this.bottomAlpha = bottomAlpha
            this.topOnTop = topOnTop
            this.bottomOnTop = bottomOnTop
            this.isWideLayout = isWideLayout
            this.areScreensSwapped = areScreensSwapped
            mustUpdateConfiguration = true
        }
    }

    fun setBackground(background: RuntimeBackground) {
        synchronized(backgroundLock) {
            // The SVG parse is only kept across resizes and context changes of the same background
            val previousBackground = this.background?.background
            if (previousBackground?.id != background.background?.id || previousBackground?.uri != background.background?.uri) {
                clearSvgBackground()
            }
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

        // Called again in the same GL context when the render mode or surface size changes, so free what the previous call created
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

        // The texture has 2 empty lines between the screens (SCREEN_GAP), one on each side of the middle
        val lineRelativeSize = 1f / FRAME_TEXTURE_HEIGHT.toFloat()
        val topScreenBottomV = 0.5f - lineRelativeSize
        val bottomScreenTopV = 0.5f + lineRelativeSize

        // Lay out both screens for the current frame width, swap and wide-layout flags (see getScreensDrawing)
        val drawing = getScreensDrawing(topScreenRect, bottomScreenRect, frameTopScreenWidth, isWideLayout, areScreensSwapped, width)
        isBottomScreenWideInQuads = areScreensSwapped
        val topScreen = drawing.topScreen
        val bottomScreen = drawing.bottomScreen
        val topScreenVertexData = topScreen?.let { buildScreenVertexData(it.drawRect, buildScreenUvs(it.leftU, it.rightU, 0f, topScreenBottomV), topAlpha) }
        val bottomScreenVertexData = bottomScreen?.let { buildScreenVertexData(it.drawRect, buildScreenUvs(it.leftU, it.rightU, bottomScreenTopV, 1f), bottomAlpha) }
        val topOverlayVertexData = topScreen?.let { buildScreenVertexData(it.nativeRect, buildScreenUvs(0f, 1f, 0f, topScreenBottomV), topAlpha) }
        val bottomOverlayVertexData = bottomScreen?.let { buildScreenVertexData(it.nativeRect, buildScreenUvs(0f, 1f, bottomScreenTopV, 1f), bottomAlpha) }

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

        updateShadowCoordinates(topScreen?.drawRect, bottomScreen?.drawRect)
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
     * @param drawnBottomScreenRect Where the bottom screen is drawn, which is wider than its rect while widescreen is on with the screens
     * swapped
     */
    private fun updateShadowCoordinates(drawnTopScreenRect: Rect?, drawnBottomScreenRect: Rect?) {
        // Empty when the setting is off, so nothing is drawn
        val shadowVertexData = if (rendererConfiguration?.screenShadow == true) {
            listOfNotNull(
                drawnTopScreenRect?.let { buildShadowVertexData(it, topAlpha) },
                drawnBottomScreenRect?.let { buildShadowVertexData(it, bottomAlpha) },
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

        // 3x3 grid of cells around the screen minus the center; each vertex carries its distance outside the screen in radii
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
        val deband = rendererConfiguration?.deband ?: false
        // Building the filter compiles its shaders, so it's only done when the filter changes
        if (!mustRebuildScreenFilter && filtering == screenFilterFiltering && dsScreenColors == screenFilterDsScreenColors && deband == screenFilterDeband) {
            return
        }

        // Delete previous shader
        screenFilter?.delete()

        screenFilter = VideoFilterPipeline(VideoFilterShaderProvider.getFilter(filtering, deband), dsScreenColors)
        screenFilterFiltering = filtering
        screenFilterDsScreenColors = dsScreenColors
        screenFilterDeband = deband
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
            // An SVG background is rasterized at the surface's size, so it's rasterized again for the new one from the kept parse. Raster
            // backgrounds are only placed again
            val currentUri = background?.background?.uri
            if (svgBackground != null && currentUri != null && currentUri == svgBackgroundUri && (width != svgRasterizedWidth || height != svgRasterizedHeight)) {
                mustLoadBackground = true
            }
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

        // Pre-passes must run before anything is drawn to the surface; also called for invalid frames to clear the cache
        val screenTexture = screenFilter?.renderPrePasses(presentFrameWrapper, isBottomScreenWideInQuads, tiltShift)

        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        if (!presentFrameWrapper.isValidFrame) {
            return
        }

        loadBackgroundIfNeeded()
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
                        useScreenQuadShader(overlayShader, presentFrameWrapper.overlayTextureId)
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
        useScreenQuadShader(shader, textureId)
        shader.setTextureSize(textureWidth, textureHeight)
        shader.setTopScreenWidth(topScreenWidth)
        shader.setBottomScreenWide(isBottomScreenWideInQuads)
    }

    /**
     * Uses [shader] to draw the screen quads with [textureId] bound to its tex uniform, filtered the way the shader asks.
     */
    private fun useScreenQuadShader(shader: Shader, textureId: Int) {
        shader.use()

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textureId)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, shader.textureFiltering)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, shader.textureFiltering)

        setScreenVertexAttributes(shader)
        GLES30.glUniform1i(shader.uniformTex, 0)
    }

    private fun setScreenVertexAttributes(shader: Shader) {
        GLES30.glVertexAttribPointer(shader.attribPos, 2, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 0)
        GLES30.glVertexAttribPointer(shader.attribUv, 2, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 2 * Float.SIZE_BYTES)
        GLES30.glVertexAttribPointer(shader.attribAlpha, 1, GLES30.GL_FLOAT, false, FLOATS_PER_VERTEX * Float.SIZE_BYTES, 4 * Float.SIZE_BYTES)
    }

    private fun renderBackground() {
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
        setScreenVertexAttributes(shadowShader)

        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, shadowIndices)
    }

    /**
     * Loads the background into its texture if it has to be. GL thread only. Reading and decoding the file is done outside backgroundLock,
     * which setBackground() waits on from the main thread, and the result is dropped if the background was set again meanwhile.
     */
    private fun loadBackgroundIfNeeded() {
        val request = synchronized(backgroundLock) {
            if (!mustLoadBackground) {
                return
            }
            mustLoadBackground = false

            val currentBackground = background ?: return
            val uri = currentBackground.background?.uri
            if (uri == null) {
                clearSvgBackground()
                return
            }
            val cachedSvg = svgBackground?.takeIf { svgBackgroundUri == uri }
            if (cachedSvg != null) {
                BackgroundLoadRequest(currentBackground, uri, cachedSvg, svgRasterizedWidth, svgRasterizedHeight)
            } else {
                BackgroundLoadRequest(currentBackground, uri, null, 0, 0)
            }
        }

        val surfaceWidth = width.roundToInt()
        val surfaceHeight = height.roundToInt()
        val isSvgStartNanos = System.nanoTime()
        val isSvg = request.cachedSvg != null || BackgroundDecoder.isSvg(context, request.uri)
        val isSvgEndNanos = System.nanoTime()
        val svg = if (isSvg) request.cachedSvg ?: BackgroundDecoder.parseSvg(context, request.uri) else null
        val parseEndNanos = System.nanoTime()
        val bitmap = when {
            svg != null -> svg.rasterize(surfaceWidth, surfaceHeight, request.background.mode, getMaxTextureSize())
            // An SVG that can't be parsed is left out, same as a raster background that fails to decode
            isSvg -> null
            else -> decodeRasterBackground(request.uri)
        }
        val rasterEndNanos = System.nanoTime()
        var uploadNanos = 0L

        synchronized(backgroundLock) {
            // setBackground() was called while this was loading, so the next frame loads the new one
            if (mustLoadBackground || background !== request.background) {
                bitmap?.recycle()
                return
            }

            if (svg != null) {
                svgBackground = svg
                svgBackgroundUri = request.uri
                svgRasterizedWidth = surfaceWidth
                svgRasterizedHeight = surfaceHeight
            } else {
                clearSvgBackground()
            }

            bitmap?.let {
                val uploadStartNanos = System.nanoTime()
                uploadBackgroundBitmap(it)
                uploadNanos = System.nanoTime() - uploadStartNanos
            }
        }

        if (svg != null && bitmap != null) {
            SvgTimingLog.log(
                source = SvgTimingLog.SOURCE_RENDERER,
                backgroundName = request.background.background?.name,
                lastWidth = request.rasterizedWidth,
                lastHeight = request.rasterizedHeight,
                width = surfaceWidth,
                height = surfaceHeight,
                isSvgNanos = isSvgEndNanos - isSvgStartNanos,
                parseNanos = if (request.cachedSvg == null) parseEndNanos - isSvgEndNanos else null,
                rasterNanos = rasterEndNanos - parseEndNanos,
                uploadNanos = uploadNanos,
            )
        }
    }

    // The background to load, read under backgroundLock, with its kept SVG parse and the size that was last rasterized from it if there's one
    private class BackgroundLoadRequest(
        val background: RuntimeBackground,
        val uri: Uri,
        val cachedSvg: SvgBackground?,
        val rasterizedWidth: Int,
        val rasterizedHeight: Int,
    )

    // Guarded by backgroundLock
    private fun clearSvgBackground() {
        svgBackground = null
        svgBackgroundUri = null
        svgRasterizedWidth = 0
        svgRasterizedHeight = 0
    }

    private fun getMaxTextureSize(): Int {
        if (maxTextureSize <= 0) {
            val maxSize = IntArray(1)
            GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_SIZE, maxSize, 0)
            maxTextureSize = maxSize[0]
        }
        return maxTextureSize
    }

    private fun decodeRasterBackground(uri: Uri): Bitmap? {
        val backgroundSampleSize = BitmapUtils.calculateMinimumSampleSize(context, uri, width.roundToInt(), height.roundToInt())

        val bitmapResult = runCatching {
            context.contentResolver.openInputStream(uri)?.let { stream ->
                val options = BitmapFactory.Options().apply {
                    inSampleSize = backgroundSampleSize
                }

                BitmapFactory.decodeStream(stream, null, options)
            }
        }

        return bitmapResult.getOrNull()
    }

    /**
     * Replaces the background texture's image with [bitmap], which is recycled. Called under backgroundLock.
     */
    private fun uploadBackgroundBitmap(bitmap: Bitmap) {
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, backgroundTexture)
        GLUtils.texImage2D(GLES30.GL_TEXTURE_2D, 0, bitmap, 0)
        backgroundWidth = bitmap.width
        backgroundHeight = bitmap.height
        bitmap.recycle()

        isBackgroundLoaded = true
        isBackgroundPositionDirty = true
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