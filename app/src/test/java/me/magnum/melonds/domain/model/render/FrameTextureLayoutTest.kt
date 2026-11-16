package me.magnum.melonds.domain.model.render

import org.junit.Assert.assertEquals
import org.junit.Test

class FrameTextureLayoutTest {

    @Test
    fun withoutAMarginTheVCoordinatesAreTheOldOnesExactly() {
        val layout = FrameTextureLayout(0)

        assertEquals(386f, layout.frameRows, 0f)
        // What DSRenderer used before the margin, bit for bit
        assertEquals(0.5f - 1f / 386f, layout.topScreenBottomV, 0f)
        assertEquals(0.5f + 1f / 386f, layout.bottomScreenTopV, 0f)
        assertEquals(0f, layout.topScreenV(0f), 0f)
        assertEquals(layout.topScreenBottomV, layout.topScreenV(192f), 0f)
        assertEquals(layout.bottomScreenTopV, layout.bottomScreenV(0f), 0f)
        assertEquals(1f, layout.bottomScreenV(192f), 0f)
    }

    @Test
    fun aMarginMakesTheTopScreensHalfTaller() {
        // 342 shown from 512: 48 rows above and below, so 288 + 2 + 192 rows
        val layout = FrameTextureLayout(48)

        assertEquals(288, layout.topScreenRows)
        assertEquals(482f, layout.frameRows, 0f)
        assertEquals(288f / 482f, layout.topScreenBottomV, 1e-6f)
        assertEquals(290f / 482f, layout.bottomScreenTopV, 1e-6f)
    }

    @Test
    fun theTopScreensNativeRowsSitInTheMiddleOfItsHalf() {
        val layout = FrameTextureLayout(48)

        // DS line 0 is row 48, line 191 ends at row 240
        assertEquals(48f / 482f, layout.topScreenV(48f), 1e-6f)
        assertEquals(240f / 482f, layout.topScreenV(240f), 1e-6f)
        // The whole half's edges are exact
        assertEquals(0f, layout.topScreenV(0f), 0f)
        assertEquals(layout.topScreenBottomV, layout.topScreenV(288f), 0f)
    }

    @Test
    fun theBottomScreenFollowsTheGap() {
        val layout = FrameTextureLayout(48)

        assertEquals(layout.bottomScreenTopV, layout.bottomScreenV(0f), 0f)
        assertEquals((290f + 96f) / 482f, layout.bottomScreenV(96f), 1e-6f)
        assertEquals(1f, layout.bottomScreenV(192f), 0f)
    }

    @Test
    fun theOverlayKeepsTheOldLayout() {
        assertEquals(FrameTextureLayout(0).topScreenBottomV, FrameTextureLayout.OVERLAY.topScreenBottomV, 0f)
        assertEquals(FrameTextureLayout(0).bottomScreenTopV, FrameTextureLayout.OVERLAY.bottomScreenTopV, 0f)
    }
}
