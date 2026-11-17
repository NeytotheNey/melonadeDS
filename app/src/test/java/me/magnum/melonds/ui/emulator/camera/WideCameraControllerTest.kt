package me.magnum.melonds.ui.emulator.camera

import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.camera.GameScene
import me.magnum.melonds.domain.model.camera.WideCameraRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class WideCameraControllerTest {

    // x, zoom and y, as melon.wide.camera() gets them
    private val sent = mutableListOf<Triple<Float, Float, Float>>()
    private val picked = mutableListOf<Pair<Int, Float>>()
    private val controller = WideCameraController(
        onZoomPicked = { session, zoom -> picked.add(session to zoom) },
        onCameraChanged = { x, zoom, y -> sent.add(Triple(x, zoom, y)) },
    )
    private val atRest = Triple(0f, 1f, 0f)
    private var now = 1_000_000_000L
    private val settings = WideCameraSettings(showWidth = 342, returnsToCenter = true)

    private fun frames(count: Int, wingLines: Int = 192, frameWidth: Int = 512, frameMargin: Int = 0) = (1..count).map {
        now += 16_666_667L
        controller.onFrame(now, frameWidth, frameMargin, wingLines)
    }.last()

    @Test
    fun offItGivesNoWindowAndTellsTheScriptsTheRestStateOnce() {
        assertNull(frames(30))
        // Once, on the first frame, so scripts never keep a recreated activity's old camera
        assertEquals(listOf(atRest), sent)
    }

    @Test
    fun switchingTheCameraOffDropsHeldInput() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30)
        controller.onSticks(1f, 0f, false)
        controller.setTouching(true)
        controller.setSettings(null)
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))

        // No stick and no touch left over: it settles back at rest
        val window = frames(120)!!
        assertEquals(0f, window.offset, 0.0001f)
    }

    @Test
    fun releasingHeldInputStopsAStuckTouchAndStick() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30)
        controller.onSticks(1f, 0f, false)
        controller.setTouching(true)
        controller.onKey(Input.LEFT, true)
        frames(10)
        controller.releaseHeldInput()

        // The stick no longer pans and nothing counts as moving, so the window stays where it got to
        val stopped = frames(1)!!.offset
        assertTrue(stopped > 0f)
        assertEquals(stopped, frames(120)!!.offset, 0.0001f)
    }

    @Test
    fun theStickPansAndTheScriptsHearOfIt() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30)
        controller.onSticks(1f, 0f, false)
        val window = frames(60)!!

        assertEquals(85f, window.offset, 0.01f)
        assertEquals(85f, sent.last().first, 0.06f)
        assertEquals(1f, sent.last().second, 0.0001f)
    }

    @Test
    fun theDpadBringsItBackAndTheLastWordIsTheMiddle() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30)
        controller.onSticks(1f, 0f, false)
        frames(60)
        controller.onSticks(0f, 0f, false)
        controller.onKey(Input.RIGHT, true)
        frames(600)

        assertEquals(atRest, sent.last())
    }

    @Test
    fun switchingItOffGoesBackToNoWindowAndTheMiddle() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30)
        controller.onZoomPress()
        frames(60)
        controller.setSettings(null)

        assertNull(frames(1))
        assertEquals(atRest, sent.last())
    }

    @Test
    fun itStartsAtTheRememberedZoomAndReportsPicks() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        assertEquals(1.25f, frames(60)!!.zoom, 0.0001f)
        // A settings change (a fold, say) keeps the zoom that's there
        controller.setState(WideCameraState(session = 1, settings = settings.copy(returnsToCenter = false), startZoom = 1f))
        assertEquals(1.25f, frames(30)!!.zoom, 0.0001f)

        controller.onZoomPress()
        assertEquals(512f, frames(60)!!.width, 0.01f)
        assertEquals(listOf(1 to 512f / 342f), picked)
    }

    @Test
    fun aRecreatedControllerStartsAtTheLatestPick() {
        // What the view model's state holds after a pick, replayed to the new activity's controller
        controller.setState(WideCameraState(session = 3, settings = settings, startZoom = 512f / 342f))
        assertEquals(512f, frames(60)!!.width, 0.01f)
    }

    @Test
    fun aPickComingBackDoesntDropHeldInput() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        frames(30)
        controller.onSticks(1f, 0f, false)
        frames(5)
        // The view model's state after the pick: same settings, a new start zoom
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        // Still panning to the edge, so the stick wasn't dropped
        assertEquals(85f, frames(120)!!.offset, 0.01f)
    }

    @Test
    fun aNewSessionStartsOverEvenWithoutTheOffStateBetween() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        frames(30)
        controller.onZoomPress()
        controller.onSticks(1f, 0f, false)
        frames(60)
        // The next ROM's state, with the null settings between conflated away
        controller.setState(WideCameraState(session = 2, settings = settings, startZoom = 1f))
        val window = frames(60)!!
        assertEquals(1f, window.zoom, 0.0001f)
        assertEquals(0f, window.offset, 0.0001f)

        controller.onZoomPress()
        frames(30)
        assertEquals(listOf(1 to 1.25f, 2 to 1.25f), picked)
    }

    @Test
    fun aPressBeforeTheWideFrameWaitsForIt() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        controller.onZoomPress()
        // Frames from before the wide one (at launch, or a width change on its way)
        assertNull(frames(10, frameWidth = 342))
        assertEquals(1.25f, frames(60)!!.zoom, 0.0001f)
    }

    @Test
    fun aPressWhileOffIsDropped() {
        controller.onZoomPress()
        frames(5)
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        assertEquals(1f, frames(60)!!.zoom, 0.0001f)
        assertTrue(picked.isEmpty())
    }

    @Test
    fun theStickPansDownIntoTheMarginAndTheScriptsHearOfIt() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30, frameMargin = 48)
        controller.onSticks(0f, 1f, false)
        val window = frames(60, frameMargin = 48)!!

        // At 1x the window's 192 rows can go 48 rows down, to the margin's bottom
        assertEquals(48f, window.offsetY, 0.01f)
        assertEquals(0f, window.offset, 0.0001f)
        assertEquals(48f, sent.last().third, 0.06f)
    }

    @Test
    fun aVerticalDragMovesThePictureWithTheFinger() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30, frameMargin = 48)
        controller.setTouching(true)
        // A tenth of the picture's height down, so the window goes up by 19.2 of its 192 rows
        controller.onTouchMove(0f, 1f, 0.1f)
        val window = frames(1, frameMargin = 48)!!

        assertEquals(-19.2f, window.offsetY, 0.01f)
    }

    @Test
    fun withoutAMarginTheStickDownDoesNothing() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        frames(30)
        controller.onSticks(0f, 1f, false)
        val window = frames(60)!!

        assertEquals(0f, window.y, 0f)
        assertEquals(192f, window.height, 0f)
        assertEquals(atRest, sent.last())
    }

    @Test
    fun aScriptHoldingNativeKeepsTheMiddle() {
        controller.setSettings(WideCameraSettings(showWidth = 342, returnsToCenter = true))
        controller.setScriptRequest(WideCameraRequest(holdNative = true))
        repeat(4) { controller.onZoomPress() }
        val window = frames(60)!!

        assertEquals(1f, window.zoom, 0.0001f)
        assertEquals(listOf(atRest), sent)
    }

    // A known Pokemon game: the scene the native field read would give, and how often the controller asked
    private var scene = GameScene.FIELD
    private var sceneAsks = 0
    private val knownGameController = WideCameraController(
        onCameraChanged = { _, _, _ -> },
        gameScene = { sceneAsks++; scene },
    )

    private fun knownGameFrames(count: Int, wingLines: Int = 192) = (1..count).map {
        now += 16_666_667L
        knownGameController.onFrame(now, 512, 0, wingLines)
    }.last()

    @Test
    fun aKnownGamesTextBoxPullsInAndTheZoomComesBackAfter() {
        knownGameController.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        assertEquals(1.25f, knownGameFrames(120)!!.zoom, 0f)
        scene = GameScene.TEXT
        assertEquals(1f, knownGameFrames(30)!!.zoom, 0f)
        scene = GameScene.FIELD
        // Still in until 0.5 s after the text box
        assertEquals(1f, knownGameFrames(27)!!.zoom, 0f)
        assertEquals(1.25f, knownGameFrames(60)!!.zoom, 0f)
    }

    @Test
    fun theTextSettingOffLeavesTheZoomOut() {
        knownGameController.setState(WideCameraState(session = 1, settings = settings.copy(zoomsInOnText = false), startZoom = 1.25f))
        knownGameFrames(120)
        scene = GameScene.TEXT
        assertEquals(1.25f, knownGameFrames(60)!!.zoom, 0f)
        // A battle still pulls in
        scene = GameScene.BATTLE
        assertEquals(1f, knownGameFrames(60)!!.zoom, 0f)
    }

    @Test
    fun theSceneDecidesInsteadOfTheWingLinesAndAFadeHolds() {
        knownGameController.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        knownGameFrames(120)
        assertEquals(1.25f, knownGameFrames(60, wingLines = 0)!!.zoom, 0f)
        scene = GameScene.FADE
        assertEquals(1.25f, knownGameFrames(60, wingLines = 0)!!.zoom, 0f)
    }

    @Test
    fun swappedScreensAndAScriptsHoldStillWinInAKnownGame() {
        knownGameController.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        knownGameFrames(120)
        knownGameController.setScreensSwapped(true)
        assertEquals(1f, knownGameFrames(1)!!.zoom, 0f)
        knownGameController.setScreensSwapped(false)
        knownGameController.setScriptRequest(WideCameraRequest(holdNative = true))
        assertEquals(1f, knownGameFrames(120)!!.zoom, 0f)
    }

    @Test
    fun theSceneIsOnlyAskedForWhileTheCameraIsOn() {
        knownGameFrames(30)
        assertEquals(0, sceneAsks)
        knownGameController.setSettings(settings)
        knownGameFrames(30)
        assertEquals(30, sceneAsks)
    }

    @Test
    fun anUnknownGameKeepsTheWingRule() {
        // The default scene provider: no game state, so no wings snaps to 1x at once
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        assertEquals(1.25f, frames(120)!!.zoom, 0.0001f)
        assertEquals(1f, frames(1, wingLines = 0)!!.zoom, 0f)
    }
}
