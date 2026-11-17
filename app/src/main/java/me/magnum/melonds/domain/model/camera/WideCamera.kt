package me.magnum.melonds.domain.model.camera

import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sign

/**
 * The part of a [frameWidth] x [frameHeight] frame (the render width Wr, and the render height Hr = SCREEN_HEIGHT + 2 margin rows) the wide
 * screen shows: columns [x, x + width) and rows [y, y + height) of the top screen's half, in DS pixels. [showWidth] is the layout's width (Wd),
 * the one the screen's rect is shaped for; a window wider than its height covers (because the margin can't cover it) is shown letterboxed top
 * and bottom.
 */
data class WideWindow(
    val showWidth: Int,
    val frameWidth: Int,
    val x: Float,
    val width: Float,
    val frameHeight: Int = SCREEN_HEIGHT,
    val y: Float = 0f,
    val height: Float = SCREEN_HEIGHT.toFloat(),
) {

    // The window's offset from the frame's center in DS pixels, what melon.wide.camera() calls x
    val offset get() = x + width / 2f - frameWidth / 2f

    // The same down the frame (down is positive), melon.wide.camera()'s y
    val offsetY get() = y + height / 2f - frameHeight / 2f

    // 1 at the show width, more when zoomed out
    val zoom get() = width / showWidth

    // The frame's rows above (and below) the top screen's SCREEN_HEIGHT, so its DS line 0 is row margin
    val margin get() = (frameHeight - SCREEN_HEIGHT) / 2

    companion object {
        /**
         * The middle [showWidth] columns and SCREEN_HEIGHT rows, which looks exactly like a frame rendered at [showWidth] with no margin
         */
        fun centered(showWidth: Int, frameWidth: Int, frameHeight: Int = SCREEN_HEIGHT): WideWindow {
            return WideWindow(
                showWidth,
                frameWidth,
                (frameWidth - showWidth) / 2f,
                showWidth.toFloat(),
                frameHeight,
                (frameHeight - SCREEN_HEIGHT) / 2f,
                SCREEN_HEIGHT.toFloat(),
            )
        }
    }
}

/**
 * What the running scripts ask of the camera with melon.wide (LuaWideCameraRequests picks it): a zoom and a pan on each axis that replace the
 * player's, null to leave them to the player, and whether to stay at 1x in the middle.
 */
data class WideCameraRequest(val zoom: Float? = null, val pan: Float? = null, val holdNative: Boolean = false, val panY: Float? = null) {
    val isOff get() = zoom == null && pan == null && panY == null && !holdNative

    companion object {
        val NONE = WideCameraRequest()
    }
}

/**
 * What a Pokemon game the native field read knows is doing (cpp/gamestate/GameState.h, which runs PokeDaisy's pk_nds_field.cpp), as
 * MelonEmulator.getGameScene returns it. The numbers are PokeDaisy's WideCamera.SCENE_*; BATTLE is its SCENE_BATTLE_START's, which ours sends
 * for the whole battle (PokeDaisy's DS build calls a battle OFF)
 */
object GameScene {
    // Not a game the field read knows (or not read yet): the camera keeps its generic rules
    const val UNKNOWN = -1
    // The overworld with nothing over it
    const val FIELD = 0
    // The overworld with a text box up: a conversation, a sign, a yes/no prompt
    const val TEXT = 1
    const val BATTLE = 3
    // Off the field: the title, the intro, any screen that isn't the overworld
    const val OFF = 4
    // The top screen faded all the way to black or white, between two maps or screens; it decides nothing, so the zoom holds through a door
    const val FADE = 5
    // A menu screen over the field (the party, the bag), and Gen 5's X menu
    const val MENU = 6
}

/**
 * What the camera sees of one presented frame (WideCamera.update takes these one by one too, so the render thread allocates nothing for them).
 *
 * @param showWidth The layout's width (Wd)
 * @param frameWidth The frame's width (Wr, what the core renders at)
 * @param topWingLines The DS top screen's lines with 3D wings this frame, the dual-screen wing hold's held lines included
 * @param areScreensSwapped Whether the DS bottom screen is the wide one, which keeps the camera at 1x
 * @param frameMargin The frame's rows of 3D above and below the top screen (camera stage 2), so it's SCREEN_HEIGHT + 2 * frameMargin tall
 * @param gameScene What a known Pokemon game is doing (GameScene), or GameScene.UNKNOWN for other games
 */
data class WideCameraFrame(
    val showWidth: Int,
    val frameWidth: Int,
    val topWingLines: Int,
    val areScreensSwapped: Boolean,
    val frameMargin: Int = 0,
    val gameScene: Int = GameScene.UNKNOWN,
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
 * @param stickY The right stick's Y, -1 to 1 (down is positive), before the dead zone
 * @param dragFractionY How far a touch dragged the picture down, as a fraction of its drawn height
 */
data class WideCameraInput(
    var stickX: Float = 0f,
    var zoomPresses: Int = 0,
    var dragFraction: Float = 0f,
    var pinchScale: Float = 1f,
    var isTouching: Boolean = false,
    var isPlayerMoving: Boolean = false,
    var stickY: Float = 0f,
    var dragFractionY: Float = 0f,
)

/**
 * The WideMelon camera (docs/plan-widemelon.md, "Camera: pan and zoom"): which window of the render-size frame the wide screen shows. The game's
 * own camera never moves; zooming out widens the window up to the render width (and heightens it into the margin rows, stage 2) and panning
 * slides it, clamped to the frame. No Android here, so it's unit-tested; not thread-safe (WideCameraController locks around it).
 *
 * In a Pokemon game the native field read knows (a GameScene other than UNKNOWN), the scene decides when to pull in to 1x instead of the wing
 * lines, and the zoom and the look-around move the way PokeDaisy's WideCamera.kt does (docs/plan-expand-parity.md, Phase 1; updateForScene).
 */
class WideCamera {

    // The player's zoom and pan, which the shown ones ease toward; a script's request replaces them while it lasts and leaves them where it was
    private var targetZoom = 1f
    private var targetPan = 0f
    private var targetPanY = 0f
    // What's shown
    private var zoom = 1f
    private var pan = 0f
    private var panY = 0f
    // How long the frame has been free to leave 1x, so a frame or two of wings after a menu doesn't zoom back out
    private var freeSeconds = 0f
    // Known games: whether the scene wants the close-up at 1x, and for how long it hasn't (PokeDaisy's closeOverSince). Close from the start, so
    // the player's zoom comes back once the field shows
    private var isCloseWanted = true
    private var closeFreeSeconds = 0f
    // Known games: a fade was the last scene that wasn't TEXT, and the seconds of TEXT since it. Leaving a building in HG/SS reads as TEXT for
    // about 0.8 s after the fade (the wipe-in runs as a field task while the town's name banner is on the text box's BG), so TEXT that follows
    // a fade with no FIELD frame between decides nothing for AFTER_FADE_TEXT_SECONDS
    private var isAfterFade = false
    private var afterFadeTextSeconds = 0f
    // Known games: the zoom's eased move from tweenFrom to tweenTarget over tweenSeconds, tweenElapsed into it (PokeDaisy's tween); a NaN
    // target makes the next one start from wherever the zoom is
    private var tweenFrom = 1f
    private var tweenTarget = Float.NaN
    private var tweenElapsed = 0f
    private var tweenSeconds = 0f
    // Known games: the last scene that wasn't UNKNOWN, used while the read gives UNKNOWN for a frame (a reset, a state load, the first poll
    // after the camera comes on), so a known game never drops to the wing-line rules; reset() forgets it, which a new session does
    private var lastKnownScene = GameScene.UNKNOWN
    // The last window handed out, handed out again while nothing moves
    private var lastWindow: WideWindow? = null

    /**
     * The zoom the player last picked with the Camera Zoom hotkey, set by the update that applied the press, for the caller to remember for the
     * game (takePickedZoom); null when there was no press since the last take
     */
    private var pickedZoom: Float? = null

    var returnsToCenter = true

    // Known games: whether a text box (GameScene.TEXT) pulls in to 1x
    var zoomsInOnText = true

    fun update(frame: WideCameraFrame, input: WideCameraInput, request: WideCameraRequest, deltaSeconds: Float): WideWindow? {
        return update(
            frame.showWidth,
            frame.frameWidth,
            frame.frameMargin,
            frame.topWingLines,
            frame.areScreensSwapped,
            input,
            request,
            deltaSeconds,
            frame.gameScene,
        )
    }

    /**
     * Moves the camera on by [deltaSeconds] for a frame (see WideCameraFrame) and returns the window to show, or null when the frame isn't wider
     * than the show width (the screen is then drawn the way it is without a camera).
     */
    fun update(
        showWidth: Int,
        frameWidth: Int,
        frameMargin: Int,
        topWingLines: Int,
        areScreensSwapped: Boolean,
        input: WideCameraInput,
        request: WideCameraRequest,
        deltaSeconds: Float,
        gameScene: Int = GameScene.UNKNOWN,
    ): WideWindow? {
        if (showWidth <= SCREEN_WIDTH || frameWidth <= showWidth) {
            // A frame from before the wide one (at launch, or a width change on its way): back to the middle, keeping the player's zoom
            zoom = 1f
            resetPan()
            freeSeconds = 0f
            tweenTarget = Float.NaN
            return null
        }

        val frameHeight = SCREEN_HEIGHT + 2 * frameMargin.coerceAtLeast(0)
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
        // drops its 3D, the holds end within 2 frames. In a known game the scene takes the wing lines' place (updateForScene)
        if (gameScene != GameScene.UNKNOWN) {
            lastKnownScene = gameScene
        }
        val scene = lastKnownScene
        val isKnownGame = scene != GameScene.UNKNOWN
        val isHeldNative = request.holdNative || areScreensSwapped || (!isKnownGame && topWingLines < MIN_WING_LINES)
        if (isHeldNative) {
            // Straight to 1x and the middle, since the wings are black or don't belong to this screen; the player's zoom comes back after
            zoom = 1f
            resetPan()
            freeSeconds = 0f
            tweenTarget = Float.NaN
            // In a known game it comes back like after any close-up, CLOSE_RETURN_SECONDS after the hold ends, eased
            isCloseWanted = true
            closeFreeSeconds = 0f
            return getCenteredWindow(showWidth, frameWidth, frameHeight)
        }
        if (isKnownGame) {
            return updateForScene(showWidth, frameWidth, frameHeight, scene, input, request, dt, maxZoom)
        }
        tweenTarget = Float.NaN
        freeSeconds += dt
        if (freeSeconds < RESUME_SECONDS) {
            return getCenteredWindow(showWidth, frameWidth, frameHeight)
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

        // Pan, each axis on its own: the script's, or the stick, a drag and the drift back to the middle
        val isStickIdle = abs(input.stickX) <= STICK_DEAD_ZONE && abs(input.stickY) <= STICK_DEAD_ZONE
        val isReturning = returnsToCenter && isStickIdle && !input.isTouching && input.isPlayerMoving
        val returnFactor = if (isReturning) exp(-dt / RETURN_SECONDS) else 1f
        if (request.pan != null) {
            targetPan = request.pan
        } else {
            targetPan += getStickPan(input.stickX, dt)
            // The picture follows the finger, so the window moves the other way
            targetPan -= input.dragFraction * showWidth * zoom
            targetPan = returnToCenter(targetPan, returnFactor)
        }
        if (request.panY != null) {
            targetPanY = request.panY
        } else {
            targetPanY += getStickPan(input.stickY, dt)
            // A drag is a fraction of the picture's height, which shows the window's rows
            targetPanY -= input.dragFractionY * getWindowHeight(frameHeight, zoom)
            targetPanY = returnToCenter(targetPanY, returnFactor)
        }
        targetPan = targetPan.coerceIn(-getMaxPan(showWidth, frameWidth, targetZoom), getMaxPan(showWidth, frameWidth, targetZoom))
        targetPanY = targetPanY.coerceIn(-getMaxPanY(frameHeight, targetZoom), getMaxPanY(frameHeight, targetZoom))
        pan = if (input.isTouching && request.pan == null) targetPan else approach(pan, targetPan, PAN_SECONDS, dt, PAN_SNAP)
        panY = if (input.isTouching && request.panY == null) targetPanY else approach(panY, targetPanY, PAN_SECONDS, dt, PAN_SNAP)
        val maxPan = getMaxPan(showWidth, frameWidth, zoom)
        pan = pan.coerceIn(-maxPan, maxPan)
        val maxPanY = getMaxPanY(frameHeight, zoom)
        panY = panY.coerceIn(-maxPanY, maxPanY)

        val width = (showWidth * zoom).coerceAtMost(frameWidth.toFloat())
        val x = ((frameWidth - width) / 2f + pan).coerceIn(0f, frameWidth - width)
        val height = getWindowHeight(frameHeight, zoom)
        val y = ((frameHeight - height) / 2f + panY).coerceIn(0f, frameHeight - height)
        return getWindow(showWidth, frameWidth, x, width, frameHeight, y, height)
    }

    /**
     * [update] for a known Pokemon game, after the holds every game has (a script's hold_native, swapped screens): PokeDaisy's WideCamera.frame,
     * with our stick speed, dead zone, touch drag, pinch and script requests kept.
     *
     * - The scene says when to pull in to 1x: a battle, a menu screen and anything off the field always do, a text box if [zoomsInOnText], the
     *   plain field never, and a fade decides nothing (the zoom holds through a door). The player's zoom comes back CLOSE_RETURN_SECONDS after
     *   the close-up was last wanted. Ours only: a text box straight after a fade, with no field between, decides nothing for its first
     *   AFTER_FADE_TEXT_SECONDS (HG/SS's building exits; see isAfterFade).
     * - The zoom eases with smoothstep in log space: PULL_IN_SECONDS in (a battle too, as on PokeDaisy's DS path), ZOOM_OUT_SECONDS out. A pinch
     *   still follows the fingers, and a script's zoom eases as in other games.
     * - Pulled in, the look-around springs back to the middle (LOOK_SECONDS). Walking with [returnsToCenter] on, it springs back to the middle
     *   (WALK_RETURN_SECONDS), or with the right stick held follows the stick, keeping the middle PLAYER_KEEP DS pixels inside the window
     *   (LOOK_SECONDS). Standing, the stick and a drag pan as in other games, and the view stays where it's left.
     */
    private fun updateForScene(
        showWidth: Int,
        frameWidth: Int,
        frameHeight: Int,
        gameScene: Int,
        input: WideCameraInput,
        request: WideCameraRequest,
        dt: Float,
        maxZoom: Float,
    ): WideWindow {
        when (gameScene) {
            GameScene.FADE -> {
                isAfterFade = true
                afterFadeTextSeconds = 0f
            }
            GameScene.TEXT -> if (isAfterFade) afterFadeTextSeconds += dt
            else -> isAfterFade = false
        }
        val isFadeText = gameScene == GameScene.TEXT && isAfterFade && afterFadeTextSeconds < AFTER_FADE_TEXT_SECONDS
        val wantsClose = when {
            gameScene == GameScene.FIELD -> false
            // Decides nothing: still close if it was, and the time since the close-up keeps running if it wasn't
            gameScene == GameScene.FADE || isFadeText -> isCloseWanted
            gameScene == GameScene.TEXT -> zoomsInOnText
            else -> true
        }
        if (wantsClose) {
            isCloseWanted = true
            closeFreeSeconds = 0f
        } else {
            isCloseWanted = false
            closeFreeSeconds = (closeFreeSeconds + dt).coerceAtMost(CLOSE_RETURN_SECONDS)
        }
        val isClose = isCloseWanted || closeFreeSeconds < CLOSE_RETURN_SECONDS
        // Kept meaningful for the wing-line rules: held while pulled in, free to leave 1x otherwise
        freeSeconds = if (isClose) 0f else RESUME_SECONDS

        // Zoom: the script's, or the player's pinches (the hotkey's steps are in update), left alone while pulled in
        if (!isClose) {
            if (request.zoom != null) {
                targetZoom = request.zoom
            } else if (input.pinchScale > 0f && input.pinchScale != 1f) {
                targetZoom /= input.pinchScale
            }
        }
        targetZoom = targetZoom.coerceIn(1f, maxZoom)
        val shownTarget = if (isClose) 1f else targetZoom
        if (!isClose && input.isTouching && request.zoom == null) {
            // A pinch follows the fingers
            zoom = targetZoom
            tweenFrom = zoom
            tweenTarget = zoom
            tweenSeconds = 0f
        } else if (!isClose && request.zoom != null) {
            // A script's zoom eases as in other games, since a script may move it every frame, which would restart a timed move each time
            zoom = approach(zoom, targetZoom, ZOOM_SECONDS, dt, ZOOM_SNAP)
            tweenTarget = Float.NaN
        } else {
            if (shownTarget != tweenTarget) {
                // A new target starts a new move from wherever the zoom is: in at the pace of what's coming, back out slower
                tweenFrom = zoom
                tweenTarget = shownTarget
                tweenElapsed = 0f
                tweenSeconds = if (shownTarget < tweenFrom) PULL_IN_SECONDS else ZOOM_OUT_SECONDS
            } else {
                tweenElapsed += dt
            }
            val progress = if (tweenSeconds <= 0f) 1f else (tweenElapsed / tweenSeconds).coerceIn(0f, 1f)
            zoom = tweenFrom * (tweenTarget / tweenFrom).pow(smoothstep(progress))
            if (abs(zoom - shownTarget) < ZOOM_SNAP) {
                zoom = shownTarget
            }
        }

        // Pan, each axis on its own
        val width = (showWidth * zoom).coerceAtMost(frameWidth.toFloat())
        val height = getWindowHeight(frameHeight, zoom)
        val stickX = getStickStrength(input.stickX)
        val stickY = getStickStrength(input.stickY)
        val isWalking = returnsToCenter && input.isPlayerMoving && !input.isTouching
        // Where a spring pulls the view, and its time constant; 0 for none (standing)
        var springX = 0f
        var springY = 0f
        val springSeconds = when {
            isClose -> LOOK_SECONDS
            isWalking && (stickX != 0f || stickY != 0f) -> {
                // Follow the stick, keeping the player (the frame's middle) inside the window
                springX = stickX * max(0f, width / 2f - PLAYER_KEEP)
                springY = stickY * max(0f, height / 2f - PLAYER_KEEP)
                LOOK_SECONDS
            }
            isWalking -> WALK_RETURN_SECONDS
            else -> 0f
        }
        val maxPan = getMaxPan(showWidth, frameWidth, zoom)
        val maxPanY = getMaxPanY(frameHeight, zoom)
        // Pulled in, the middle wins over a script's pan, as with hold_native; walking, a script's pan wins over the springs
        if (springSeconds > 0f && (isClose || request.pan == null)) {
            pan = spring(pan, springX, dt, springSeconds).coerceIn(-maxPan, maxPan)
            targetPan = pan
        } else {
            targetPan = request.pan ?: (targetPan + getStickPan(input.stickX, dt) - input.dragFraction * showWidth * zoom)
            targetPan = targetPan.coerceIn(-maxPan, maxPan)
            pan = if (input.isTouching && request.pan == null) targetPan else approach(pan, targetPan, PAN_SECONDS, dt, PAN_SNAP)
        }
        if (springSeconds > 0f && (isClose || request.panY == null)) {
            panY = spring(panY, springY, dt, springSeconds).coerceIn(-maxPanY, maxPanY)
            targetPanY = panY
        } else {
            targetPanY = request.panY ?: (targetPanY + getStickPan(input.stickY, dt) - input.dragFractionY * height)
            targetPanY = targetPanY.coerceIn(-maxPanY, maxPanY)
            panY = if (input.isTouching && request.panY == null) targetPanY else approach(panY, targetPanY, PAN_SECONDS, dt, PAN_SNAP)
        }
        pan = pan.coerceIn(-maxPan, maxPan)
        panY = panY.coerceIn(-maxPanY, maxPanY)

        val x = ((frameWidth - width) / 2f + pan).coerceIn(0f, frameWidth - width)
        val y = ((frameHeight - height) / 2f + panY).coerceIn(0f, frameHeight - height)
        return getWindow(showWidth, frameWidth, x, width, frameHeight, y, height)
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

    private fun resetPan() {
        pan = 0f
        targetPan = 0f
        panY = 0f
        targetPanY = 0f
    }

    private fun getCenteredWindow(showWidth: Int, frameWidth: Int, frameHeight: Int): WideWindow {
        return getWindow(
            showWidth,
            frameWidth,
            (frameWidth - showWidth) / 2f,
            showWidth.toFloat(),
            frameHeight,
            (frameHeight - SCREEN_HEIGHT) / 2f,
            SCREEN_HEIGHT.toFloat(),
        )
    }

    // The last window again if it's the same, so a camera at rest allocates nothing
    private fun getWindow(showWidth: Int, frameWidth: Int, x: Float, width: Float, frameHeight: Int, y: Float, height: Float): WideWindow {
        val last = lastWindow
        if (last != null && last.showWidth == showWidth && last.frameWidth == frameWidth && last.x == x && last.width == width &&
            last.frameHeight == frameHeight && last.y == y && last.height == height) {
            return last
        }
        return WideWindow(showWidth, frameWidth, x, width, frameHeight, y, height).also { lastWindow = it }
    }

    /**
     * Back to 1x in the middle, for a camera that's off or a new session
     */
    fun reset() {
        pickedZoom = null
        targetZoom = 1f
        zoom = 1f
        resetPan()
        freeSeconds = 0f
        isCloseWanted = true
        closeFreeSeconds = 0f
        isAfterFade = false
        afterFadeTextSeconds = 0f
        tweenTarget = Float.NaN
        lastKnownScene = GameScene.UNKNOWN
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
        // Known games, from PokeDaisy's WideCamera.kt: the zoom's eased moves (in, back out), the look-around's springs
        // (following the stick or pulled in, and back to the player who walks), how far inside the window the player stays while the view
        // follows the stick (DS pixels), and how long after the close-up the player's zoom comes back
        const val PULL_IN_SECONDS = 0.42f
        const val ZOOM_OUT_SECONDS = 0.65f
        const val LOOK_SECONDS = 0.12f
        const val WALK_RETURN_SECONDS = 0.18f
        const val PLAYER_KEEP = 28f
        const val CLOSE_RETURN_SECONDS = 0.5f
        // Ours: how long TEXT straight after a fade decides nothing (an HG/SS building exit reads as TEXT for about 0.8 s)
        const val AFTER_FADE_TEXT_SECONDS = 1.5f
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
         * How far a window at [zoom] can sit off center vertically in a [frameHeight]-tall frame (none without a margin)
         */
        fun getMaxPanY(frameHeight: Int, zoom: Float): Float {
            return ((frameHeight - SCREEN_HEIGHT * zoom) / 2f).coerceAtLeast(0f)
        }

        /**
         * The rows a window at [zoom] shows of a [frameHeight]-tall frame: as many as keep the DS's pixel shape, at most the frame's (the rest is
         * letterboxed)
         */
        fun getWindowHeight(frameHeight: Int, zoom: Float): Float {
            return (SCREEN_HEIGHT * zoom).coerceAtMost(frameHeight.toFloat())
        }

        // DS pixels the stick moves the pan by over dt, 0 inside the dead zone
        private fun getStickPan(stick: Float, dt: Float): Float {
            return getStickStrength(stick) * PAN_SPEED * dt
        }

        // The stick past the dead zone, -1 to 1
        private fun getStickStrength(stick: Float): Float {
            if (abs(stick) <= STICK_DEAD_ZONE) {
                return 0f
            }
            val strength = (abs(stick) - STICK_DEAD_ZONE) / (1f - STICK_DEAD_ZONE)
            return sign(stick) * strength.coerceAtMost(1f)
        }

        // PokeDaisy's spring: [current] pulled toward [target] with time constant [seconds] over dt, landing on it once within 0.05 DS pixels
        private fun spring(current: Float, target: Float, dt: Float, seconds: Float): Float {
            val next = current + (target - current) * (1f - exp(-dt / seconds))
            return if (abs(next - target) < 0.05f) target else next
        }

        private fun smoothstep(p: Float) = p * p * (3f - 2f * p)

        // The pan drifted back toward the middle by [factor] (1 for no drift), landing on the middle exactly, where the picture is the same as
        // without a camera
        private fun returnToCenter(pan: Float, factor: Float): Float {
            if (factor == 1f) {
                return pan
            }
            val next = pan * factor
            return if (abs(next) < 0.01f) 0f else next
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
