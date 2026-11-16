package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.camera.WideWindow
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

    // The camera: 16:9 shown from a 512 wide frame
    private val cameraFrame = 512
    private val cameraCentered = WideWindow.centered(342, cameraFrame)

    @Test
    fun aCenteredCameraWindowLooksLikeAFrameAtTheShowWidth() {
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = cameraCentered)

        assertScreen(drawing.topScreen, wideTopSlot, wideTopSlotNative, 85f / 512f, 427f / 512f)
        // The bottom screen is still the frame's middle 256
        assertScreen(drawing.bottomScreen, bottomSlot, bottomSlot, 0.25f, 0.75f)
        val overlay = drawing.topScreen!!.overlay
        assertEquals(0f, overlay.leftU, 0.0001f)
        assertEquals(1f, overlay.rightU, 0.0001f)
        assertEquals(80.47f, overlay.left, 0.01f)
    }

    @Test
    fun aZoomedOutWindowIsLetterboxed() {
        val window = WideWindow(342, cameraFrame, 0f, 512f)
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = window)

        // 360 * 342 / 512 tall, with the middle 256 of 512 columns in the middle half
        assertScreen(drawing.topScreen, Rect(0, 60, 640, 240), Rect(160, 60, 320, 240), 0f, 1f)
        // The output cap still sizes the frame for 1x, so zooming doesn't change the internal scale
        assertEquals(wideTopSlotNative, drawing.topScreen!!.nativeRectAt1x)
        // Without a margin it's all 192 rows
        assertRows(drawing.topScreen, 0f, 192f)
    }

    // Stage 2: 48 rows of margin above and below, so the top screen's half is 288 rows
    private val tallCentered = WideWindow.centered(342, cameraFrame, 288)

    @Test
    fun aZoomedOutWindowWithAMarginFillsTheScreen() {
        val height = 192f * 512f / 342f
        val window = WideWindow(342, cameraFrame, 0f, 512f, 288, (288f - height) / 2f, height)
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = window)

        // The whole rect, with the middle 256 of 512 columns and 192 of the window's 287.4 rows in the middle
        assertScreen(drawing.topScreen, wideTopSlot, Rect(160, 60, 320, 240), 0f, 1f)
        assertRows(drawing.topScreen, (288f - height) / 2f, (288f + height) / 2f)
        val overlay = drawing.topScreen!!.overlay
        assertEquals(59.77f, overlay.top, 0.01f)
        assertEquals(300.23f, overlay.bottom, 0.01f)
        assertEquals(0f, overlay.topV, 0.0001f)
        assertEquals(1f, overlay.bottomV, 0.0001f)
        assertEquals(wideTopSlotNative, drawing.topScreen.nativeRectAt1x)
    }

    @Test
    fun atOneXWithAMarginItsTheSameRectsShowingTheMiddleRows() {
        val withMargin = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = tallCentered, frameMargin = 48)
        val without = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = cameraCentered)

        assertScreen(withMargin.topScreen, wideTopSlot, wideTopSlotNative, 85f / 512f, 427f / 512f)
        assertRows(withMargin.topScreen, 48f, 240f)
        assertEquals(without.topScreen!!.overlay, withMargin.topScreen!!.overlay)
        assertEquals(without.bottomScreen, withMargin.bottomScreen)
        assertRows(withMargin.bottomScreen, 0f, 192f)
    }

    @Test
    fun aWindowPannedUpCutsTheDrawingsAtThePicturesBottom() {
        val window = WideWindow(342, cameraFrame, 85f, 342f, 288, 0f, 192f)
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = window)

        // The top 48 rows are margin, so DS line 0 is a quarter of the way down and the screen runs past the picture
        assertScreen(drawing.topScreen, wideTopSlot, Rect(80, 90, 480, 360), 85f / 512f, 427f / 512f)
        assertRows(drawing.topScreen, 0f, 192f)
        val overlay = drawing.topScreen!!.overlay
        assertEquals(90f, overlay.top, 0.01f)
        assertEquals(360f, overlay.bottom, 0.01f)
        assertEquals(0f, overlay.topV, 0.0001f)
        assertEquals(0.75f, overlay.bottomV, 0.0001f)
    }

    @Test
    fun withoutAWindowTheMarginIsLeftOut() {
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, frameMargin = 48)
        val without = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f)

        assertRows(drawing.topScreen, 48f, 240f)
        assertRows(drawing.bottomScreen, 0f, 192f)
        assertEquals(without.topScreen!!.drawRect, drawing.topScreen!!.drawRect)
        assertRows(without.topScreen, 0f, 192f)

        // Swapped, the DS top screen is the narrow one and still shows its middle rows
        val swapped = getScreensDrawing(bottomSlot, wideTopSlot, cameraFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f, frameMargin = 48)
        assertRows(swapped.topScreen, 48f, 240f)
        assertRows(swapped.bottomScreen, 0f, 192f)
    }

    @Test
    fun swappedTheWindowLeavesTheBottomScreensRowsAlone() {
        val drawing = getScreensDrawing(bottomSlot, wideTopSlot, cameraFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f, wideWindow = tallCentered, frameMargin = 48)

        assertEquals(wideTopSlotNative, drawing.touchScreenRect)
        assertRows(drawing.bottomScreen, 0f, 192f)
        assertRows(drawing.topScreen, 48f, 240f)
    }

    @Test
    fun aPannedWindowCutsTheDrawingsToThePicture() {
        val window = WideWindow(342, cameraFrame, 0f, 342f)
        val drawing = getScreensDrawing(wideTopSlot, bottomSlot, cameraFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = window)

        val overlay = drawing.topScreen!!.overlay
        assertEquals(640f, overlay.right, 0.01f)
        assertEquals(0f, overlay.leftU, 0.0001f)
        // Columns 128 to 341 of the frame are the middle 256's first 214
        assertEquals(214f / 256f, overlay.rightU, 0.0001f)
        assertEquals(0f, drawing.topScreen.leftU, 0.0001f)
        assertEquals(342f / 512f, drawing.topScreen.rightU, 0.0001f)
    }

    @Test
    fun swappedTouchesFollowTheCenteredWindow() {
        val drawing = getScreensDrawing(bottomSlot, wideTopSlot, cameraFrame, isWideLayout = true, areScreensSwapped = true, surfaceWidth = 960f, wideWindow = cameraCentered)

        assertEquals(wideTopSlotNative, drawing.touchScreenRect)
    }

    @Test
    fun aWindowForAnotherFrameWidthIsLeftOut() {
        val withWindow = getScreensDrawing(wideTopSlot, bottomSlot, wideFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f, wideWindow = cameraCentered)
        val without = getScreensDrawing(wideTopSlot, bottomSlot, wideFrame, isWideLayout = true, areScreensSwapped = false, surfaceWidth = 960f)

        assertEquals(without, withWindow)
    }

    private fun assertRows(screen: ScreenDrawing?, topRow: Float, bottomRow: Float) {
        assertEquals(topRow, screen!!.topRow, 0.001f)
        assertEquals(bottomRow, screen.bottomRow, 0.001f)
    }

    private fun assertScreen(screen: ScreenDrawing?, drawRect: Rect, nativeRect: Rect, leftU: Float, rightU: Float) {
        assertEquals(drawRect, screen?.drawRect)
        assertEquals(nativeRect, screen?.nativeRect)
        assertEquals(leftU, screen!!.leftU, 0.0001f)
        assertEquals(rightU, screen.rightU, 0.0001f)
    }
}
