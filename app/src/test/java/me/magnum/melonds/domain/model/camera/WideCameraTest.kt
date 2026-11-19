package me.magnum.melonds.domain.model.camera

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.exp
import kotlin.math.pow
import kotlin.math.roundToInt

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

    // Known games (a GameScene from the native field read)

    // [seconds] of 60 fps frames showing [scene], the last window
    private fun play(
        camera: WideCamera,
        scene: Int,
        seconds: Float,
        input: WideCameraInput = WideCameraInput(),
        request: WideCameraRequest = WideCameraRequest.NONE,
        sceneFrame: WideCameraFrame = frame,
    ): WideWindow? {
        var window: WideWindow? = null
        repeat((seconds * 60f).roundToInt()) { window = camera.update(sceneFrame.copy(gameScene = scene), input, request, 1f / 60f) }
        return window
    }

    // A known game's camera at the player's zoom of 1.25 on the field, past the start's close-up and its return
    private fun fieldCamera(): WideCamera {
        return WideCamera().apply {
            setPlayerZoom(1.25f)
            play(this, GameScene.FIELD, 2f)
        }
    }

    @Test
    fun aKnownGameStartsAt1xAndZoomsOutHalfASecondAfterTheFieldShows() {
        val camera = WideCamera().apply { setPlayerZoom(1.25f) }
        assertEquals(1f, play(camera, GameScene.OFF, 1f)!!.zoom, 0f)
        // Not yet: the return waits 0.5 s
        assertEquals(1f, play(camera, GameScene.FIELD, 0.45f)!!.zoom, 0f)
        // Then eases out over 0.65 s
        val easing = play(camera, GameScene.FIELD, 0.4f)!!.zoom
        assertTrue(easing > 1f && easing < 1.25f)
        assertEquals(1.25f, play(camera, GameScene.FIELD, 0.5f)!!.zoom, 0f)
    }

    @Test
    fun theZoomOutFollowsALogSpaceSmoothstepOver650ms() {
        // The 16:9 shown, 512 rendered case from the Fold run: the widest step is 512 / 427 (about 1.2)
        val wide = WideCameraFrame(showWidth = 427, frameWidth = 512, topWingLines = 192, areScreensSwapped = false)
        val to = 512f / 427f
        val camera = WideCamera().apply { setPlayerZoom(to) }
        play(camera, GameScene.TEXT, 1f, sceneFrame = wide)
        // The text box closes; well inside the 0.5 s wait, then frame by frame
        play(camera, GameScene.FIELD, 0.45f, sceneFrame = wide)
        val zooms = (0..60).map { camera.update(wide.copy(gameScene = GameScene.FIELD), WideCameraInput(), WideCameraRequest.NONE, 1f / 60f)!!.zoom }
        // The move starts on the last frame still at 1x, with no step
        val start = zooms.lastIndexOf(1f)
        assertTrue(start in 1..5)
        for (k in listOf(10, 20, 30)) {
            val p = k / 60f / 0.65f
            assertEquals(to.pow(p * p * (3f - 2f * p)), zooms[start + k], 1e-4f)
        }
        // Slow at both ends, fastest in the middle, and not there before 0.65 s (39 frames)
        assertTrue(zooms[start + 1] - zooms[start] < zooms[start + 20] - zooms[start + 19])
        assertTrue(zooms[start + 37] - zooms[start + 36] < zooms[start + 20] - zooms[start + 19])
        assertTrue(zooms[start + 36] < to)
        assertEquals(to, zooms[start + 40], 0f)
    }

    @Test
    fun aTextBoxPullsInTo1xOverItsEase() {
        val camera = fieldCamera()
        val easing = play(camera, GameScene.TEXT, 0.2f)!!.zoom
        assertTrue(easing > 1f && easing < 1.25f)
        // In within 0.42 s
        val window = play(camera, GameScene.TEXT, 0.25f)!!
        assertEquals(1f, window.zoom, 0f)
        assertEquals(0f, window.offset, 0f)
    }

    @Test
    fun aTextBoxDoesNothingWithTheSettingOff() {
        val camera = fieldCamera().apply { zoomsInOnText = false }
        assertEquals(1.25f, play(camera, GameScene.TEXT, 1f)!!.zoom, 0f)
    }

    @Test
    fun battlesMenusAndOffTheFieldAlwaysPullIn() {
        for (scene in listOf(GameScene.BATTLE, GameScene.MENU, GameScene.OFF)) {
            val camera = fieldCamera().apply { zoomsInOnText = false }
            assertEquals(1f, play(camera, scene, 1f)!!.zoom, 0f)
        }
    }

    @Test
    fun aBattlePullsInAsFastAsATextBox() {
        // PokeDaisy's DS path calls a battle OFF, so it pulls in over the same 0.42 s
        val text = play(fieldCamera(), GameScene.TEXT, 0.3f)!!.zoom
        val battle = play(fieldCamera(), GameScene.BATTLE, 0.3f)!!.zoom
        assertEquals(text, battle, 0f)
        assertTrue(battle > 1f)
        assertEquals(1f, play(fieldCamera(), GameScene.BATTLE, 0.45f)!!.zoom, 0f)
    }

    // [frames] 60 fps frames of [scene], every zoom shown
    private fun zooms(camera: WideCamera, scene: Int, frames: Int): List<Float> {
        return (1..frames).map { camera.update(frame.copy(gameScene = scene), WideCameraInput(), WideCameraRequest.NONE, 1f / 60f)!!.zoom }
    }

    @Test
    fun anHgssBuildingExitDoesntPumpTheZoom() {
        // The desktop's SoulSilver exit (logs/expand-phase1-ss-exit-debug.log): 92 frames of fade, 46 of text (the wipe-in's field task
        // with the town's banner on BG3), then the field
        val camera = fieldCamera()
        val shown = zooms(camera, GameScene.FADE, 92) + zooms(camera, GameScene.TEXT, 46) + zooms(camera, GameScene.FIELD, 120)
        assertTrue(shown.all { it == 1.25f })
    }

    @Test
    fun aTextBoxRightAfterAFieldFramePullsIn() {
        val camera = fieldCamera()
        zooms(camera, GameScene.FADE, 92)
        zooms(camera, GameScene.FIELD, 1)
        assertEquals(1f, zooms(camera, GameScene.TEXT, 30).last(), 0f)
    }

    @Test
    fun textThatOutlastsTheWindowAfterAFadePullsIn() {
        val camera = fieldCamera()
        zooms(camera, GameScene.FADE, 30)
        // Held for the first 1.5 s, then pulled in over 0.42 s
        val held = zooms(camera, GameScene.TEXT, 89)
        assertTrue(held.all { it == 1.25f })
        assertEquals(1f, zooms(camera, GameScene.TEXT, 30).last(), 0f)
        // And a fade then text after a close-up keeps it in
        zooms(camera, GameScene.FADE, 30)
        assertTrue(zooms(camera, GameScene.TEXT, 46).all { it == 1f })
    }

    @Test
    fun aFadeHoldsTheZoomEitherWay() {
        // A door's fade on the field: no pull-in
        val camera = fieldCamera()
        assertEquals(1.25f, play(camera, GameScene.FADE, 2f)!!.zoom, 0f)

        // A fade after a menu screen: still in, until the field has shown for 0.5 s
        play(camera, GameScene.MENU, 1f)
        assertEquals(1f, play(camera, GameScene.FADE, 2f)!!.zoom, 0f)
        assertEquals(1f, play(camera, GameScene.FIELD, 0.45f)!!.zoom, 0f)
        assertEquals(1.25f, play(camera, GameScene.FIELD, 1f)!!.zoom, 0f)
    }

    @Test
    fun theZoomComesBackHalfASecondAfterTheTextBoxCloses() {
        val camera = fieldCamera()
        play(camera, GameScene.TEXT, 1f)
        assertEquals(1f, play(camera, GameScene.FIELD, 0.45f)!!.zoom, 0f)
        val easing = play(camera, GameScene.FIELD, 0.2f)!!.zoom
        assertTrue(easing > 1f && easing < 1.25f)
        assertEquals(1.25f, play(camera, GameScene.FIELD, 0.6f)!!.zoom, 0f)
        // Another text box within the half second keeps it in
        play(camera, GameScene.TEXT, 1f)
        play(camera, GameScene.FIELD, 0.3f)
        assertEquals(1f, play(camera, GameScene.TEXT, 0.5f)!!.zoom, 0f)
        assertEquals(1f, play(camera, GameScene.FIELD, 0.45f)!!.zoom, 0f)
    }

    @Test
    fun aKnownGameIgnoresTheWingLines() {
        val camera = fieldCamera()
        assertEquals(1.25f, play(camera, GameScene.FIELD, 1f, sceneFrame = frame.copy(topWingLines = 0))!!.zoom, 0f)
    }

    @Test
    fun unknownGamesKeepTheWingRule() {
        val camera = readyCamera()
        camera.setPlayerZoom(1.25f)
        assertEquals(1.25f, play(camera, GameScene.UNKNOWN, 1f)!!.zoom, 1e-4f)
        // No wings snaps to 1x at once, and comes back as it always did
        assertEquals(centered, camera.update(frame.copy(topWingLines = 0), WideCameraInput(), WideCameraRequest.NONE, 1f / 60f))
        assertEquals(centered, play(camera, GameScene.UNKNOWN, 0.2f))
        assertEquals(1.25f, play(camera, GameScene.UNKNOWN, 1f)!!.zoom, 1e-4f)
    }

    @Test
    fun swappedScreensAndHoldNativeStillWinInAKnownGame() {
        val swapped = fieldCamera()
        assertEquals(centered, swapped.update(frame.copy(areScreensSwapped = true, gameScene = GameScene.FIELD), WideCameraInput(), WideCameraRequest.NONE, 1f / 60f))
        val held = fieldCamera()
        assertEquals(centered, held.update(frame.copy(gameScene = GameScene.FIELD), WideCameraInput(), WideCameraRequest(holdNative = true), 1f / 60f))
        // Afterwards the player's zoom comes back like after any close-up
        assertEquals(1f, play(held, GameScene.FIELD, 0.45f)!!.zoom, 0f)
        assertEquals(1.25f, play(held, GameScene.FIELD, 1f)!!.zoom, 0f)
    }

    @Test
    fun pulledInTheLookAroundSpringsBackToTheMiddle() {
        val camera = fieldCamera()
        // At 1.25 the window is 427.5 wide, so it pans at most (512 - 427.5) / 2
        val panned = play(camera, GameScene.FIELD, 1f, WideCameraInput(stickX = 1f))!!
        assertEquals(42.25f, panned.offset, 1e-3f)
        // One frame in: a spring with a 0.12 s time constant, not a snap (the zoom's ease hasn't moved yet on its first frame)
        val firstFrame = play(camera, GameScene.TEXT, 1f / 60f, WideCameraInput(stickX = 1f))!!
        assertEquals(42.25f * exp(-(1f / 60f) / 0.12f), firstFrame.offset, 0.01f)
        // About 1/e of the way left after one more time constant
        val afterTau = play(camera, GameScene.TEXT, 0.12f, WideCameraInput(stickX = 1f))!!
        assertEquals(firstFrame.offset * exp(-1f), afterTau.offset, 0.5f)
        // And the stick does nothing while pulled in: it lands in the middle
        assertEquals(0f, play(camera, GameScene.TEXT, 1f, WideCameraInput(stickX = 1f))!!.offset, 0f)
    }

    @Test
    fun anUnknownFrameInAKnownGameKeepsTheLastScene() {
        // The read gives UNKNOWN for a frame after a reset or a state load; with no wing lines that frame would snap to 1x by the wing rule
        val reference = fieldCamera()
        val camera = fieldCamera()
        play(reference, GameScene.TEXT, 0.1f)
        play(camera, GameScene.TEXT, 0.1f)
        val referenceZooms = (1..30).map { reference.update(frame.copy(gameScene = GameScene.TEXT), WideCameraInput(), WideCameraRequest.NONE, 1f / 60f)!!.zoom }
        val zooms = (1..30).map {
            val scene = if (it == 1) GameScene.UNKNOWN else GameScene.TEXT
            val wingLines = if (it == 1) 0 else 192
            camera.update(frame.copy(gameScene = scene, topWingLines = wingLines), WideCameraInput(), WideCameraRequest.NONE, 1f / 60f)!!.zoom
        }
        // The same ease, frame for frame: no blip to 1x and no restart
        assertEquals(referenceZooms, zooms)
        assertTrue(zooms.first() > 1f)
    }

    // 768 rendered, so at 1x there's more room to pan than the player keep allows
    private val roomyFrame = WideCameraFrame(showWidth = 342, frameWidth = 768, topWingLines = 192, areScreensSwapped = false)

    @Test
    fun standingStillTheViewStaysAndWalkingSpringsItBack() {
        val camera = WideCamera()
        play(camera, GameScene.FIELD, 1f, sceneFrame = roomyFrame)
        play(camera, GameScene.FIELD, 0.3f, WideCameraInput(stickX = 1f), sceneFrame = roomyFrame)
        val panned = play(camera, GameScene.FIELD, 1f, sceneFrame = roomyFrame)!!.offset
        assertTrue(panned > 50f)
        // Standing with the stick let go: it stays
        assertEquals(panned, play(camera, GameScene.FIELD, 2f, sceneFrame = roomyFrame)!!.offset, 0f)
        // Walking: back to the middle with a 0.18 s time constant, so about 1/e of the way left after 0.18 s
        val afterTau = play(camera, GameScene.FIELD, 0.18f, WideCameraInput(isPlayerMoving = true), sceneFrame = roomyFrame)!!.offset
        assertEquals(panned * exp(-1f), afterTau, panned * 0.05f)
        assertEquals(0f, play(camera, GameScene.FIELD, 2f, WideCameraInput(isPlayerMoving = true), sceneFrame = roomyFrame)!!.offset, 0f)
    }

    @Test
    fun walkingWithTheStickFollowsItButKeepsThePlayerInside() {
        val camera = WideCamera()
        play(camera, GameScene.FIELD, 1f, sceneFrame = roomyFrame)
        val window = play(camera, GameScene.FIELD, 2f, WideCameraInput(stickX = 1f, stickY = 0f, isPlayerMoving = true), sceneFrame = roomyFrame)!!
        // The window is 342 wide, so the middle stays 28 inside its edge: 342 / 2 - 28
        assertEquals(143f, window.offset, 0.01f)
        // Half the stick past the dead zone, half as far
        val half = play(camera, GameScene.FIELD, 2f, WideCameraInput(stickX = 0.6f, isPlayerMoving = true), sceneFrame = roomyFrame)!!
        assertEquals(71.5f, half.offset, 0.01f)
    }

    @Test
    fun withoutReturnToCenterWalkingLeavesTheView() {
        val camera = WideCamera().apply { returnsToCenter = false }
        play(camera, GameScene.FIELD, 1f, sceneFrame = roomyFrame)
        play(camera, GameScene.FIELD, 0.3f, WideCameraInput(stickX = 1f), sceneFrame = roomyFrame)
        val panned = play(camera, GameScene.FIELD, 1f, sceneFrame = roomyFrame)!!.offset
        assertTrue(panned > 50f)
        assertEquals(panned, play(camera, GameScene.FIELD, 2f, WideCameraInput(isPlayerMoving = true), sceneFrame = roomyFrame)!!.offset, 0f)
    }

    @Test
    fun aScriptsZoomMovingEveryFrameStillGetsThereInAKnownGame() {
        val camera = fieldCamera()
        var window: WideWindow? = null
        for (i in 1..60) {
            window = camera.update(frame.copy(gameScene = GameScene.FIELD), WideCameraInput(), WideCameraRequest(zoom = 1.25f + i * 0.004f), 1f / 60f)
        }
        // Close behind it (the ease lags a moving target by about 0.12 s), not stuck where it started
        assertEquals(1.49f, window!!.zoom, 0.04f)
        // And pulled in, the scene still wins
        assertEquals(1f, play(camera, GameScene.MENU, 1f, request = WideCameraRequest(zoom = 1.4f))!!.zoom, 0f)
    }

    @Test
    fun aPinchStillFollowsTheFingersInAKnownGame() {
        val camera = fieldCamera()
        val window = camera.update(frame.copy(gameScene = GameScene.FIELD), WideCameraInput(pinchScale = 1.25f / 1.4f, isTouching = true), WideCameraRequest.NONE, 1f / 60f)!!
        assertEquals(1.4f, window.zoom, 1e-4f)
    }

    // The render width follows the player's zoom (WidescreenLaunch.getRenderWidth): the widest view is 512, but the core renders only as wide
    // as the zoom needs, so frames can be narrower than that. 342 shown at 1x renders at 342; 1.25x renders 428 wide with 25 rows of margin
    private val oneXFrame = frame.copy(frameWidth = 342, maxWidth = 512)
    private val stepFrame = frame.copy(frameWidth = 428, frameMargin = 25, maxWidth = 512)
    private val widestFrame = frame.copy(frameMargin = 48, maxWidth = 512)

    private fun settleOn(camera: WideCamera, sceneFrame: WideCameraFrame, input: WideCameraInput = WideCameraInput()): WideWindow? {
        var window: WideWindow? = null
        repeat(60) { window = camera.update(sceneFrame, input, WideCameraRequest.NONE, 0.05f) }
        return window
    }

    @Test
    fun theRenderZoomIsTheNextStepAtOrAboveTheZoom() {
        val maxZoom = 512 / 342f
        assertEquals(1f, WideCamera.getRenderZoom(1f, maxZoom), 0f)
        assertEquals(1.25f, WideCamera.getRenderZoom(1.1f, maxZoom), 0f)
        assertEquals(1.25f, WideCamera.getRenderZoom(1.25f, maxZoom), 0f)
        // A float error past a step isn't the next one
        assertEquals(1f, WideCamera.getRenderZoom(1.0001f, maxZoom), 0f)
        assertEquals(1.25f, WideCamera.getRenderZoom(1.2501f, maxZoom), 0f)
        // Capped at the widest view, and never under 1
        assertEquals(maxZoom, WideCamera.getRenderZoom(1.3f, maxZoom), 0f)
        assertEquals(maxZoom, WideCamera.getRenderZoom(3f, maxZoom), 0f)
        assertEquals(1f, WideCamera.getRenderZoom(0.5f, maxZoom), 0f)
    }

    @Test
    fun aFrameAtTheShownWidthGivesNoWindowButTheHotkeyStillZoomsOut() {
        val camera = WideCamera()
        settleOn(camera, oneXFrame)
        val input = WideCameraInput(zoomPresses = 1)
        // Drawn as without a camera, but the press counts, so the core can be asked for a wider frame
        assertNull(camera.update(oneXFrame, input, WideCameraRequest.NONE, 0.016f))
        assertEquals(0, input.zoomPresses)
        assertEquals(1.25f, camera.playerZoom, 0f)
        assertEquals(1.25f, camera.takePickedZoom()!!, 0f)
    }

    @Test
    fun theHotkeyZoomsOutPastTheFramesWidthToTheWidestView() {
        val camera = WideCamera()
        settleOn(camera, stepFrame)
        // From 1x on a frame that's only 1.25x wide: the range comes from the widest view, not the frame
        camera.update(stepFrame, WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        assertEquals(512f / 342f, camera.playerZoom, 1e-5f)
        // And round to 1x after it
        camera.update(stepFrame, WideCameraInput(zoomPresses = 1), WideCameraRequest.NONE, 0.016f)
        assertEquals(1f, camera.playerZoom, 0f)
    }

    @Test
    fun whileTheWiderFrameIsOnItsWayTheWindowShowsAllOfTheOneThereIs() {
        val camera = WideCamera()
        settleOn(camera, stepFrame, WideCameraInput(stickX = 1f, stickY = 1f))
        camera.update(stepFrame, WideCameraInput(zoomPresses = 2), WideCameraRequest.NONE, 0.016f)
        val pending = settleOn(camera, stepFrame, WideCameraInput(stickX = 1f, stickY = 1f))!!

        // The whole 428 columns and the rows that keep the pixel shape, inside the frame's 242
        assertEquals(0f, pending.x, 0f)
        assertEquals(428f, pending.width, 0.01f)
        assertEquals(192f * 428f / 342f, pending.height, 0.01f)
        assertTrue(pending.y >= 0f && pending.y + pending.height <= 242f)

        // The widest frame comes: the zoom eases on from the frame instead of jumping to the end
        val first = camera.update(widestFrame, WideCameraInput(), WideCameraRequest.NONE, 0.016f)!!
        assertTrue(first.zoom > 428f / 342f - 1e-4f && first.zoom < 1.45f)
        val window = settleOn(camera, widestFrame)!!
        assertEquals(512f, window.width, 0.01f)
        assertEquals(192f * 512f / 342f, window.height, 0.01f)
    }

    @Test
    fun aKnownGamesZoomOutAheadOfTheFrameStartsItsEaseOnceTheFrameComes() {
        val camera = WideCamera().apply { setPlayerZoom(512f / 342f) }
        // Off the field, then the field on a frame that's only 1.25x wide
        play(camera, GameScene.OFF, 1f, sceneFrame = stepFrame)
        assertEquals(428f / 342f, play(camera, GameScene.FIELD, 2f, sceneFrame = stepFrame)!!.zoom, 1e-4f)
        // The widest frame: a fresh 0.65 s ease from the frame's zoom, not a jump
        val first = play(camera, GameScene.FIELD, 1f / 60f, sceneFrame = widestFrame)!!.zoom
        assertTrue(first < 1.27f)
        assertEquals(512f / 342f, play(camera, GameScene.FIELD, 1f, sceneFrame = widestFrame)!!.zoom, 1e-4f)
    }

    @Test
    fun pullInsLeaveThePlayersZoomAlone() {
        // No wing lines in an unknown game
        val camera = WideCamera().apply { setPlayerZoom(1.25f) }
        settleOn(camera, stepFrame)
        assertEquals(1f, camera.update(stepFrame.copy(topWingLines = 0), WideCameraInput(), WideCameraRequest.NONE, 0.016f)!!.zoom, 0f)
        assertEquals(1.25f, camera.playerZoom, 0f)
        // Swapped screens and a script's hold
        camera.update(stepFrame.copy(areScreensSwapped = true), WideCameraInput(), WideCameraRequest.NONE, 0.016f)
        camera.update(stepFrame, WideCameraInput(), WideCameraRequest(holdNative = true), 0.016f)
        assertEquals(1.25f, camera.playerZoom, 0f)

        // A known game's text box, battle and menu
        val known = WideCamera().apply { setPlayerZoom(1.25f) }
        play(known, GameScene.FIELD, 2f, sceneFrame = stepFrame)
        for (scene in listOf(GameScene.TEXT, GameScene.BATTLE, GameScene.MENU, GameScene.OFF)) {
            assertEquals(1f, play(known, scene, 1f, sceneFrame = stepFrame)!!.zoom, 0f)
            assertEquals(1.25f, known.playerZoom, 0f)
        }
    }

    @Test
    fun aScriptsZoomIsThePlayersZoomToo() {
        // So a script's zoom-out gets a frame wide enough for it
        val camera = readyCamera()
        camera.update(stepFrame, WideCameraInput(), WideCameraRequest(zoom = 1.4f), 0.016f)
        assertEquals(1.4f, camera.playerZoom, 1e-5f)
    }

    @Test
    fun aKnownRangeClampsThePlayersZoomEvenWhileHeld() {
        val camera = WideCamera().apply { setPlayerZoom(2f) }
        // Held to 1x by a script and by a frame from before the wide one: the player's zoom is still kept inside the range
        camera.update(stepFrame, WideCameraInput(), WideCameraRequest(holdNative = true), 0.016f)
        assertEquals(512f / 342f, camera.playerZoom, 0f)
        assertEquals(1f, camera.shownZoom, 0f)
        camera.setPlayerZoom(2f)
        assertNull(camera.update(stepFrame.copy(frameWidth = 256), WideCameraInput(), WideCameraRequest.NONE, 0.016f))
        assertEquals(512f / 342f, camera.playerZoom, 0f)
    }
}
