package me.magnum.melonds.ui.emulator.camera

import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.camera.WideCamera
import me.magnum.melonds.domain.model.camera.WideCameraInput
import me.magnum.melonds.domain.model.camera.WideCameraRequest
import me.magnum.melonds.domain.model.camera.WideWindow
import kotlin.math.abs

/**
 * The camera's settings for the shown layout: [showWidth] is the layout's width (Wd), and the core renders wider (Wr, the frame's width).
 */
data class WideCameraSettings(val showWidth: Int, val returnsToCenter: Boolean)

/**
 * The camera's state for the screen: [session] tells one game session from the next (a ROM switch), [settings] are null while it's off, and
 * [startZoom] is the zoom the player last picked for the game, which the camera starts at when it comes on. Kept apart from [settings], so a
 * pick doesn't count as a settings change.
 */
data class WideCameraState(val session: Int, val settings: WideCameraSettings?, val startZoom: Float)

/**
 * Collects the player's camera input and the scripts' requests from the main thread and moves the [WideCamera] once per presented frame on the
 * render thread (FrameRenderCoordinator), so every surface shows the same window. [onCameraChanged] gets the window's offset from center, its
 * zoom and its offset down from center for melon.wide.camera(), on the render thread, on the first frame and whenever they change. [onZoomPicked] gets the zoom a Camera Zoom
 * press picked and the session it was picked in, on the render thread, to remember for the game.
 */
class WideCameraController(
    private val onZoomPicked: (session: Int, zoom: Float) -> Unit = { _, _ -> },
    private val onCameraChanged: (x: Float, zoom: Float, y: Float) -> Unit,
) {

    private val lock = Any()
    private val camera = WideCamera()

    // Guarded by lock
    private var session = 0
    private var settings: WideCameraSettings? = null
    private var startZoom = 1f
    private var request = WideCameraRequest.NONE
    private var areScreensSwapped = false
    private var stickX = 0f
    private var stickY = 0f
    private var leftStickMoving = false
    private val heldDirections = mutableSetOf<Input>()
    private var zoomPresses = 0
    private var dragFraction = 0f
    private var dragFractionY = 0f
    private var pinchScale = 1f
    private var isTouching = false
    private var lastFrameNanos = 0L
    // Reused for every frame, so a moving camera allocates only its window
    private val input = WideCameraInput()

    // Render thread only. NaN until the first frame, so a new controller (after the activity is recreated, say) always tells the scripts where
    // the camera is instead of leaving them the old one's
    private var sentX = Float.NaN
    private var sentZoom = Float.NaN
    private var sentY = Float.NaN

    /**
     * Whether the camera is on for the shown layout, so the input that drives it is worth sending
     */
    val isEnabled: Boolean
        get() = synchronized(lock) { settings != null }

    /**
     * The camera's state from the view model (see WideCameraState). A new session starts over from nothing, even if the off state between the
     * sessions never came through; within one, settings of null switch it off (no widescreen, the camera switched off, no zoom-out past the
     * shown width, or a layout variant that isn't wide), and a settings change drops the held input, whose releases may never come
     */
    fun setState(state: WideCameraState) = synchronized(lock) {
        if (state.session != session) {
            session = state.session
            applySettings(null)
        }
        startZoom = state.startZoom
        applySettings(state.settings)
    }

    /**
     * [setState] within the current session
     */
    fun setSettings(newSettings: WideCameraSettings?, newStartZoom: Float = startZoom) = synchronized(lock) {
        startZoom = newStartZoom
        applySettings(newSettings)
    }

    // Under lock
    private fun applySettings(newSettings: WideCameraSettings?) {
        if (newSettings == null) {
            camera.reset()
            // Presses wait for a frame that can apply them, but not past the camera going off
            zoomPresses = 0
        }
        // Coming on starts at the game's remembered zoom; a change of the shown width (a fold, say) keeps the zoom that's there
        if (newSettings != null && settings == null) {
            camera.setPlayerZoom(startZoom)
        }
        if (newSettings != settings) {
            clearHeldInput()
        }
        settings = newSettings
        camera.returnsToCenter = newSettings?.returnsToCenter ?: true
    }

    fun setScriptRequest(newRequest: WideCameraRequest) = synchronized(lock) {
        request = newRequest
    }

    fun setScreensSwapped(swapped: Boolean) = synchronized(lock) {
        areScreensSwapped = swapped
    }

    /**
     * The right stick's X and Y (-1 to 1, right and down are positive) and whether the left stick is pushed
     */
    fun onSticks(rightX: Float, rightY: Float, isLeftStickPushed: Boolean) = synchronized(lock) {
        stickX = rightX
        stickY = rightY
        leftStickMoving = isLeftStickPushed
    }

    /**
     * A DS input went down or up; the D-pad counts as the player moving
     */
    fun onKey(input: Input, isPressed: Boolean) = synchronized(lock) {
        if (input == Input.UP || input == Input.DOWN || input == Input.LEFT || input == Input.RIGHT) {
            if (isPressed) heldDirections.add(input) else heldDirections.remove(input)
        }
    }

    /**
     * A Camera Zoom press: the next zoom step, or 1x after the widest
     */
    fun onZoomPress() = synchronized(lock) {
        zoomPresses++
    }

    /**
     * A touch on the wide screen: [dragFraction] of its drawn picture's width moved right, [dragFractionY] of its height down, fingers spread by
     * [pinchScale]
     */
    fun onTouchMove(dragFraction: Float, pinchScale: Float, dragFractionY: Float = 0f) = synchronized(lock) {
        this.dragFraction += dragFraction
        this.dragFractionY += dragFractionY
        this.pinchScale *= pinchScale
    }

    fun setTouching(touching: Boolean) = synchronized(lock) {
        isTouching = touching
    }

    /**
     * Forgets the sticks, the D-pad and a touch, for when their releases won't come (the touch listener was swapped out mid-gesture, a menu
     * opened over the game)
     */
    fun releaseHeldInput() = synchronized(lock) {
        clearHeldInput()
    }

    // Under lock
    private fun clearHeldInput() {
        stickX = 0f
        stickY = 0f
        leftStickMoving = false
        heldDirections.clear()
        isTouching = false
        dragFraction = 0f
        dragFractionY = 0f
        pinchScale = 1f
    }

    /**
     * Moves the camera on for a presented frame [frameWidth] wide with [frameMargin] rows above and below the top screen, and returns its window,
     * or null to draw the frame as without a camera. Render thread.
     */
    fun onFrame(nowNanos: Long, frameWidth: Int, frameMargin: Int, topWingLines: Int): WideWindow? {
        var pickedZoom: Float? = null
        var pickedSession = 0
        val window = synchronized(lock) {
            val currentSettings = settings
            val deltaSeconds = if (lastFrameNanos == 0L) 0f else (nowNanos - lastFrameNanos) / 1e9f
            lastFrameNanos = nowNanos
            input.stickX = stickX
            input.stickY = stickY
            input.zoomPresses = zoomPresses
            input.dragFraction = dragFraction
            input.dragFractionY = dragFractionY
            input.pinchScale = pinchScale
            input.isTouching = isTouching
            input.isPlayerMoving = heldDirections.isNotEmpty() || leftStickMoving
            dragFraction = 0f
            dragFractionY = 0f
            pinchScale = 1f
            if (currentSettings == null) {
                zoomPresses = 0
                null
            } else {
                camera.update(currentSettings.showWidth, frameWidth, frameMargin, topWingLines, areScreensSwapped, input, request, deltaSeconds).also {
                    // Whatever the camera couldn't apply yet (a frame from before the wide one) waits for the next frame
                    zoomPresses = input.zoomPresses
                    pickedZoom = camera.takePickedZoom()
                    pickedSession = session
                }
            }
        }
        pickedZoom?.let { onZoomPicked(pickedSession, it) }

        val x = window?.offset ?: 0f
        val zoom = window?.zoom ?: 1f
        val y = window?.offsetY ?: 0f
        // Small moves aren't worth telling the scripts about, but landing back in the middle at 1x always is (as is the first frame)
        val isBackAtRest = x == 0f && y == 0f && zoom == 1f && (sentX != 0f || sentY != 0f || sentZoom != 1f)
        if (isBackAtRest || abs(x - sentX) >= CHANGE_THRESHOLD || abs(y - sentY) >= CHANGE_THRESHOLD || abs(zoom - sentZoom) >= CHANGE_THRESHOLD / 256f) {
            sentX = x
            sentZoom = zoom
            sentY = y
            onCameraChanged(x, zoom, y)
        }
        return window
    }

    companion object {
        // In DS pixels (and that much of a 256 wide window for the zoom)
        private const val CHANGE_THRESHOLD = 0.05f
    }
}
