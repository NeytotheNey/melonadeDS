package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH

/**
 * How one DS screen is drawn from the frame.
 *
 * @param drawRect Where its picture goes
 * @param nativeRect Where its middle SCREEN_WIDTH columns end up, which is where the 2D is: its Lua drawings go there, and for the bottom
 * screen its touches are taken from there. The same as [drawRect] unless the screen is drawn wide
 * @param leftU The left edge of the columns of its half of the frame that are drawn, as a texture u coordinate
 * @param rightU The right edge of those columns
 */
data class ScreenDrawing(val drawRect: Rect, val nativeRect: Rect, val leftU: Float, val rightU: Float)

/**
 * How the DS screens are drawn, or null for a screen the layout doesn't show
 */
data class ScreensDrawing(val topScreen: ScreenDrawing?, val bottomScreen: ScreenDrawing?) {
    /**
     * Where the bottom screen's middle SCREEN_WIDTH columns are, so where touches go, or null if the layout doesn't show it
     */
    val touchScreenRect get() = bottomScreen?.nativeRect
}

/**
 * How the DS screens are drawn by DSRenderer, which TouchscreenInputHandler also uses so that taps line up with the picture. The frame's halves
 * are both [frameWidth] DS pixels wide, with their real SCREEN_WIDTH columns in the middle, and anything around those (widescreen's wings) is
 * only drawn for the screen in the layout's top screen rect: the DS top screen, or the bottom one when [areScreensSwapped]. A game that has
 * its 3D on the bottom screen gets it drawn wide that way, once the player swaps the screens.
 *
 * - The wide screen gets its half's whole width, placed by [getWideScreenDrawRect] in its rect.
 * - The other one gets the middle SCREEN_WIDTH columns of its half. Its rect is used as it is (the player may have made it 16:9 or free in the
 *   editor), except with the screens swapped in a wide layout: its rect is then the bottom screen's one, which wasn't made for it, so it keeps its
 *   4:3 shape in the middle of it.
 *
 * @param topScreenRect The rect the DS top screen is drawn in, which is the layout's bottom screen rect when [areScreensSwapped]
 * @param bottomScreenRect The rect the DS bottom screen is drawn in
 * @param surfaceWidth The width of the surface the screens are drawn on, which a wide screen in a layout that isn't wide can grow up to
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
        val drawRect = if (isWideLayout && areScreensSwapped) getLetterboxedRect(it, SCREEN_WIDTH / SCREEN_HEIGHT.toFloat()) else it
        val leftU = (frameWidth.coerceAtLeast(SCREEN_WIDTH) - SCREEN_WIDTH) / (2f * frameWidth.coerceAtLeast(SCREEN_WIDTH))
        ScreenDrawing(drawRect, drawRect, leftU, 1f - leftU)
    }

    return if (areScreensSwapped) ScreensDrawing(narrowScreen, wideScreen) else ScreensDrawing(wideScreen, narrowScreen)
}

/**
 * Where the screen in the layout's top screen rect [rect] is drawn when the frame is [frameWidth] DS pixels wide. In a wide layout the frame is
 * fitted in [rect] at its own aspect ratio, whatever its width, so a frame whose width doesn't match the variant yet (just after a fold, or while
 * paused) never spreads beyond the rect. Otherwise a SCREEN_WIDTH frame takes [rect] itself, and a wider one is drawn at its own aspect ratio,
 * placed by getWideTopScreenRect so that it doesn't cover the other screen's rect [otherRect]. Stored layouts never change
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
