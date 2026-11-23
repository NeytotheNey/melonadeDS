package me.magnum.melonds.domain.model.render

import me.magnum.melonds.domain.model.SCREEN_WIDTH

/**
 * The sun shadows' guard band in a frame's top screen half (melon.screen.sun's guard, MelonInstance's FrameGuard), in DS pixels: the columns
 * left and right of the shown top screen and the rows above and below it that are drawn and filtered but not shown. The shown top screen sits
 * between them with the native SCREEN_WIDTH x SCREEN_HEIGHT in its middle, and the bottom screen's columns are under the native ones.
 */
data class FrameGuard(val left: Int = 0, val right: Int = 0, val top: Int = 0, val bottom: Int = 0) {

    /**
     * Kept within a frame [frameWidth] wide with [frameMargin] rows (MelonInstance does already), so a bad one can't turn the shown frame inside
     * out: the same guard on every side to the least room any side has, as before sides were picked, otherwise each axis to its own room.
     */
    fun coercedTo(frameWidth: Int, frameMargin: Int): FrameGuard {
        val roomX = maxOf(0, frameWidth - SCREEN_WIDTH)
        val roomY = maxOf(0, 2 * frameMargin)
        if (left == right && left == top && left == bottom) {
            val guard = left.coerceIn(0, minOf(roomX / 2, roomY / 2))
            return FrameGuard(guard, guard, guard, guard)
        }
        val shownLeft = left.coerceIn(0, roomX)
        val shownTop = top.coerceIn(0, roomY)
        return FrameGuard(shownLeft, right.coerceIn(0, roomX - shownLeft), shownTop, bottom.coerceIn(0, roomY - shownTop))
    }

    // The shown top screen's width and margin rows in a frame [frameWidth] wide with [frameMargin] rows
    fun shownWidth(frameWidth: Int) = frameWidth - left - right
    fun shownMargin(frameMargin: Int) = frameMargin - (top + bottom) / 2

    // The frame's columns left of native column 0 (both halves) and the top screen half's rows above DS line 0
    fun nativeLeft(frameWidth: Int) = left + (shownWidth(frameWidth) - SCREEN_WIDTH) / 2f
    fun nativeTop(frameMargin: Int) = top + shownMargin(frameMargin).toFloat()

    // The native screen's offset from the frame's middle in DS pixels, right and down, as the core's view shift has it (GLRenderer::SetViewShift)
    val shiftX get() = (left - right) / 2f
    val shiftY get() = (top - bottom) / 2f

    companion object {
        val NONE = FrameGuard()

        // The same on all four sides
        fun all(guard: Int) = FrameGuard(guard, guard, guard, guard)
    }
}
