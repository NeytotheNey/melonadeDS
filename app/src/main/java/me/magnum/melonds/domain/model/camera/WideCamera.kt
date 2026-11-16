package me.magnum.melonds.domain.model.camera

import me.magnum.melonds.domain.model.SCREEN_WIDTH
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.sign

/**
 * The part of a [frameWidth]-wide frame (the render width, Wr) the wide screen shows: columns [x, x + width) in DS pixels. [showWidth] is the
 * layout's width (Wd), the one the screen's rect is shaped for; a window wider than it is shown letterboxed top and bottom.
 */
data class WideWindow(val showWidth: Int, val frameWidth: Int, val x: Float, val width: Float) {

    // The window's offset from the frame's center in DS pixels, what melon.wide.camera() calls x
    val offset get() = x + width / 2f - frameWidth / 2f

    // 1 at the show width, more when zoomed out
    val zoom get() = width / showWidth

    companion object {
        /**
         * The middle [showWidth] columns, which looks exactly like a frame rendered at [showWidth]
         */
        fun centered(showWidth: Int, frameWidth: Int): WideWindow {
            return WideWindow(showWidth, frameWidth, (frameWidth - showWidth) / 2f, showWidth.toFloat())
        }
    }
}

/**
 * What the running scripts ask of the camera with melon.wide (LuaWideCameraRequests picks it): a zoom and a pan that replace the player's, null to
 * leave them to the player, and whether to stay at 1x in the middle.
 */
data class WideCameraRequest(val zoom: Float? = null, val pan: Float? = null, val holdNative: Boolean = false) {
    val isOff get() = zoom == null && pan == null && !holdNative

    companion object {
        val NONE = WideCameraRequest()
    }
}

/**
 * What the camera sees of one presented frame (WideCamera.update takes these one by one too, so the render thread allocates nothing for them).
 *
 * @param showWidth The layout's width (Wd)
 * @param frameWidth The frame's width (Wr, what the core renders at)
 * @param topWingLines The DS top screen's lines with 3D wings this frame, the dual-screen wing hold's held lines included
 * @param areScreensSwapped Whether the DS bottom screen is the wide one, which keeps the camera at 1x
 */
data class WideCameraFrame(
    val showWidth: Int,
    val frameWidth: Int,
    val topWingLines: Int,
    val areScreensSwapped: Boolean,
)

/**
 * The player's camera input since the last frame; mutable so WideCameraController can reuse one.
 *
 * @param stickX The right stick's X, -1 to 1 (right is positive), before the dead zone
 * @param zoomPresses Camera Zoom hotkey presses, each one the next zoom step (see WideCamera.cycleZoom). WideCamera.update zeroes it once it has
 * applied them (or dropped them for a script's zoom), so presses that arrive before a wide frame can wait for one
 * @param dragFraction How far a touch dragged the picture, as a fraction of the screen's width (right is positive)
 * @param pinchScale How much a pinch spread the fingers (above 1) or closed them, multiplied over the frame
 * @param isTouching Whether a finger is on the wide screen
 * @param isPlayerMoving Whether the D-pad or the left stick is held
 */
data class WideCameraInput(
    var stickX: Float = 0f,
    var zoomPresses: Int = 0,
    var dragFraction: Float = 0f,
    var pinchScale: Float = 1f,
    var isTouching: Boolean = false,
    var isPlayerMoving: Boolean = false,
)

/**
 * The WideMelon camera (docs/plan-widemelon.md, "Camera: pan and zoom"): which window of the render-width frame the wide screen shows. The game's
 * own camera never moves; zooming out widens the window up to the render width and panning slides it, clamped to the frame. No Android here, so
 * it's unit-tested; not thread-safe (WideCameraController locks around it).
 */
class WideCamera {

    // The player's zoom and pan, which the shown ones ease toward; a script's request replaces them while it lasts and leaves them where it was
    private var targetZoom = 1f
    private var targetPan = 0f
    // What's shown
    private var zoom = 1f
    private var pan = 0f
    // How long the frame has been free to leave 1x, so a frame or two of wings after a menu doesn't zoom back out
    private var freeSeconds = 0f
    // The last window handed out, handed out again while nothing moves
    private var lastWindow: WideWindow? = null

    /**
     * The zoom the player last picked with the Camera Zoom hotkey, set by the update that applied the press, for the caller to remember for the
     * game (takePickedZoom); null when there was no press since the last take
     */
    private var pickedZoom: Float? = null

    var returnsToCenter = true

    fun update(frame: WideCameraFrame, input: WideCameraInput, request: WideCameraRequest, deltaSeconds: Float): WideWindow? {
        return update(frame.showWidth, frame.frameWidth, frame.topWingLines, frame.areScreensSwapped, input, request, deltaSeconds)
    }

    /**
     * Moves the camera on by [deltaSeconds] for a frame (see WideCameraFrame) and returns the window to show, or null when the frame isn't wider
     * than the show width (the screen is then drawn the way it is without a camera).
     */
    fun update(
        showWidth: Int,
        frameWidth: Int,
        topWingLines: Int,
        areScreensSwapped: Boolean,
        input: WideCameraInput,
        request: WideCameraRequest,
        deltaSeconds: Float,
    ): WideWindow? {
        if (showWidth <= SCREEN_WIDTH || frameWidth <= showWidth) {
            // A frame from before the wide one (at launch, or a width change on its way): back to the middle, keeping the player's zoom
            zoom = 1f
            pan = 0f
            targetPan = 0f
            freeSeconds = 0f
            return null
        }

        val dt = deltaSeconds.coerceIn(0f, MAX_DELTA_SECONDS)
        val maxZoom = frameWidth / showWidth.toFloat()
        // The hotkey's presses count even while 1x is held (in a menu, say), so the picked step shows once the hold ends; while a script sets
        // the zoom they're dropped
        if (input.zoomPresses > 0) {
            if (request.zoom == null) {
                repeat(input.zoomPresses) { targetZoom = cycleZoom(targetZoom.coerceIn(1f, maxZoom), maxZoom) }
                pickedZoom = targetZoom
            }
            input.zoomPresses = 0
        }
        // Dual-screen 3D games aren't held: the core counts the wing hold's held lines, so their top screen has wings on every frame. Once a game
        // drops its 3D, the holds end within 2 frames
        val isHeldNative = request.holdNative || areScreensSwapped || topWingLines < MIN_WING_LINES
        if (isHeldNative) {
            // Straight to 1x and the middle, since the wings are black or don't belong to this screen; the player's zoom comes back after
            zoom = 1f
            pan = 0f
            targetPan = 0f
            freeSeconds = 0f
            return getWindow(showWidth, frameWidth, (frameWidth - showWidth) / 2f, showWidth.toFloat())
        }
        freeSeconds += dt
        if (freeSeconds < RESUME_SECONDS) {
            return getWindow(showWidth, frameWidth, (frameWidth - showWidth) / 2f, showWidth.toFloat())
        }

        // Zoom: the script's, or the player's pinches (the hotkey's steps are in above)
        if (request.zoom != null) {
            targetZoom = request.zoom
        } else if (input.pinchScale > 0f && input.pinchScale != 1f) {
            targetZoom /= input.pinchScale
        }
        targetZoom = targetZoom.coerceIn(1f, maxZoom)
        // A pinch follows the fingers
        zoom = if (input.isTouching && request.zoom == null) targetZoom else approach(zoom, targetZoom, ZOOM_SECONDS, dt, ZOOM_SNAP)

        // Pan: the script's, or the stick, a drag and the drift back to the middle
        val isStickIdle = abs(input.stickX) <= STICK_DEAD_ZONE
        if (request.pan != null) {
            targetPan = request.pan
        } else {
            if (!isStickIdle) {
                val strength = (abs(input.stickX) - STICK_DEAD_ZONE) / (1f - STICK_DEAD_ZONE)
                targetPan += sign(input.stickX) * strength.coerceAtMost(1f) * PAN_SPEED * dt
            }
            // The picture follows the finger, so the window moves the other way
            targetPan -= input.dragFraction * showWidth * zoom
            if (returnsToCenter && isStickIdle && !input.isTouching && input.isPlayerMoving) {
                targetPan *= exp(-dt / RETURN_SECONDS)
                // Landing on the middle exactly, where the picture is the same as without a camera
                if (abs(targetPan) < 0.01f) {
                    targetPan = 0f
                }
            }
        }
        targetPan = targetPan.coerceIn(-getMaxPan(showWidth, frameWidth, targetZoom), getMaxPan(showWidth, frameWidth, targetZoom))
        pan = if (input.isTouching && request.pan == null) targetPan else approach(pan, targetPan, PAN_SECONDS, dt, PAN_SNAP)
        val maxPan = getMaxPan(showWidth, frameWidth, zoom)
        pan = pan.coerceIn(-maxPan, maxPan)

        val width = (showWidth * zoom).coerceAtMost(frameWidth.toFloat())
        val x = ((frameWidth - width) / 2f + pan).coerceIn(0f, frameWidth - width)
        return getWindow(showWidth, frameWidth, x, width)
    }

    /**
     * Starts the player's zoom at [zoom] (the step remembered for the game), clamped to the frame once it's known
     */
    fun setPlayerZoom(zoom: Float) {
        targetZoom = zoom.coerceAtLeast(1f)
    }

    /**
     * The zoom the last hotkey press picked, once, or null if there was none since the last call
     */
    fun takePickedZoom(): Float? {
        return pickedZoom.also { pickedZoom = null }
    }

    // The last window again if it's the same, so a camera at rest allocates nothing
    private fun getWindow(showWidth: Int, frameWidth: Int, x: Float, width: Float): WideWindow {
        val last = lastWindow
        if (last != null && last.showWidth == showWidth && last.frameWidth == frameWidth && last.x == x && last.width == width) {
            return last
        }
        return WideWindow(showWidth, frameWidth, x, width).also { lastWindow = it }
    }

    /**
     * Back to 1x in the middle, for a camera that's off or a new session
     */
    fun reset() {
        pickedZoom = null
        targetZoom = 1f
        targetPan = 0f
        zoom = 1f
        pan = 0f
        freeSeconds = 0f
    }

    companion object {
        // Zoom steps of the Camera Zoom hotkey, in zoom factor; the widest zoom is a step of its own when the range doesn't end on one
        const val ZOOM_STEP = 0.25f
        // Right stick: dead zone, and DS pixels per second at full tilt
        const val STICK_DEAD_ZONE = 0.2f
        const val PAN_SPEED = 300f
        // Easing time constants: how fast the shown zoom and pan catch up, and how fast the pan drifts back to the middle while the player moves
        const val ZOOM_SECONDS = 0.12f
        const val PAN_SECONDS = 0.08f
        const val RETURN_SECONDS = 0.5f
        // How long a frame has to stay free of the things that hold 1x before the player's zoom comes back
        const val RESUME_SECONDS = 0.25f
        // Fewer top-screen lines with 3D wings than this holds 1x
        const val MIN_WING_LINES = 1
        // A longer gap (a pause, a stall) counts as this, so the camera doesn't jump
        const val MAX_DELTA_SECONDS = 0.1f
        // How close the shown zoom (in zoom factor) and pan (in DS pixels) get before they land on their targets
        private const val ZOOM_SNAP = 0.001f
        private const val PAN_SNAP = 0.01f

        /**
         * What a Camera Zoom press goes to from [zoom]: the next step above it (a pinched zoom between steps goes to the one above), or 1x from
         * the widest view, like PokeDaisy's Expand. With a widest view of 1.5 that's 1, 1.25, 1.5, 1, ...
         */
        fun cycleZoom(zoom: Float, maxZoom: Float): Float {
            if (zoom >= maxZoom - 1e-3f) {
                return 1f
            }
            // The next multiple of ZOOM_STEP above it, at most the widest view
            return ((floor(zoom / ZOOM_STEP + 1e-3f) + 1f) * ZOOM_STEP).coerceIn(1f, maxZoom)
        }

        /**
         * How far a window at [zoom] can sit off center in a [frameWidth]-wide frame
         */
        fun getMaxPan(showWidth: Int, frameWidth: Int, zoom: Float): Float {
            return ((frameWidth - showWidth * zoom) / 2f).coerceAtLeast(0f)
        }

        /**
         * [current] eased toward [target] with time constant [seconds] over [dt], landing on it once within [snap]
         */
        private fun approach(current: Float, target: Float, seconds: Float, dt: Float, snap: Float): Float {
            val next = current + (target - current) * (1f - exp(-dt / seconds))
            return if (abs(target - next) < snap) target else next
        }
    }
}
