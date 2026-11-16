package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.camera.WideWindow
import me.magnum.melonds.domain.model.consoleAspectRatio

/**
 * Where a screen's Lua drawings go, in surface pixels, and the part of their SCREEN_WIDTH columns that shows ([leftU] to [rightU], 0 to 1)
 */
data class OverlayDrawing(val left: Float, val top: Float, val right: Float, val bottom: Float, val leftU: Float = 0f, val rightU: Float = 1f) {
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
 */
data class ScreenDrawing(
    val drawRect: Rect,
    val nativeRect: Rect,
    val leftU: Float,
    val rightU: Float,
    val overlay: OverlayDrawing = OverlayDrawing(nativeRect),
    val nativeRectAt1x: Rect = nativeRect,
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
 * for the window's show width and shows only the window, letterboxed when it's wider than the show width
 */
fun getScreensDrawing(
    topScreenRect: Rect?,
    bottomScreenRect: Rect?,
    frameWidth: Int,
    isWideLayout: Boolean,
    areScreensSwapped: Boolean,
    surfaceWidth: Float,
    wideWindow: WideWindow? = null,
): ScreensDrawing {
    val (wideScreenRect, narrowScreenRect) = if (areScreensSwapped) bottomScreenRect to topScreenRect else topScreenRect to bottomScreenRect
    // A window for another frame width (one presented before a width change) is left out
    val window = wideWindow?.takeIf { it.frameWidth == frameWidth && it.showWidth in (SCREEN_WIDTH + 1) until frameWidth }

    val wideScreen = wideScreenRect?.let {
        if (window != null) {
            val rect = getWideScreenDrawRect(it, narrowScreenRect, window.showWidth, isWideLayout, surfaceWidth)
            return@let getWindowedScreenDrawing(rect, window)
        }
        val drawRect = getWideScreenDrawRect(it, narrowScreenRect, frameWidth, isWideLayout, surfaceWidth)
        ScreenDrawing(drawRect, getNativeScreenRect(drawRect, frameWidth), 0f, 1f)
    }
    val narrowScreen = narrowScreenRect?.let {
        val drawRect = if (isWideLayout && areScreensSwapped) getLetterboxedRect(it, consoleAspectRatio) else it
        val leftU = (frameWidth.coerceAtLeast(SCREEN_WIDTH) - SCREEN_WIDTH) / (2f * frameWidth.coerceAtLeast(SCREEN_WIDTH))
        ScreenDrawing(drawRect, drawRect, leftU, 1f - leftU)
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
 * keeps the DS's pixel shape (so shorter, centered, when the window is wider than the show width), with the frame's middle SCREEN_WIDTH columns
 * wherever the window puts them and their Lua drawings cut to the picture
 */
private fun getWindowedScreenDrawing(rect: Rect, window: WideWindow): ScreenDrawing {
    val frameWidth = window.frameWidth.toFloat()
    val windowWidth = window.width.coerceIn(1f, frameWidth)
    val windowX = window.x.coerceIn(0f, frameWidth - windowWidth)
    val pictureHeight = rect.height * minOf(1f, window.showWidth / windowWidth)
    val drawRect = buildCenteredRect(rect.x + rect.width / 2f, rect.y + (rect.height - pictureHeight) / 2f, rect.width.toFloat(), pictureHeight)

    val pixelsPerColumn = drawRect.width / windowWidth
    val nativeLeft = drawRect.x + ((frameWidth - SCREEN_WIDTH) / 2f - windowX) * pixelsPerColumn
    val nativeRight = nativeLeft + SCREEN_WIDTH * pixelsPerColumn
    val nativeRect = buildCenteredRect((nativeLeft + nativeRight) / 2f, drawRect.y.toFloat(), nativeRight - nativeLeft, drawRect.height.toFloat())

    val overlayLeft = nativeLeft.coerceIn(drawRect.x.toFloat(), drawRect.right.toFloat())
    val overlayRight = nativeRight.coerceIn(overlayLeft, drawRect.right.toFloat())
    val overlay = OverlayDrawing(
        left = overlayLeft,
        top = drawRect.y.toFloat(),
        right = overlayRight,
        bottom = drawRect.bottom.toFloat(),
        leftU = (overlayLeft - nativeLeft) / (nativeRight - nativeLeft),
        rightU = (overlayRight - nativeLeft) / (nativeRight - nativeLeft),
    )
    return ScreenDrawing(drawRect, nativeRect, windowX / frameWidth, (windowX + windowWidth) / frameWidth, overlay, getNativeScreenRect(rect, window.showWidth))
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
