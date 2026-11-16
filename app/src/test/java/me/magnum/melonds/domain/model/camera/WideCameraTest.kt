package me.magnum.melonds.domain.model.camera

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class WideCameraTest {

    // 16:9 shown, 2x rendered
    private val frame = WideCameraFrame(showWidth = 342, frameWidth = 512, topWingLines = 192, areScreensSwapped = false)
    private val centered = WideWindow.centered(342, 512)

    // Past the wait after the start (RESUME_SECONDS), with nothing pressed
    private fun readyCamera(): WideCamera {
        return WideCamera().apply {
            update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.1f)
            update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.1f)
            update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.1f)
        }
    }

    // Long enough for any easing to land
    private fun settle(camera: WideCamera, input: WideCameraInput = WideCameraInput(), request: WideCameraRequest = WideCameraRequest.NONE): WideWindow? {
        var window: WideWindow? = null
        repeat(60) { window = camera.update(frame, input, request, 0.05f) }
        return window
    }

    @Test
    fun noWiderFrameMeansNoWindow() {
        val camera = WideCamera()
        assertNull(camera.update(frame.copy(frameWidth = 342), WideCameraInput(), WideCameraRequest.NONE, 0.016f))
        assertNull(camera.update(frame.copy(showWidth = 256, frameWidth = 512), WideCameraInput(), WideCameraRequest.NONE, 0.016f))
    }

    @Test
    fun atRestTheWindowIsTheMiddleAtTheShowWidth() {
        val window = settle(readyCamera())!!
        assertEquals(centered, window)
        assertEquals(85f, window.x)
        assertEquals(0f, window.offset)
        assertEquals(1f, window.zoom)
    }

    @Test
    fun theZoomKeyCyclesThroughTheStepsAndBackTo1x() {
        assertEquals(1.25f, WideCamera.cycleZoom(1f, 1.5f), 1e-5f)
        assertEquals(1.5f, WideCamera.cycleZoom(1.25f, 1.5f), 1e-5f)
        assertEquals(1f, WideCamera.cycleZoom(1.5f, 1.5f), 1e-5f)
        // A range that doesn't end on a step has the widest view as its last one
        val maxZoom = 512 / 342f
        assertEquals(maxZoom, WideCamera.cycleZoom(1.25f, maxZoom), 1e-5f)
        assertEquals(1f, WideCamera.cycleZoom(maxZoom, maxZoom), 1e-5f)
        // From a pinched zoom, the next step above it
        assertEquals(1.25f, WideCamera.cycleZoom(1.1f, maxZoom), 1e-5f)
        assertEquals(maxZoom, WideCamera.cycleZoom(1.4f, maxZoom), 1e-5f)
    }

    @Test
    fun eachPressShowsTheNextStepAndIsReportedOnce() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        assertEquals(1.25f, settle(camera)!!.zoom, 1e-4f)
        assertEquals(1.25f, camera.takePickedZoom()!!, 1e-5f)
        assertNull(camera.takePickedZoom())

        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        assertEquals(512f, settle(camera)!!.width, 0.01f)
        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        assertEquals(1f, settle(camera)!!.zoom, 1e-4f)
    }

    @Test
    fun aPressIsLeftForTheNextFrameOnlyWhenThisOneCantApplyIt() {
        val camera = readyCamera()
        val input = WideCameraInput(zoomPresses = 1)
        assertNull(camera.update(frame.copy(frameWidth = 342), input, WideCameraRequest.NONE, 0.016f))
        assertEquals(1, input.zoomPresses)

        camera.update(frame, input, WideCameraRequest.NONE, 0.016f)
        assertEquals(0, input.zoomPresses)

        // A script's zoom drops it instead
        input.zoomPresses = 1
        camera.update(frame, input, WideCameraRequest(zoom = 1f), 0.016f)
        assertEquals(0, input.zoomPresses)
    }

    @Test
    fun aPinchThenAPressGoesToTheStepAbove() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(pinchScale = 1 / 1.1f, isTouching = true), WideCameraRequest.NONE, 0.016f)
        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        assertEquals(1.25f, settle(camera)!!.zoom, 1e-4f)
    }

    @Test
    fun aPressDuringAHoldShowsOnceTheHoldEndsAndAScriptsZoomIgnoresIt() {
        val camera = readyCamera()
        val menu = frame.copy(topWingLines = 0)
        assertEquals(centered, camera.update(menu, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f))
        assertEquals(1.25f, camera.takePickedZoom()!!, 1e-5f)
        assertEquals(1.25f, settle(camera)!!.zoom, 1e-4f)

        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest(zoom = 1f), 0.016f)
        assertNull(camera.takePickedZoom())
    }

    @Test
    fun theRememberedZoomIsWhereItStartsEvenAfterNarrowFrames() {
        val camera = WideCamera()
        camera.setPlayerZoom(1.25f)
        // Frames from before the wide one, at launch
        assertNull(camera.update(frame.copy(frameWidth = 342), WideCameraInput(), WideCameraRequest.NONE, 0.016f))
        assertEquals(1.25f, settle(camera)!!.zoom, 1e-4f)
        // A remembered zoom past this game's range is clamped
        camera.setPlayerZoom(3f)
        assertEquals(512f, settle(camera)!!.width, 0.01f)
    }

    @Test
    fun zoomingOutWidensTheWindowUpToTheFrame() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        val window = settle(camera)!!
        assertEquals(0f, window.x, 0.01f)
        assertEquals(512f, window.width, 0.01f)
    }

    @Test
    fun theStickPansAndStopsAtTheEdge() {
        val camera = readyCamera()
        val window = settle(camera, WideCameraInput(stickX = 1f))!!
        // Clamped to the frame's right edge
        assertEquals(512f - 342f, window.x, 0.01f)
        assertEquals(85f, window.offset, 0.01f)

        val left = settle(camera, WideCameraInput(stickX = -1f))!!
        assertEquals(0f, left.x, 0.01f)
    }

    @Test
    fun theSticksDeadZoneDoesNothing() {
        val camera = readyCamera()
        assertEquals(centered, settle(camera, WideCameraInput(stickX = WideCamera.STICK_DEAD_ZONE)))
    }

    @Test
    fun panningEasesInsteadOfJumping() {
        val camera = readyCamera()
        val window = camera.update(frame, WideCameraInput(stickX = 1f), WideCameraRequest.NONE, 0.016f)!!
        val target = WideCamera.PAN_SPEED * 0.016f
        assertTrue(window.offset > 0f && window.offset < target)
    }

    @Test
    fun zoomingInClampsThePan() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        settle(camera)
        // At 1.25x the window is 427.5 wide, so it can only sit 42.25 off center
        val window = settle(camera, WideCameraInput(stickX = 1f))!!
        assertEquals(42.25f, window.offset, 0.01f)
        assertEquals(512f, window.x + window.width, 0.01f)
    }

    @Test
    fun aDragMovesThePictureWithTheFinger() {
        val camera = readyCamera()
        // A tenth of the screen's width to the right moves the window a tenth of its width to the left, at once
        val window = camera.update(frame, WideCameraInput(dragFraction = 0.1f, isTouching = true), WideCameraRequest.NONE, 0.016f)!!
        assertEquals(-34.2f, window.offset, 0.01f)
    }

    @Test
    fun aPinchZoomsAtOnce() {
        val camera = readyCamera()
        val window = camera.update(frame, WideCameraInput(pinchScale = 0.8f, isTouching = true), WideCameraRequest.NONE, 0.016f)!!
        assertEquals(1.25f, window.zoom, 1e-4f)
    }

    @Test
    fun movingWithTheStickIdleEasesBackToTheMiddle() {
        val camera = readyCamera()
        settle(camera, WideCameraInput(stickX = 1f))
        val window = settle(camera, WideCameraInput(isPlayerMoving = true))!!
        assertEquals(0f, window.offset, 0.5f)
    }

    @Test
    fun withoutReturnToCenterMovingKeepsThePan() {
        val camera = readyCamera().apply { returnsToCenter = false }
        settle(camera, WideCameraInput(stickX = 1f))
        val window = settle(camera, WideCameraInput(isPlayerMoving = true))!!
        assertEquals(85f, window.offset, 0.01f)
    }

    @Test
    fun standingStillKeepsThePan() {
        val camera = readyCamera()
        settle(camera, WideCameraInput(stickX = 1f))
        assertEquals(85f, settle(camera)!!.offset, 0.01f)
    }

    @Test
    fun noWingsSnapToTheMiddleAt1xAndTheZoomComesBackLater() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        settle(camera, WideCameraInput(stickX = 1f))

        val menu = frame.copy(topWingLines = 0)
        assertEquals(centered, camera.update(menu, WideCameraInput(), WideCameraRequest.NONE, 0.016f))
        // Still the middle right after the wings come back
        assertEquals(centered, camera.update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.1f))

        // Then the player's zoom again, in the middle
        val window = settle(camera)!!
        assertEquals(1.25f, window.zoom, 1e-4f)
        assertEquals(0f, window.offset, 0.01f)
    }

    @Test
    fun swappedScreensAndHoldNativeKeep1x() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        settle(camera)

        assertEquals(centered, camera.update(frame.copy(areScreensSwapped = true), WideCameraInput(stickX = 1f), WideCameraRequest.NONE, 0.016f))
        assertEquals(centered, settle(camera, request = WideCameraRequest(zoom = 1.4f, holdNative = true)))
    }

    @Test
    fun aDualScreenGameWithHeldWingsZoomsAndPans() {
        // The core counts the wing hold's held lines, so a dual-screen game's top screen has wings on every frame, if not always all 192
        val camera = WideCamera()
        var window: WideWindow? = null
        repeat(120) {
            val lines = if (it % 2 == 0) 192 else 150
            window = camera.update(frame.copy(topWingLines = lines), WideCameraInput(stickX = 1f, zoomPresses = if (it == 30) 1 else 0), WideCameraRequest.NONE, 0.016f)
        }
        assertEquals(1.25f, window!!.zoom, 1e-4f)
        assertEquals(42.25f, window.offset, 0.01f)
    }

    @Test
    fun framesThatAlternateWithNoWingsNeverLeave1x() {
        // What a dual-screen game would look like without the held lines counted (or one flashing its 3D): it stays at 1x
        val camera = WideCamera()
        repeat(120) {
            val lines = if (it % 2 == 0) 192 else 0
            assertEquals(centered, camera.update(frame.copy(topWingLines = lines), WideCameraInput(stickX = 1f, zoomPresses = 1), WideCameraRequest.NONE, 0.016f))
        }
    }

    @Test
    fun aWindowAtRestIsTheSameObject() {
        val camera = readyCamera()
        val first = settle(camera)
        assertTrue(first === camera.update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.016f))
    }

    @Test
    fun theZoomLandsCloseToItsTarget() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        var window = camera.update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.016f)!!
        while (window.zoom != 1.25f) {
            // Never stuck more than a thousandth away
            assertTrue(1.25f - window.zoom >= 0.001f - 1e-6f)
            window = camera.update(frame, WideCameraInput(), WideCameraRequest.NONE, 0.016f)!!
        }
    }

    @Test
    fun aScriptsZoomAndPanReplaceThePlayersAndAreClamped() {
        val camera = readyCamera()
        val window = settle(camera, WideCameraInput(zoomPresses = 1, stickX = -1f), WideCameraRequest(zoom = 3f, pan = 40f))!!
        // The widest view leaves no room to pan
        assertEquals(512f, window.width, 0.01f)
        assertEquals(0f, window.offset, 0.01f)

        val panned = settle(camera, request = WideCameraRequest(zoom = 1f, pan = 40f))!!
        assertEquals(1f, panned.zoom, 1e-4f)
        assertEquals(40f, panned.offset, 0.01f)
    }

    @Test
    fun handingControlBackKeepsWhereTheScriptLeftIt() {
        val camera = readyCamera()
        settle(camera, request = WideCameraRequest(zoom = 1.25f, pan = 20f))
        val window = settle(camera)!!
        assertEquals(1.25f, window.zoom, 1e-4f)
        assertEquals(20f, window.offset, 0.01f)
    }

    @Test
    fun aLongGapCountsAsAShortOne() {
        val camera = readyCamera()
        val window = camera.update(frame, WideCameraInput(stickX = 1f, isTouching = true), WideCameraRequest.NONE, 5f)!!
        assertEquals(WideCamera.PAN_SPEED * WideCamera.MAX_DELTA_SECONDS, window.offset, 0.01f)
    }

    // Stage 2: the same frame with 48 rows of margin above and below the top screen, 288 rows in all (what 342 shown from 512 gets)
    private val tallFrame = frame.copy(frameMargin = 48)
    private val tallCentered = WideWindow.centered(342, 512, 288)

    private fun settleTall(camera: WideCamera, input: WideCameraInput = WideCameraInput(), request: WideCameraRequest = WideCameraRequest.NONE): WideWindow? {
        var window: WideWindow? = null
        repeat(60) { window = camera.update(tallFrame, input, request, 0.05f) }
        return window
    }

    @Test
    fun withoutAMarginTheWindowKeepsTheScreensRowsAtEveryZoom() {
        val camera = readyCamera()
        camera.update(frame, WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        val window = settle(camera, WideCameraInput(stickY = 1f))!!

        assertEquals(512f, window.width, 0.01f)
        // Exactly the 192 rows, so the screen letterboxes as in stage 1
        assertEquals(192, window.frameHeight)
        assertEquals(0f, window.y, 0f)
        assertEquals(192f, window.height, 0f)
        assertEquals(0f, window.offsetY, 0f)
    }

    @Test
    fun atRestTheTallWindowIsTheMiddleRows() {
        val camera = readyCamera()
        val window = settleTall(camera)!!

        assertEquals(tallCentered, window)
        assertEquals(48f, window.y, 0f)
        assertEquals(192f, window.height, 0f)
        assertEquals(48, window.margin)
    }

    @Test
    fun zoomingOutGrowsTheWindowsRowsIntoTheMargin() {
        val camera = readyCamera()
        camera.update(tallFrame, WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        val window = settleTall(camera)!!

        assertEquals(512f, window.width, 0.01f)
        // 192 * 512 / 342 rows, under the frame's 288, centered
        assertEquals(192f * 512f / 342f, window.height, 0.01f)
        assertEquals(0f, window.offsetY, 0.01f)
        assertTrue(window.y >= 0f && window.y + window.height <= 288f)
    }

    @Test
    fun aMarginTooSmallForTheZoomStopsAtTheFrame() {
        val camera = readyCamera()
        camera.update(frame.copy(frameMargin = 20), WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        var window: WideWindow? = null
        repeat(60) { window = camera.update(frame.copy(frameMargin = 20), WideCameraInput(), WideCameraRequest.NONE, 0.05f) }

        // All 232 rows, and the rest is letterboxed
        assertEquals(0f, window!!.y, 0f)
        assertEquals(232f, window!!.height, 0f)
    }

    @Test
    fun theStickPansVerticallyAndStopsAtTheMarginsEdge() {
        val camera = readyCamera()
        val down = settleTall(camera, WideCameraInput(stickY = 1f))!!
        assertEquals(48f, down.offsetY, 0.01f)
        assertEquals(288f, down.y + down.height, 0.01f)
        // The other axis stays put
        assertEquals(0f, down.offset, 0.0001f)

        val up = settleTall(camera, WideCameraInput(stickY = -1f))!!
        assertEquals(-48f, up.offsetY, 0.01f)
        assertEquals(0f, up.y, 0.01f)
    }

    @Test
    fun zoomingOutLeavesLessRoomToPanVertically() {
        val camera = readyCamera()
        settleTall(camera, WideCameraInput(stickY = 1f))
        camera.update(tallFrame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        val window = settleTall(camera, WideCameraInput(stickY = 1f))!!

        // At 1.25x the window is 240 rows, so it can go (288 - 240) / 2 down
        assertEquals(1.25f, window.zoom, 1e-4f)
        assertEquals(24f, window.offsetY, 0.01f)
    }

    @Test
    fun aVerticalDragMovesThePictureWithTheFinger() {
        val camera = readyCamera()
        camera.update(tallFrame, WideCameraInput(), WideCameraRequest.NONE, 0.016f)
        // A tenth of the picture's height down: the window goes up a tenth of its 192 rows
        val window = camera.update(tallFrame, WideCameraInput(dragFractionY = 0.1f, isTouching = true), WideCameraRequest.NONE, 0.016f)!!

        assertEquals(-19.2f, window.offsetY, 0.01f)
    }

    @Test
    fun movingEasesTheVerticalPanBackToTheMiddleToo() {
        val camera = readyCamera()
        settleTall(camera, WideCameraInput(stickX = 1f, stickY = 1f))
        val window = settleTall(camera, WideCameraInput(isPlayerMoving = true))!!

        assertEquals(0f, window.offset, 0.5f)
        assertEquals(0f, window.offsetY, 0.5f)
    }

    @Test
    fun aHoldCentersBothAxes() {
        val camera = readyCamera()
        settleTall(camera, WideCameraInput(stickY = 1f))
        val window = camera.update(tallFrame.copy(topWingLines = 0), WideCameraInput(stickY = 1f), WideCameraRequest.NONE, 0.016f)

        assertEquals(tallCentered, window)
    }

    @Test
    fun aScriptsVerticalPanIsClampedAndLeavesXToThePlayer() {
        val camera = readyCamera()
        val window = settleTall(camera, WideCameraInput(stickX = 1f), WideCameraRequest(panY = 100f))!!

        assertEquals(48f, window.offsetY, 0.01f)
        // pan(x) wasn't asked for, so the stick still has x
        assertEquals(85f, window.offset, 0.01f)

        // And the other way round
        val xOnly = settleTall(camera, WideCameraInput(stickY = -1f), WideCameraRequest(pan = -20f))!!
        assertEquals(-20f, xOnly.offset, 0.01f)
        assertEquals(-48f, xOnly.offsetY, 0.01f)
    }
}
