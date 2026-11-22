package me.magnum.melonds.ui.emulator.camera

import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.camera.GameScene
import me.magnum.melonds.domain.model.camera.WideCamera
import me.magnum.melonds.domain.model.camera.WideCameraRequest
import me.magnum.melonds.domain.model.camera.WideWindow
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class WideCameraControllerTest {

    // x, zoom and y, as melon.wide.camera() gets them
    private val sent = mutableListOf<Triple<Float, Float, Float>>()
    private val picked = mutableListOf<Pair<Int, Float>>()
    // The render zooms asked for, with their session
    private val renderZooms = mutableListOf<Pair<Int, Float>>()
    private val controller = WideCameraController(
        onZoomPicked = { session, zoom -> picked.add(session to zoom) },
        onCameraChanged = { x, zoom, y -> sent.add(Triple(x, zoom, y)) },
        onRenderZoomNeeded = { session, zoom -> renderZooms.add(session to zoom) },
    )
    private val atRest = Triple(0f, 1f, 0f)
    private var now = 1_000_000_000L
    private val settings = WideCameraSettings(showWidth = 342, returnsToCenter = true, maxWidth = 512)

    private fun frames(count: Int, wingLines: Int = 192, frameWidth: Int = 512, frameMargin: Int = 0, frameGuard: Int = 0) = (1..count).map {
        now += 16_666_667L
        controller.onFrame(now, frameWidth, frameMargin, wingLines, frameGuard)
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
        assertNull(frames(10, frameWidth = 256))
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

    // The render width follows the player's zoom: frames at 342 are the core at 1x, 428 with 25 rows at 1.25x, 512 with 48 at the widest

    @Test
    fun at1xItAsksForThe1xRenderOnceAndDrawsAsWithoutACamera() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        assertNull(frames(60, frameWidth = 342))
        assertEquals(listOf(1 to 1f), renderZooms)
        assertEquals(listOf(atRest), sent)
    }

    @Test
    fun itAsksForTheRememberedZoomsStep() {
        controller.setState(WideCameraState(session = 2, settings = settings, startZoom = 512f / 342f))
        frames(1)
        assertEquals(listOf(2 to 512f / 342f), renderZooms)
    }

    @Test
    fun aPressAt1xAsksForTheWiderFrameAndShowsItOnceItComes() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        frames(30, frameWidth = 342)
        controller.onZoomPress()
        // Asked for at once; until the frame comes it's drawn at 1x
        assertNull(frames(1, frameWidth = 342))
        assertEquals(listOf(1 to 1f, 1 to 1.25f), renderZooms)
        assertNull(frames(3, frameWidth = 342))
        assertEquals(1.25f, frames(60, frameWidth = 428, frameMargin = 25)!!.zoom, 0.0001f)

        // The next press goes past the frame's width to the widest view, shown whole frame until that one comes
        controller.onZoomPress()
        assertEquals(428f, frames(10, frameWidth = 428, frameMargin = 25)!!.width, 0.01f)
        assertEquals(1 to 512f / 342f, renderZooms.last())
        assertEquals(512f, frames(60, frameMargin = 48)!!.width, 0.01f)

        // Then back to 1x, which asks for the 1x render once the zoom has eased in
        controller.onZoomPress()
        frames(60, frameMargin = 48)
        assertEquals(1 to 1f, renderZooms.last())
        assertEquals(4, renderZooms.size)
    }

    @Test
    fun aPinchAsksForMoreRoomAtOnceButLessOnlyOnceTheFingersAreOff() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        frames(30, frameWidth = 342)
        controller.setTouching(true)
        // Out to 1.1, which renders for 1.25
        controller.onTouchMove(0f, 1f / 1.1f)
        frames(1, frameWidth = 342)
        assertEquals(1 to 1.25f, renderZooms.last())
        // Back in to 1x with the fingers still down: no resize yet
        controller.onTouchMove(0f, 1.1f)
        frames(5, frameWidth = 428, frameMargin = 25)
        assertEquals(1 to 1.25f, renderZooms.last())
        controller.setTouching(false)
        // And with them off, after the few frames a narrower one has to hold
        frames(WideCameraController.SHRINK_FRAMES - 1, frameWidth = 428, frameMargin = 25)
        assertEquals(1 to 1.25f, renderZooms.last())
        frames(1, frameWidth = 428, frameMargin = 25)
        assertEquals(listOf(1 to 1f, 1 to 1.25f, 1 to 1f), renderZooms)
    }

    @Test
    fun pullInsDontChangeTheRenderZoom() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        frames(60, frameWidth = 428, frameMargin = 25)
        // No wing lines, swapped screens and a script's hold all show 1x, but the player's zoom stays where it was
        assertEquals(1f, frames(30, wingLines = 0, frameWidth = 428, frameMargin = 25)!!.zoom, 0f)
        controller.setScreensSwapped(true)
        frames(30, frameWidth = 428, frameMargin = 25)
        controller.setScreensSwapped(false)
        controller.setScriptRequest(WideCameraRequest(holdNative = true))
        frames(30, frameWidth = 428, frameMargin = 25)
        assertEquals(listOf(1 to 1.25f), renderZooms)
    }

    @Test
    fun aKnownGamesPullInDoesntChangeTheRenderZoom() {
        val asked = mutableListOf<Float>()
        val known = WideCameraController(onCameraChanged = { _, _, _ -> }, gameScene = { scene }, onRenderZoomNeeded = { _, zoom -> asked.add(zoom) })
        known.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        fun play(count: Int) = (1..count).map { now += 16_666_667L; known.onFrame(now, 428, 25, 192) }.last()
        assertEquals(1.25f, play(120)!!.zoom, 0f)
        for (pullIn in listOf(GameScene.TEXT, GameScene.BATTLE, GameScene.MENU)) {
            scene = pullIn
            assertEquals(1f, play(60)!!.zoom, 0f)
        }
        assertEquals(listOf(1.25f), asked)
    }

    @Test
    fun aScriptsZoomOutAsksForTheRoomItNeeds() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        frames(30, frameWidth = 342)
        controller.setScriptRequest(WideCameraRequest(zoom = 1.4f))
        frames(1, frameWidth = 342)
        assertEquals(1 to 512f / 342f, renderZooms.last())
    }

    @Test
    fun aNewSessionOrShownWidthAsksAgain() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        frames(1, frameWidth = 428, frameMargin = 25)
        // A fold: the shown width changes, and the zoom is clamped to the new range
        controller.setSettings(settings.copy(showWidth = 426))
        frames(1)
        assertEquals(1 to 512f / 426f, renderZooms.last())
        controller.setState(WideCameraState(session = 2, settings = settings, startZoom = 1f))
        frames(1, frameWidth = 342)
        assertEquals(listOf(1 to 1.25f, 1 to 512f / 426f, 2 to 1f), renderZooms)
    }

    @Test
    fun aWrapTo1xEasesAllTheWayInBeforeTheNarrowerRenderIsAskedFor() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 512f / 342f))
        assertEquals(512f, frames(60, frameMargin = 48)!!.width, 0.01f)
        controller.onZoomPress()
        // Each frame's window zoom until the narrower render zoom is asked for
        val zooms = mutableListOf<Float>()
        while (renderZooms.size == 1 && zooms.size < 300) {
            zooms.add(frames(1, frameMargin = 48)?.zoom ?: 1f)
        }

        assertEquals(listOf(1 to 512f / 342f, 1 to 1f), renderZooms)
        // The ease ran on the wide frame: the zoom came down over several frames and was at 1x for the last few before the ask
        assertTrue(zooms.first() > 1.2f)
        assertTrue(zooms.size > 10)
        assertTrue(zooms.takeLast(WideCameraController.SHRINK_FRAMES).all { it <= 1.001f })
        assertTrue(zooms.zipWithNext().all { (a, b) -> b <= a + 1e-5f })
    }

    @Test
    fun aKnownGamesWrapTo1xEasesOverItsPullInBeforeTheNarrowerRender() {
        val asked = mutableListOf<Float>()
        val known = WideCameraController(onCameraChanged = { _, _, _ -> }, gameScene = { scene }, onRenderZoomNeeded = { _, zoom -> asked.add(zoom) })
        known.setState(WideCameraState(session = 1, settings = settings, startZoom = 512f / 342f))
        fun play(count: Int) = (1..count).map { now += 16_666_667L; known.onFrame(now, 512, 48, 192) }.last()
        assertEquals(512f, play(180)!!.width, 0.01f)
        known.onZoomPress()
        val zooms = mutableListOf<Float>()
        while (asked.size == 1 && zooms.size < 300) {
            zooms.add(play(1)?.zoom ?: 1f)
        }

        assertEquals(listOf(512f / 342f, 1f), asked)
        // The 0.42 s pull-in ease, then the frames a narrower one holds
        assertTrue(zooms.size >= (WideCamera.PULL_IN_SECONDS * 60f).toInt())
        assertTrue(zooms[zooms.size / 4] > 1.1f)
        assertTrue(zooms.takeLast(WideCameraController.SHRINK_FRAMES).all { it <= 1.001f })
    }

    // The sun's guard band: a frame 64 DS pixels wider on each side and 64 rows taller above and below than the shown one
    private val guard = 64

    @Test
    fun withAGuardTheCameraWorksOnTheShownFrame() {
        // The same moves on a guarded frame and on its shown frame give the same windows
        val plainController = WideCameraController(onCameraChanged = { _, _, _ -> })
        listOf(controller, plainController).forEach { it.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f)) }
        var plainNow = now
        fun both(count: Int): Pair<WideWindow?, WideWindow?> {
            val plain = (1..count).map {
                plainNow += 16_666_667L
                plainController.onFrame(plainNow, 512, 48, 192)
            }.last()
            return frames(count, frameWidth = 640, frameMargin = 112, frameGuard = guard) to plain
        }
        both(30)
        controller.onZoomPress()
        plainController.onZoomPress()
        controller.onSticks(0.6f, -0.4f, false)
        plainController.onSticks(0.6f, -0.4f, false)
        val (guarded, plain) = both(20)

        assertEquals(plain, guarded)
        assertEquals(512, guarded!!.frameWidth)
        assertEquals(288, guarded.frameHeight)
    }

    @Test
    fun withAGuardZoomedAllTheWayOutItShowsTheWholeShownFrame() {
        // No widest view: the range is the shown frame's, not the guarded one's
        controller.setState(WideCameraState(session = 1, settings = WideCameraSettings(showWidth = 342, returnsToCenter = true), startZoom = 1f))
        frames(30, frameWidth = 640, frameMargin = 112, frameGuard = guard)
        repeat(2) { controller.onZoomPress() }
        val window = frames(60, frameWidth = 640, frameMargin = 112, frameGuard = guard)!!

        assertEquals(512f / 342f, window.zoom, 0.0001f)
        assertEquals(0f, window.x, 0.01f)
        assertEquals(512f, window.width, 0.01f)
        assertEquals(512, window.frameWidth)
        assertEquals(288, window.frameHeight)
        // The rows fit inside the shown 288 too
        assertTrue(window.y >= 0f && window.y + window.height <= 288f)
        assertEquals(512f / 342f, renderZooms.last().second, 0.0001f)
    }

    @Test
    fun withAGuardThePanStopsAtTheShownFramesEdges() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        frames(30, frameWidth = 640, frameMargin = 112, frameGuard = guard)
        controller.onSticks(1f, 1f, false)
        val window = frames(120, frameWidth = 640, frameMargin = 112, frameGuard = guard)!!

        // At 1x the 342 columns go to the shown 512's right edge and the 192 rows to the shown 288's bottom, never into the guard
        assertEquals(170f, window.x, 0.01f)
        assertEquals(96f, window.y, 0.01f)
        assertEquals(85f, window.offset, 0.01f)
        assertEquals(48f, window.offsetY, 0.01f)
    }

    @Test
    fun withAGuardAt1xItsDrawnAsWithoutACamera() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1f))
        // The shown frame is the show width: 342 + 2 * 64 columns and 64 margin rows that are all guard
        assertNull(frames(30, frameWidth = 470, frameMargin = guard, frameGuard = guard))
        assertEquals(listOf(1 to 1f), renderZooms)
    }

    @Test
    fun aScriptsZoomWobblingAcrossAStepDoesntResizeEveryFrame() {
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 1.25f))
        frames(60, frameWidth = 428, frameMargin = 25)
        repeat(120) {
            controller.setScriptRequest(WideCameraRequest(zoom = if (it % 2 == 0) 1.26f else 1.24f))
            frames(1, frameMargin = 48)
        }
        // The start zoom's, then one wider for 1.26, and no narrower one back and forth
        assertEquals(listOf(1 to 1.25f, 1 to 512f / 342f), renderZooms)
    }

    @Test
    fun aStartZoomPastTheRangeAsksForTheWidestEvenWhileHeld() {
        controller.setScriptRequest(WideCameraRequest(holdNative = true))
        controller.setState(WideCameraState(session = 1, settings = settings, startZoom = 2f))
        assertEquals(1f, frames(30, frameWidth = 428, frameMargin = 25)!!.zoom, 0f)
        assertEquals(listOf(1 to 512f / 342f), renderZooms)
    }
}
