package me.magnum.melonds.domain.model.render

import me.magnum.melonds.domain.model.SCREEN_GAP
import me.magnum.melonds.domain.model.SCREEN_HEIGHT

/**
 * Where the screens' rows are in a frame texture, as texture v coordinates (DSRenderer draws the screens with these): the DS top screen's half
 * is [topScreenRows] tall, SCREEN_HEIGHT plus the WideMelon camera's [topScreenMargin] above and below (stage 2), then SCREEN_GAP empty rows and
 * the bottom screen's SCREEN_HEIGHT. Without a margin it's the old FRAME_TEXTURE_HEIGHT layout, with exactly the old v coordinates.
 */
class FrameTextureLayout(val topScreenMargin: Int) {

    val topScreenRows = SCREEN_HEIGHT + 2 * topScreenMargin.coerceAtLeast(0)
    val frameRows = (topScreenRows + SCREEN_GAP + SCREEN_HEIGHT).toFloat()
    private val bottomScreenStartRow = topScreenRows + SCREEN_GAP

    // The gap's 2 rows are one on each side of the split, which is the middle (0.5) without a margin
    private val lineRelativeSize = 1f / frameRows
    private val split = (topScreenRows + SCREEN_GAP / 2) / frameRows

    // The end of the top screen's half and the start of the bottom screen's
    val topScreenBottomV = split - lineRelativeSize
    val bottomScreenTopV = split + lineRelativeSize

    /**
     * The v of [row] of the top screen's half (0 to topScreenRows, ScreenDrawing.topRow or bottomRow); its edges are exactly 0 and
     * [topScreenBottomV]
     */
    fun topScreenV(row: Float): Float = when {
        row <= 0f -> 0f
        row >= topScreenRows -> topScreenBottomV
        else -> row / frameRows
    }

    /**
     * The v of [row] of the bottom screen's half (0 to SCREEN_HEIGHT); its edges are exactly [bottomScreenTopV] and 1
     */
    fun bottomScreenV(row: Float): Float = when {
        row <= 0f -> bottomScreenTopV
        row >= SCREEN_HEIGHT -> 1f
        else -> (bottomScreenStartRow + row) / frameRows
    }

    companion object {
        // The Lua overlay texture always has the margin-free layout (LuaOverlay::kHeight)
        val OVERLAY = FrameTextureLayout(0)
    }
}
