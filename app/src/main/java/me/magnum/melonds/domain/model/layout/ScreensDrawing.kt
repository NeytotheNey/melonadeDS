package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.camera.WideWindow
import me.magnum.melonds.domain.model.consoleAspectRatio

/**
 * Where a screen's Lua drawings go, in surface pixels, and the part of their SCREEN_WIDTH columns and SCREEN_HEIGHT rows that shows ([leftU] to
 * [rightU] and [topV] to [bottomV], 0 to 1)
 */
data class OverlayDrawing(
    val left: Float,
    val top: Float,
    val right: Float,
    val bottom: Float,
    val leftU: Float = 0f,
    val rightU: Float = 1f,
    val topV: Float = 0f,
    val bottomV: Float = 1f,
) {
    constructor(rect: Rect) : this(rect.x.toFloat(), rect.y.toFloat(), rect.right.toFloat(), rect.bottom.toFloat())
}

/**
 * How one DS screen is drawn from the frame.
 *
 * @param drawRect Where its picture goes
 * @param nativeRect Where its middle SCREEN_WIDTH columns (the 2D, Lua drawings and touches) end up
 * @param leftU The left edge of the columns of its half of the frame that are drawn, as a texture u coordinate
 * @param rightU The right edge of those columns
 * @param overlay Where its Lua drawings go: on [nativeRect], cut to [drawRect] when a camera window leaves part of it out
 * @param nativeRectAt1x [nativeRect] with a camera window at 1x, the largest any window shows the middle columns, for the output cap (the same
 * without a camera)
 * @param topRow The first of the rows of its half of the frame that are drawn, in DS pixels: the DS top screen's half is SCREEN_HEIGHT plus the
 * camera's margin above and below (stage 2), so without a window it shows the middle SCREEN_HEIGHT; the bottom screen's is SCREEN_HEIGHT
 * @param bottomRow The end of those rows
 */
data class ScreenDrawing(
    val drawRect: Rect,
    val nativeRect: Rect,
    val leftU: Float,
    val rightU: Float,
    val overlay: OverlayDrawing = OverlayDrawing(nativeRect),
    val nativeRectAt1x: Rect = nativeRect,
    val topRow: Float = 0f,
    val bottomRow: Float = SCREEN_HEIGHT.toFloat(),
)

/**
 * How the DS screens are drawn, or null for a screen the layout doesn't show
 */
data class ScreensDrawing(val topScreen: ScreenDrawing?, val bottomScreen: ScreenDrawing?) {
    /**
     * Where touches go on the bottom screen, or null if the layout doesn't show it.
     */
    val touchScreenRect get() = bottomScreen?.nativeRect
}

/**
 * How DSRenderer (and TouchscreenInputHandler, so taps line up) draws both screens from a [frameWidth]-wide frame: the screen in the layout's
 * top rect gets the whole half, the other its middle SCREEN_WIDTH columns, kept 4:3 when swapped in a wide layout.
 *
 * @param topScreenRect The rect the DS top screen is drawn in (the layout's bottom screen rect when [areScreensSwapped])
 * @param bottomScreenRect The rect the DS bottom screen is drawn in
 * @param surfaceWidth The surface width, which a wide screen in a layout that isn't wide can grow up to
 * @param wideWindow The WideMelon camera's window of the screen in the layout's top rect, or null without a camera: that screen is then shaped
 * for the window's show width and shows only the window, letterboxed by as much as the window's height can't cover
 * @param frameMargin The frame's rows above and below the DS top screen (the camera's margin, stage 2), which only the camera's window shows
 */
fun getScreensDrawing(
    topScreenRect: Rect?,
    bottomScreenRect: Rect?,
    frameWidth: Int,
    isWideLayout: Boolean,
    areScreensSwapped: Boolean,
    surfaceWidth: Float,
    wideWindow: WideWindow? = null,
    frameMargin: Int = 0,
): ScreensDrawing {
    val (wideScreenRect, narrowScreenRect) = if (areScreensSwapped) bottomScreenRect to topScreenRect else topScreenRect to bottomScreenRect
    // A window for another frame width (one presented before a width change) is left out
    val window = wideWindow?.takeIf { it.frameWidth == frameWidth && it.showWidth in (SCREEN_WIDTH + 1) until frameWidth }
    // The DS top screen's native rows, which a screen shows without a window
    val topScreenRows = frameMargin.coerceAtLeast(0).toFloat()

    val wideScreen = wideScreenRect?.let {
        if (window != null) {
            val rect = getWideScreenDrawRect(it, narrowScreenRect, window.showWidth, isWideLayout, surfaceWidth)
            // The bottom screen has no margin, so swapped (the camera keeps 1x then) it shows all its rows
            val rowWindow = if (areScreensSwapped && window.frameHeight != SCREEN_HEIGHT) {
                window.copy(frameHeight = SCREEN_HEIGHT, y = 0f, height = SCREEN_HEIGHT.toFloat())
            } else {
                window
            }
            return@let getWindowedScreenDrawing(rect, rowWindow)
        }
        val drawRect = getWideScreenDrawRect(it, narrowScreenRect, frameWidth, isWideLayout, surfaceWidth)
        val firstRow = if (areScreensSwapped) 0f else topScreenRows
        ScreenDrawing(drawRect, getNativeScreenRect(drawRect, frameWidth), 0f, 1f, topRow = firstRow, bottomRow = firstRow + SCREEN_HEIGHT)
    }
    val narrowScreen = narrowScreenRect?.let {
        val drawRect = if (isWideLayout && areScreensSwapped) getLetterboxedRect(it, consoleAspectRatio) else it
        val leftU = (frameWidth.coerceAtLeast(SCREEN_WIDTH) - SCREEN_WIDTH) / (2f * frameWidth.coerceAtLeast(SCREEN_WIDTH))
        val firstRow = if (areScreensSwapped) topScreenRows else 0f
        ScreenDrawing(drawRect, drawRect, leftU, 1f - leftU, topRow = firstRow, bottomRow = firstRow + SCREEN_HEIGHT)
    }

    return if (areScreensSwapped) ScreensDrawing(narrowScreen, wideScreen) else ScreensDrawing(wideScreen, narrowScreen)
}

/**
 * Draw rect of the top-slot screen: letterboxed in a wide layout, grown by [getWideTopScreenRect] otherwise.
 */
private fun getWideScreenDrawRect(rect: Rect, otherRect: Rect?, frameWidth: Int, isWideLayout: Boolean, surfaceWidth: Float): Rect {
    if (isWideLayout) {
        return getLetterboxedRect(rect, frameWidth.coerceAtLeast(SCREEN_WIDTH) / SCREEN_HEIGHT.toFloat())
    }
    if (frameWidth <= SCREEN_WIDTH) {
        return rect
    }

    return getWideTopScreenRect(rect, otherRect, frameWidth / SCREEN_HEIGHT.toFloat(), surfaceWidth)
}

/**
 * How a screen shaped for [window]'s show width at [rect] shows that window of its frame: the window across the rect's width, as tall as that
 * keeps the DS's pixel shape (so shorter, centered, when the window's rows can't cover what its width asks: no margin, or too small a one), with
 * the frame's middle SCREEN_WIDTH columns and SCREEN_HEIGHT rows wherever the window puts them and their Lua drawings cut to the picture
 */
private fun getWindowedScreenDrawing(rect: Rect, window: WideWindow): ScreenDrawing {
    val frameWidth = window.frameWidth.toFloat()
    val windowWidth = window.width.coerceIn(1f, frameWidth)
    val windowX = window.x.coerceIn(0f, frameWidth - windowWidth)
    val frameHeight = window.frameHeight.coerceAtLeast(SCREEN_HEIGHT).toFloat()
    val windowHeight = window.height.coerceIn(1f, frameHeight)
    val windowY = window.y.coerceIn(0f, frameHeight - windowHeight)
    // At the DS's pixel shape the rect's height shows SCREEN_HEIGHT * zoom rows, so fewer rows than that leave bars above and below
    val pictureHeight = rect.height * minOf(1f, (windowHeight / SCREEN_HEIGHT) * (window.showWidth / windowWidth))
    val drawRect = buildCenteredRect(rect.x + rect.width / 2f, rect.y + (rect.height - pictureHeight) / 2f, rect.width.toFloat(), pictureHeight)

    val pixelsPerColumn = drawRect.width / windowWidth
    val nativeLeft = drawRect.x + ((frameWidth - SCREEN_WIDTH) / 2f - windowX) * pixelsPerColumn
    val nativeRight = nativeLeft + SCREEN_WIDTH * pixelsPerColumn
    // The native rows as fractions of the window's, so a window of just those (no margin, or 1x) lands on the picture's edges exactly
    val margin = (frameHeight - SCREEN_HEIGHT) / 2f
    val nativeTop = drawRect.y + drawRect.height * ((margin - windowY) / windowHeight)
    val nativeBottom = drawRect.y + drawRect.height * ((margin + SCREEN_HEIGHT - windowY) / windowHeight)
    val nativeRect = buildCenteredRect((nativeLeft + nativeRight) / 2f, nativeTop, nativeRight - nativeLeft, nativeBottom - nativeTop)

    val overlayLeft = nativeLeft.coerceIn(drawRect.x.toFloat(), drawRect.right.toFloat())
    val overlayRight = nativeRight.coerceIn(overlayLeft, drawRect.right.toFloat())
    val overlayTop = nativeTop.coerceIn(drawRect.y.toFloat(), drawRect.bottom.toFloat())
    val overlayBottom = nativeBottom.coerceIn(overlayTop, drawRect.bottom.toFloat())
    val overlay = OverlayDrawing(
        left = overlayLeft,
        top = overlayTop,
        right = overlayRight,
        bottom = overlayBottom,
        leftU = (overlayLeft - nativeLeft) / (nativeRight - nativeLeft),
        rightU = (overlayRight - nativeLeft) / (nativeRight - nativeLeft),
        topV = (overlayTop - nativeTop) / (nativeBottom - nativeTop),
        bottomV = (overlayBottom - nativeTop) / (nativeBottom - nativeTop),
    )
    return ScreenDrawing(
        drawRect,
        nativeRect,
        windowX / frameWidth,
        (windowX + windowWidth) / frameWidth,
        overlay,
        getNativeScreenRect(rect, window.showWidth),
        windowY,
        windowY + windowHeight,
    )
}

/**
 * The middle SCREEN_WIDTH part of a screen that's [frameWidth] DS pixels wide and drawn at [drawRect]
 */
private fun getNativeScreenRect(drawRect: Rect, frameWidth: Int): Rect {
    if (frameWidth <= SCREEN_WIDTH) {
        return drawRect
    }

    val nativeWidth = drawRect.width * SCREEN_WIDTH / frameWidth.toFloat()
    return buildCenteredRect(drawRect.x + drawRect.width / 2f, drawRect.y.toFloat(), nativeWidth, drawRect.height.toFloat())
}
