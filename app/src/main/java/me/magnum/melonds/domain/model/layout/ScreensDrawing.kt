package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.consoleAspectRatio

/**
 * How one DS screen is drawn from the frame.
 *
 * @param drawRect Where its picture goes
 * @param nativeRect Where its middle SCREEN_WIDTH columns (the 2D, Lua drawings and touches) end up
 * @param leftU The left edge of the columns of its half of the frame that are drawn, as a texture u coordinate
 * @param rightU The right edge of those columns
 */
data class ScreenDrawing(val drawRect: Rect, val nativeRect: Rect, val leftU: Float, val rightU: Float)

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
 */
fun getScreensDrawing(
    topScreenRect: Rect?,
    bottomScreenRect: Rect?,
    frameWidth: Int,
    isWideLayout: Boolean,
    areScreensSwapped: Boolean,
    surfaceWidth: Float,
): ScreensDrawing {
    val (wideScreenRect, narrowScreenRect) = if (areScreensSwapped) bottomScreenRect to topScreenRect else topScreenRect to bottomScreenRect

    val wideScreen = wideScreenRect?.let {
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
 * The middle SCREEN_WIDTH part of a screen that's [frameWidth] DS pixels wide and drawn at [drawRect]
 */
private fun getNativeScreenRect(drawRect: Rect, frameWidth: Int): Rect {
    if (frameWidth <= SCREEN_WIDTH) {
        return drawRect
    }

    val nativeWidth = drawRect.width * SCREEN_WIDTH / frameWidth.toFloat()
    return buildCentredRect(drawRect.x + drawRect.width / 2f, drawRect.y.toFloat(), nativeWidth, drawRect.height.toFloat())
}
