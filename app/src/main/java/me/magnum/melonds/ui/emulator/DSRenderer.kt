package me.magnum.melonds.ui.emulator

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.opengl.GLES30
import android.opengl.GLUtils
import android.util.Log
import me.magnum.melonds.common.opengl.Shader
import me.magnum.melonds.common.opengl.ShaderFactory
import me.magnum.melonds.common.opengl.ShaderProgramSource
import me.magnum.melonds.common.opengl.VideoFilterPipeline
import me.magnum.melonds.common.opengl.VideoFilterShaderProvider
import me.magnum.melonds.common.opengl.shouldFoldDeband
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.VideoFiltering
import me.magnum.melonds.domain.model.camera.WideWindow
import me.magnum.melonds.domain.model.layout.BackgroundMode
import me.magnum.melonds.domain.model.layout.OverlayDrawing
import me.magnum.melonds.domain.model.layout.getScreensDrawing
import me.magnum.melonds.domain.model.render.FrameTextureLayout
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_BOTTOM
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_TOP
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
import me.magnum.melonds.domain.model.render.Shade
import me.magnum.melonds.domain.model.render.Sun
import me.magnum.melonds.domain.model.render.TiltShift
import me.magnum.melonds.impl.diagnostics.DebugSwitches
import me.magnum.melonds.impl.diagnostics.LogTags
import me.magnum.melonds.impl.diagnostics.SvgTimingLog
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration
import me.magnum.melonds.ui.emulator.render.EmulatorRenderer
import me.magnum.melonds.utils.BackgroundDecoder
import me.magnum.melonds.utils.BitmapUtils
import me.magnum.melonds.utils.SvgBackground
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil
import java.nio.FloatBuffer
import kotlin.math.roundToInt

// Screen shadow: radius as a share of the screen's shorter side with a minimum in surface pixels, and alpha at the screen's edge
private const val SHADOW_RADIUS_FACTOR = 0.06f
private const val SHADOW_MIN_RADIUS = 8
private const val SHADOW_STRENGTH = 0.6f

// Screen and shadow vertices: position (2), UVs or distance outside the screen (2) and alpha (1); background vertices have no alpha
private const val FLOATS_PER_VERTEX = 5
private const val BACKGROUND_FLOATS_PER_VERTEX = 4
private const val VERTICES_PER_SCREEN = 6
// Both screens and their Lua drawings
private const val MAX_SCREEN_VERTEX_FLOATS = 4 * VERTICES_PER_SCREEN * FLOATS_PER_VERTEX

// The screens' draw orders (DSRenderer.screenQuadScreens), shared so that building the quads allocates nothing
private val SCREENS_NONE = IntArray(0)
private val SCREENS_TOP = intArrayOf(OVERLAY_SCREEN_TOP)
private val SCREENS_BOTTOM = intArrayOf(OVERLAY_SCREEN_BOTTOM)
private val SCREENS_TOP_THEN_BOTTOM = intArrayOf(OVERLAY_SCREEN_TOP, OVERLAY_SCREEN_BOTTOM)
private val SCREENS_BOTTOM_THEN_TOP = intArrayOf(OVERLAY_SCREEN_BOTTOM, OVERLAY_SCREEN_TOP)

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
    // The same for the bloom and vignette, null for neither
    @Volatile
    var bloom: Bloom? = null
    // The same for the depth of field, null for none
    @Volatile
    var depthOfField: DepthOfField? = null
    // The same for the shading, null for none
    @Volatile
    var shade: Shade? = null
    // The same for the sun shadows, null for none
    @Volatile
    var sun: Sun? = null
    private lateinit var backgroundShader: Shader
    private lateinit var shadowShader: Shader
    private lateinit var overlayShader: Shader
    // Whether the shaders above exist; program names are shared with the emulator's context, so deleting one twice could free a reused name
    private var areShadersCreated = false

    private var screensVbo = 0
    private var screensVao = 0
    private var screenIndices = 0
    // The DS screen (OVERLAY_SCREEN_TOP or OVERLAY_SCREEN_BOTTOM) of each quad in screensVbo in draw order, so its Lua drawings can go right after it
    // The output cap (GPU::OutputCap, on by default): the largest need of the screens the quads were built for (see outputScaleNeed); GL thread only
    private var screensOutputScaleNeed = 0
    // Surface pixels the screen quads cover (their draw rects inside the surface, summed), for shouldFoldDeband; GL thread only
    private var screensDrawnPixels = 0L
    // debug.melonade.debandfold (default 1: deband in the screen shader where shouldFoldDeband allows it; 0 = the pre-pass), read again for each game
    // (DebugSwitches.generation), and whether the last valid frame folded it in, for the DEBANDFOLD log line; GL thread only
    private var isDebandFoldOn = false
    private var debandFoldGeneration = -1
    private var wasDebandFolded = false
    private var screenQuadScreens = SCREENS_NONE
    // Reused for every quad rebuild, which happens every frame while the camera's window moves; GL thread only
    private val screenVertexBuffer: FloatBuffer = ByteBuffer.allocateDirect(MAX_SCREEN_VERTEX_FLOATS * Float.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    // Whether screensVbo has its storage yet (it's made again with the GL objects)
    private var isScreensVboAllocated = false
    // The drawn rects the shadows were last built for, so a camera move that keeps them skips the shadows
    private var shadowTopRect: Rect? = null
    private var shadowBottomRect: Rect? = null
    // Top-screen width of the frame the quads were built for; GL thread writes, touch input reads
    @Volatile
    var frameTopScreenWidth = SCREEN_WIDTH
        private set
    // The rows of the frame the quads were built for, which the WideMelon camera's margin (PresentFrameWrapper.topScreenMargin) makes taller;
    // GL thread only
    private var frameLayout = FrameTextureLayout(0)
    // The WideMelon camera's window the quads were built for, null without one; GL thread writes, touch input reads
    @Volatile
    var frameWideWindow: WideWindow? = null
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
        isScreensVboAllocated = false

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
        updateScreenCoordinates(isWindowOnly = false)
        updateShader()
    }

    /**
     * Builds the screen and Lua drawing quads into screensVbo. With [isWindowOnly] (only the camera's window moved) the shadows are only
     * rebuilt if a screen's drawn rect changed, and nothing is allocated for the quads themselves.
     */
    private fun updateScreenCoordinates(isWindowOnly: Boolean) {
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

        // The texture has 2 empty lines between the screens (SCREEN_GAP), and the top screen's half is taller by the camera's margin (stage 2):
        // see FrameTextureLayout. The Lua overlay keeps the old layout at any margin
        val layout = frameLayout
        val overlayLayout = FrameTextureLayout.OVERLAY

        // Lay out both screens for the current frame width, swap and wide-layout flags (see getScreensDrawing)
        val drawing = getScreensDrawing(
            topScreenRect, bottomScreenRect, frameTopScreenWidth, isWideLayout, areScreensSwapped, width, frameWideWindow, layout.topScreenMargin,
        )
        isBottomScreenWideInQuads = areScreensSwapped
        val topScreen = drawing.topScreen
        val bottomScreen = drawing.bottomScreen
        // The output cap (GPU::OutputCap): each screen's middle columns are shown at nativeRect's size, which the frame has to be at least as big as.
        // With the camera that's their size at 1x (nativeRectAt1x), the largest a window shows them, so zooming and panning never change it
        val outputScaleNeed = maxOf(topScreen?.let { getOutputScaleNeed(it.nativeRectAt1x) } ?: 0, bottomScreen?.let { getOutputScaleNeed(it.nativeRectAt1x) } ?: 0)
        screensDrawnPixels = (topScreen?.let { visibleArea(it.drawRect) } ?: 0L) + (bottomScreen?.let { visibleArea(it.drawRect) } ?: 0L)
        if (outputScaleNeed != screensOutputScaleNeed) {
            Log.i(LogTags.DEBUG, "OUTCAP: surface ${width.toInt()}x${height.toInt()} need $outputScaleNeed (was $screensOutputScaleNeed): " +
                "top ${topScreen?.let { "${it.nativeRect.width}x${it.nativeRect.height} draw ${it.drawRect.width}x${it.drawRect.height}" } ?: "none"}, " +
                "bottom ${bottomScreen?.let { "${it.nativeRect.width}x${it.nativeRect.height} draw ${it.drawRect.width}x${it.drawRect.height}" } ?: "none"}, " +
                "drawn $screensDrawnPixels px, frame width $frameTopScreenWidth")
            screensOutputScaleNeed = outputScaleNeed
        }

        // Under-screen first, then the one drawn over it, each screen's Lua drawings after both in the same order
        val topFirst = bottomOnTop
        screenQuadScreens = when {
            topScreen != null && bottomScreen != null -> if (topFirst) SCREENS_TOP_THEN_BOTTOM else SCREENS_BOTTOM_THEN_TOP
            topScreen != null -> SCREENS_TOP
            bottomScreen != null -> SCREENS_BOTTOM
            else -> SCREENS_NONE
        }

        val vertexBuffer = screenVertexBuffer
        vertexBuffer.clear()
        screenQuadScreens.forEach {
            if (it == OVERLAY_SCREEN_TOP) {
                val topV = layout.topScreenV(topScreen!!.topRow)
                val bottomV = layout.topScreenV(topScreen.bottomRow)
                putQuad(vertexBuffer, topScreen.drawRect, topScreen.leftU, topScreen.rightU, topV, bottomV, topAlpha)
            } else {
                val topV = layout.bottomScreenV(bottomScreen!!.topRow)
                val bottomV = layout.bottomScreenV(bottomScreen.bottomRow)
                putQuad(vertexBuffer, bottomScreen.drawRect, bottomScreen.leftU, bottomScreen.rightU, topV, bottomV, bottomAlpha)
            }
        }
        screenQuadScreens.forEach {
            if (it == OVERLAY_SCREEN_TOP) {
                putOverlayQuad(vertexBuffer, topScreen!!.overlay, 0f, overlayLayout.topScreenBottomV, topAlpha)
            } else {
                putOverlayQuad(vertexBuffer, bottomScreen!!.overlay, overlayLayout.bottomScreenTopV, 1f, bottomAlpha)
            }
        }
        val usedBytes = vertexBuffer.position() * Float.SIZE_BYTES
        vertexBuffer.position(0)

        GLES30.glBindVertexArray(screensVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, screensVbo)
        // The buffer's storage is made once at its largest size, and only filled after that
        if (!isScreensVboAllocated) {
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, MAX_SCREEN_VERTEX_FLOATS * Float.SIZE_BYTES, null, GLES30.GL_DYNAMIC_DRAW)
            isScreensVboAllocated = true
        }
        if (usedBytes > 0) {
            GLES30.glBufferSubData(GLES30.GL_ARRAY_BUFFER, 0, usedBytes, vertexBuffer)
        }
        screenIndices = screenQuadScreens.size * VERTICES_PER_SCREEN

        val topDrawRect = topScreen?.drawRect
        val bottomDrawRect = bottomScreen?.drawRect
        if (!isWindowOnly || topDrawRect != shadowTopRect || bottomDrawRect != shadowBottomRect) {
            updateShadowCoordinates(topDrawRect, bottomDrawRect)
        }
    }

    // The part of rect inside the surface, in pixels
    private fun visibleArea(rect: Rect): Long {
        val visibleWidth = minOf(rect.x + rect.width, width.toInt()) - maxOf(rect.x, 0)
        val visibleHeight = minOf(rect.y + rect.height, height.toInt()) - maxOf(rect.y, 0)
        return if (visibleWidth > 0 && visibleHeight > 0) visibleWidth.toLong() * visibleHeight else 0L
    }

    // The output cap's need for a screen whose middle columns are shown at [nativeRect]'s size
    private fun getOutputScaleNeed(nativeRect: Rect): Int {
        return ceil(maxOf(nativeRect.width / SCREEN_WIDTH.toDouble(), nativeRect.height / SCREEN_HEIGHT.toDouble()) - 1e-3).toInt().coerceAtLeast(1)
    }

    /**
     * Writes a screen quad at [rect] showing columns [leftU] to [rightU] and rows [topV] to [bottomV] of the texture
     */
    private fun putQuad(buffer: FloatBuffer, rect: Rect, leftU: Float, rightU: Float, topV: Float, bottomV: Float, alpha: Float) {
        putQuad(buffer, rect.x.toFloat(), rect.y.toFloat(), rect.right.toFloat(), rect.bottom.toFloat(), leftU, rightU, topV, bottomV, alpha)
    }

    /**
     * A screen's Lua drawings quad: [overlay]'s place, columns and the part of its screen's rows ([topV] to [bottomV] of the overlay texture)
     */
    private fun putOverlayQuad(buffer: FloatBuffer, overlay: OverlayDrawing, topV: Float, bottomV: Float, alpha: Float) {
        val rowsTopV = lerpV(topV, bottomV, overlay.topV)
        val rowsBottomV = lerpV(topV, bottomV, overlay.bottomV)
        putQuad(buffer, overlay.left, overlay.top, overlay.right, overlay.bottom, overlay.leftU, overlay.rightU, rowsTopV, rowsBottomV, alpha)
    }

    // start to end at t (0 to 1), landing on the ends exactly
    private fun lerpV(start: Float, end: Float, t: Float): Float = when {
        t <= 0f -> start
        t >= 1f -> end
        else -> start + (end - start) * t
    }

    private fun putQuad(
        buffer: FloatBuffer,
        rectLeft: Float,
        rectTop: Float,
        rectRight: Float,
        rectBottom: Float,
        leftU: Float,
        rightU: Float,
        topV: Float,
        bottomV: Float,
        alpha: Float,
    ) {
        val left = (rectLeft / width) * 2f - 1f
        val right = (rectRight / width) * 2f - 1f
        val top = 1f - rectTop / height * 2f
        val bottom = 1f - rectBottom / height * 2f

        // Position, UVs and alpha, in the vertex order of the indices above
        buffer.put(left).put(bottom).put(leftU).put(bottomV).put(alpha)
        buffer.put(left).put(top).put(leftU).put(topV).put(alpha)
        buffer.put(right).put(top).put(rightU).put(topV).put(alpha)
        buffer.put(left).put(bottom).put(leftU).put(bottomV).put(alpha)
        buffer.put(right).put(top).put(rightU).put(topV).put(alpha)
        buffer.put(right).put(bottom).put(rightU).put(bottomV).put(alpha)
    }

    /**
     * @param drawnTopScreenRect Where the top screen is drawn, which is wider than its rect while widescreen is on
     * @param drawnBottomScreenRect Where the bottom screen is drawn, which is wider than its rect while widescreen is on with the screens
     * swapped
     */
    private fun updateShadowCoordinates(drawnTopScreenRect: Rect?, drawnBottomScreenRect: Rect?) {
        shadowTopRect = drawnTopScreenRect
        shadowBottomRect = drawnBottomScreenRect
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
            // And the rows of its layout, which the camera's margin makes taller
            if (presentFrameWrapper.isValidFrame && presentFrameWrapper.topScreenMargin != frameLayout.topScreenMargin) {
                frameLayout = FrameTextureLayout(presentFrameWrapper.topScreenMargin)
                mustUpdateConfiguration = true
            }
            // And the camera's window, which moves every frame while it eases or pans: then only the quads are filled again (and the shadows if
            // a screen's drawn rect changed)
            var hasWindowMoved = false
            if (presentFrameWrapper.isValidFrame && presentFrameWrapper.wideWindow != frameWideWindow) {
                frameWideWindow = presentFrameWrapper.wideWindow
                hasWindowMoved = true
            }
            if (mustUpdateConfiguration) {
                applyRendererConfiguration()
                mustUpdateConfiguration = false
            } else if (hasWindowMoved) {
                updateScreenCoordinates(isWindowOnly = true)
            }
        }

        // debug.melonade.comp1x: a frame with screens the compositor drew at 1x goes through the 1x shader when the full frame would only go
        // through the no-filter shader. Any other way (another filter or a pre-pass, in frames made before the emulator learnt of it, or a paused
        // frame presented again after the filter changed), renderPrePasses() expands it into the full frame first
        val currentTiltShift = tiltShift
        val currentBloom = bloom
        val currentDepthOfField = depthOfField
        val currentShade = shade
        val currentSun = sun
        val filter = screenFilter
        val halvesAt1xShader = if (
            presentFrameWrapper.isValidFrame && presentFrameWrapper.halvesAt1x != 0 && currentTiltShift == null && currentBloom == null &&
            currentDepthOfField == null && currentShade == null && currentSun == null && filter?.canDrawHalvesAt1x == true
        ) {
            filter.getHalvesAt1xShader()
        } else {
            null
        }

        // debug.melonade.debandfold: deband inside the screen shader, once per drawn pixel, instead of the pre-pass over every frame texel, when
        // shouldFoldDeband allows it for this frame and layout; the pre-pass otherwise, or if the deband screen shader doesn't build
        if (debandFoldGeneration != DebugSwitches.generation) {
            debandFoldGeneration = DebugSwitches.generation
            isDebandFoldOn = DebugSwitches.readInt("debandfold", 1) != 0
        }
        val frameTexels = presentFrameWrapper.textureWidth.toLong() * presentFrameWrapper.textureHeight
        // Only a Lua effect that adds passes (a tilt-shift, bloom, vignette, depth of field, shading or sun shadows, see
        // VideoFilterPipeline.renderPrePasses) keeps deband in the pre-pass
        val isLuaEffectActive = currentTiltShift?.let { it.hasBlur || it.hasColorBoost } == true || currentBloom?.isOff == false ||
            currentDepthOfField?.isOff == false || currentShade?.isOff == false || currentSun?.isOff == false
        val debandScreenShader = if (
            presentFrameWrapper.isValidFrame && halvesAt1xShader == null && filter != null &&
            shouldFoldDeband(isDebandFoldOn, filter.canFoldDeband, isLuaEffectActive, screensDrawnPixels, frameTexels)
        ) {
            filter.getDebandScreenShader()
        } else {
            null
        }
        if (presentFrameWrapper.isValidFrame && (debandScreenShader != null) != wasDebandFolded) {
            wasDebandFolded = debandScreenShader != null
            Log.i(LogTags.DEBUG, "DEBANDFOLD: ${if (wasDebandFolded) "screen shader" else "pre-pass or none"} (switch ${if (isDebandFoldOn) 1 else 0}, " +
                "filter ${filter?.canFoldDeband == true}, Lua effect $isLuaEffectActive, drawn $screensDrawnPixels px, frame $frameTexels texels)")
        }

        // Pre-passes must run before anything is drawn to the surface; also called for invalid frames to clear the cache
        val screenTexture = if (halvesAt1xShader == null) {
            filter?.renderPrePasses(presentFrameWrapper, isBottomScreenWideInQuads, currentTiltShift, currentBloom, currentDepthOfField,
                currentShade, currentSun, foldDeband = debandScreenShader != null)
        } else {
            null
        }

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

        // The 1x shader reads which texel the frame's sample picks from these (see VideoFilterPipeline.bindHalvesAt1xCoordinates)
        if (halvesAt1xShader != null) {
            filter?.bindHalvesAt1xCoordinates(presentFrameWrapper.textureWidth, presentFrameWrapper.textureHeight)
        }

        (halvesAt1xShader ?: debandScreenShader ?: filter?.screenShader)?.let { shader ->
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
                useScreenShader(shader, screenTextureId, screenTextureWidth, screenTextureHeight, presentFrameWrapper)
                GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, screenIndices)
            } else {
                // Each screen, then its Lua drawings, so that a screen drawn over the other one also covers the other one's drawings
                screenQuadScreens.forEachIndexed { index, screen ->
                    val firstVertex = index * VERTICES_PER_SCREEN
                    useScreenShader(shader, screenTextureId, screenTextureWidth, screenTextureHeight, presentFrameWrapper)
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

    private fun useScreenShader(shader: Shader, textureId: Int, textureWidth: Int, textureHeight: Int, frame: PresentFrameWrapper) {
        useScreenQuadShader(shader, textureId)
        shader.setTextureSize(textureWidth, textureHeight)
        shader.setTopScreenWidth(frame.topScreenWidth)
        shader.setTopScreenHeight(frame.topScreenHeight)
        shader.setBottomScreenWide(isBottomScreenWideInQuads)
        shader.setHalvesAt1x(frame.halvesAt1x, frame.scaleAt1x, frame.halfScaleAt1x)
    }

    // debug.melonade.comp1x: only the no-filter shader without deband or a Lua tilt-shift, bloom, vignette, depth of field, shading or sun
    // shadows draws a screen at 1x straight from the 1x image; any other way the frame has to be expanded first, which costs more than the 1x
    // draw saves. A check of state only, asked on every frame
    override fun canDrawHalvesAt1x(): Boolean {
        return tiltShift == null && bloom == null && depthOfField == null && shade == null && sun == null &&
            screenFilter?.canDrawHalvesAt1x == true
    }

    override fun outputScaleNeed(): Int = screensOutputScaleNeed

    // The deband the screen filter was last built with (updateScreenFilter), so it follows an in-game toggle
    override fun debandsOutput(): Boolean = screenFilterDeband

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