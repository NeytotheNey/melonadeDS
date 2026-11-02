package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Rects are the DS top screen's then the DS bottom screen's, as given to the renderer (swapped, the top one is the layout's bottom rect), and
 * a 16:9 frame is 342 DS pixels wide
 */
class ScreensDrawingTest {

    private val wideFrame = 342
    // Sides of a 342 wide frame's half, as u coordinates: (342 - 256) / 2 / 342
    private val narrowLeftU = 43f / 342f

    private val wideTopSlot = Rect(0, 0, 640, 360)
    private val bottomSlot = Rect(640, 0, 320, 240)
    // The middle 256 of 342 columns of the top slot
    private val wideTopSlotNative = Rect(80, 0, 480, 360)

    @Test
    fun notSwappedTheTopScreenIsWideAndTheBottomOneNarrow() {
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, wideFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f)

        assertScreen(drawing.topScreen, wideTopSlot, wideTopSlotNative, 0f, 1f)
        assertScreen(drawing.bottomScreen, bottomSlot, bottomSlot, narrowLeftU, 1f - narrowLeftU)
        assertEquals(bottomSlot, drawing.touchScreenRect)
    }

    @Test
    fun notSwappedAStretchedBottomScreenIsUsedAsItIs() {
        // Made 16:9 by the player in the editor
        val stretchedBottomSlot = Rect(640, 0, 320, 180)
        val drawing = getScreensDrawing(wideTopSlot, stretchedBottomSlot, wideFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f)

        assertScreen(drawing.bottomScreen, stretchedBottomSlot, stretchedBottomSlot, narrowLeftU, 1f - narrowLeftU)
        assertEquals(stretchedBottomSlot, drawing.touchScreenRect)
    }

    @Test
    fun swappedTheBottomScreenIsWideAndTouchesGoToItsMiddle() {
        val drawing = getScreensDrawing(bottomSlot, wideTopSlot, wideFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f)

        assertScreen(drawing.bottomScreen, wideTopSlot, wideTopSlotNative, 0f, 1f)
        assertScreen(drawing.topScreen, bottomSlot, bottomSlot, narrowLeftU, 1f - narrowLeftU)
        assertEquals(wideTopSlotNative, drawing.touchScreenRect)
    }

    @Test
    fun swappedInAWideLayoutTheTopScreenKeeps4By3InAStretchedRect() {
        val stretchedBottomSlot = Rect(640, 0, 320, 180)
        val drawing = getScreensDrawing(stretchedBottomSlot, wideTopSlot, wideFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f)

        assertScreen(drawing.topScreen, Rect(680, 0, 240, 180), Rect(680, 0, 240, 180), narrowLeftU, 1f - narrowLeftU)
    }

    @Test
    fun swappedInAWideLayoutTheTopScreenKeeps4By3InATallRect() {
        val tallBottomSlot = Rect(0, 0, 300, 485)
        val drawing = getScreensDrawing(tallBottomSlot, wideTopSlot, wideFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f)

        assertEquals(Rect(0, 130, 300, 225), drawing.topScreen?.drawRect)
    }

    @Test
    fun swappedWithANarrowFrameTheBottomScreenIsFittedAt4By3() {
        // Widescreen off, or a frame from before the width changed
        val drawing = getScreensDrawing(bottomSlot, wideTopSlot, 256, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f)

        assertScreen(drawing.bottomScreen, Rect(80, 0, 480, 360), Rect(80, 0, 480, 360), 0f, 1f)
        assertScreen(drawing.topScreen, bottomSlot, bottomSlot, 0f, 1f)
        assertEquals(Rect(80, 0, 480, 360), drawing.touchScreenRect)
    }

    @Test
    fun swappedInALayoutThatIsNotWideTheBottomScreenGrowsUpToTheOtherScreen() {
        // A 4:3 top slot right beside the bottom slot can't grow sideways, so the wide picture gets shorter instead
        val topSlot = Rect(0, 0, 480, 360)
        val besideSlot = Rect(480, 0, 320, 240)
        val drawing = getScreensDrawing(besideSlot, topSlot, wideFrame, isWideLayout = false, areScreensSwapped = true, surfaceWidth = 1000f)

        assertScreen(drawing.bottomScreen, Rect(0, 46, 480, 269), Rect(60, 46, 360, 269), 0f, 1f)
        assertScreen(drawing.topScreen, besideSlot, besideSlot, narrowLeftU, 1f - narrowLeftU)
        assertEquals(Rect(60, 46, 360, 269), drawing.touchScreenRect)
    }

    @Test
    fun missingScreensAreLeftOut() {
        // A screen on an external display, say
        val drawing = getScreensDrawing(null, wideTopSlot, wideFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f)

        assertNull(drawing.topScreen)
        assertEquals(wideTopSlotNative, drawing.touchScreenRect)
        assertNull(getScreensDrawing(wideTopSlot, null, wideFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f).touchScreenRect)
    }

    private fun assertScreen(screen: ScreenDrawing?, drawRect: Rect, nativeRect: Rect, leftU: Float, rightU: Float) {
        assertEquals(drawRect, screen?.drawRect)
        assertEquals(nativeRect, screen?.nativeRect)
        assertEquals(leftU, screen!!.leftU, 0.0001f)
        assertEquals(rightU, screen.rightU, 0.0001f)
    }
}
