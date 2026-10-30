package me.magnum.melonds.ui.emulator

import android.content.Context
import android.os.SystemClock
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import androidx.core.view.doOnNextLayout
import androidx.core.view.isGone
import androidx.core.view.isVisible
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.common.vibration.TouchVibrator
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.input.SoftInputBehaviour
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.getScreensDrawing
import me.magnum.melonds.ui.common.LayoutView
import me.magnum.melonds.ui.common.view.RaisedButtonView
import me.magnum.melonds.ui.emulator.input.ButtonsInputHandler
import me.magnum.melonds.ui.emulator.input.DpadInputHandler
import me.magnum.melonds.ui.emulator.input.FrontendInputHandler
import me.magnum.melonds.ui.emulator.input.IInputListener
import me.magnum.melonds.ui.emulator.input.SingleButtonInputHandler
import me.magnum.melonds.ui.emulator.input.TouchscreenInputHandler
import me.magnum.melonds.ui.emulator.model.ConnectedControllersState
import me.magnum.melonds.ui.emulator.model.RuntimeInputLayoutConfiguration
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget
import javax.inject.Inject

@AndroidEntryPoint
class RuntimeLayoutView(context: Context, attrs: AttributeSet? = null) : LayoutView(context, attrs) {

    @Inject
    lateinit var touchVibrator: TouchVibrator

    private var currentRuntimeLayout: RuntimeInputLayoutConfiguration? = null
    private var frontendInputHandler: TouchCarryingInputListener? = null
    private var systemInputHandler: TouchCarryingInputListener? = null
    private var isSoftInputVisible = true
    private var areScreensSwapped = false
    private var connectedControllersState: ConnectedControllersState = ConnectedControllersState.NoControllers
    // The width of the frames the screens are drawn from, in DS pixels (DSRenderer.frameTopScreenWidth), for where touches go
    private var frameWidthProvider: () -> Int = { SCREEN_WIDTH }

    // The last event of the touches that are down now, or null if none is. Kept so they can be carried over to a new layout
    private var currentTouches: MotionEvent? = null
    // True from a layout being rebuilt while touches are down until the new views are laid out and have been given those touches
    private var isCarryingTouches = false
    // The pointers that were down when the layout was rebuilt. Any other pointer went down after, before the new views had a place
    private val carriedPointerIds = mutableSetOf<Int>()

    fun setFrontendInputHandler(frontendInputHandler: FrontendInputHandler) {
        this.frontendInputHandler = TouchCarryingInputListener(frontendInputHandler)
        updateInputs()
    }

    fun setSystemInputHandler(systemInputHandler: IInputListener) {
        this.systemInputHandler = TouchCarryingInputListener(systemInputHandler)
        updateInputs()
    }

    /**
     * Sets where the width of the frames being drawn comes from, so that touches go where the renderer draws the bottom screen
     */
    fun setFrameWidthProvider(provider: () -> Int) {
        frameWidthProvider = provider
    }

    fun setConnectedControllersState(state: ConnectedControllersState) {
        connectedControllersState = state
        updateVisibility()
    }

    fun toggleSoftInputVisibility() {
        isSoftInputVisible = !isSoftInputVisible
        setLayoutComponentToggleState(LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT, isSoftInputVisible)
        updateVisibility()
    }

    fun isSoftInputVisible(): Boolean {
        return isSoftInputVisible
    }

    fun swapScreens() {
        areScreensSwapped = !areScreensSwapped
        setLayoutComponentToggleState(LayoutComponent.BUTTON_SWAP_SCREENS, areScreensSwapped)
        updateScreenInputs()
    }

    fun areScreensSwapped(): Boolean {
        return areScreensSwapped
    }

    fun setLayoutComponentToggleState(layoutComponent: LayoutComponent, isEnabled: Boolean) {
        val buttonView = getLayoutComponentView(layoutComponent)?.view as? RaisedButtonView ?: return
        buttonView.setToggleState(isEnabled)
    }

    fun instantiateLayout(runtimeLayout: RuntimeInputLayoutConfiguration, layoutTarget: LayoutTarget) {
        // Removing the old views cancels the touches on them, which releases what they held. Without carrying the touches over, a button held
        // while the layout changes (a script switching layouts while SELECT is held, say) would be let go, and the script would switch back
        val downPointerIds = if (isCarryingTouches) emptyList() else currentTouches?.let { getDownPointerIds(it) }.orEmpty()
        val mustCarryTouches = downPointerIds.isNotEmpty()
        if (mustCarryTouches) {
            isCarryingTouches = true
            carriedPointerIds.addAll(downPointerIds)
        }
        if (isCarryingTouches) {
            // The releases the old views make as they're removed are held back, so they shouldn't buzz
            setChildrenHapticFeedbackEnabled(false)
        }

        currentRuntimeLayout = runtimeLayout
        controllerStyle = runtimeLayout.controllerStyle
        instantiateLayout(runtimeLayout.layout, layoutTarget)
        updateInputs()
        updateVisibility()
        setLayoutComponentToggleState(LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT, isSoftInputVisible)
        setLayoutComponentToggleState(LayoutComponent.BUTTON_SWAP_SCREENS, areScreensSwapped)

        if (isCarryingTouches) {
            // Nor should the new views when they're given the touches that were already down
            setChildrenHapticFeedbackEnabled(false)
        }
        if (mustCarryTouches) {
            // The new views only have a place once they're laid out
            doOnNextLayout { finishCarryingTouches() }
        }
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                currentTouches?.recycle()
                currentTouches = null
                // Lifted before the new views were laid out: there's nothing left to carry over
                finishCarryingTouches()
            }
            else -> {
                currentTouches?.recycle()
                currentTouches = MotionEvent.obtain(event)
            }
        }

        val isHandled = super.dispatchTouchEvent(event)
        if (event.actionMasked == MotionEvent.ACTION_DOWN && !isHandled) {
            // No view took it (empty space or the top screen), so the rest of this gesture, its UP included, won't come here. It mustn't be
            // kept, or a later layout change would press whatever is there then
            currentTouches?.recycle()
            currentTouches = null
        }
        return isHandled
    }

    /**
     * Gives the touches that are still down to the new views, as if they had just been put down there, then sends the releases the old views
     * made, except for inputs the new views hold again. So an input whose button is still under the finger stays held, one whose button moved
     * away is released, and nothing is pressed just because the layout changed
     */
    private fun finishCarryingTouches() {
        if (!isCarryingTouches) {
            return
        }

        currentTouches?.let { dispatchTouchesAgain(it) }
        stopCarryingTouches()
    }

    private fun stopCarryingTouches() {
        if (!isCarryingTouches) {
            return
        }

        isCarryingTouches = false
        carriedPointerIds.clear()
        setChildrenHapticFeedbackEnabled(true)
        systemInputHandler?.sendHeldBackReleases()
        frontendInputHandler?.sendHeldBackReleases()
    }

    private fun dispatchTouchesAgain(touches: MotionEvent) {
        // A pointer going up in that event is no longer down. The carried pointers go first, so the ones that went down after the layout was
        // rebuilt can be given to the new views as the new touches they are
        val pointerIndices = getDownPointerIndices(touches).sortedBy { touches.getPointerId(it) !in carriedPointerIds }
        val properties = pointerIndices.map { index -> MotionEvent.PointerProperties().also { touches.getPointerProperties(index, it) } }
        val coordinates = pointerIndices.map { index -> MotionEvent.PointerCoords().also { touches.getPointerCoords(index, it) } }
        val eventTime = SystemClock.uptimeMillis()

        // A down for the first pointer, then a pointer down for each of the others, so each one goes to the view under it
        for (pointerCount in 1..pointerIndices.size) {
            if (properties[pointerCount - 1].id !in carriedPointerIds) {
                // The rest are new touches, so what they press is pressed (once the held back releases are sent, so they don't undo it)
                stopCarryingTouches()
            }
            val action = if (pointerCount == 1) {
                MotionEvent.ACTION_DOWN
            } else {
                MotionEvent.ACTION_POINTER_DOWN or ((pointerCount - 1) shl MotionEvent.ACTION_POINTER_INDEX_SHIFT)
            }
            val event = MotionEvent.obtain(
                touches.downTime,
                eventTime,
                action,
                pointerCount,
                properties.take(pointerCount).toTypedArray(),
                coordinates.take(pointerCount).toTypedArray(),
                touches.metaState,
                touches.buttonState,
                touches.xPrecision,
                touches.yPrecision,
                touches.deviceId,
                touches.edgeFlags,
                touches.source,
                touches.flags,
            )
            super.dispatchTouchEvent(event)
            event.recycle()
        }
    }

    private fun getDownPointerIndices(touches: MotionEvent): List<Int> {
        val liftedPointerIndex = if (touches.actionMasked == MotionEvent.ACTION_POINTER_UP) touches.actionIndex else -1
        return (0 until touches.pointerCount).filter { it != liftedPointerIndex }
    }

    private fun getDownPointerIds(touches: MotionEvent): List<Int> {
        return getDownPointerIndices(touches).map { touches.getPointerId(it) }
    }

    private fun setChildrenHapticFeedbackEnabled(isEnabled: Boolean) {
        for (index in 0 until childCount) {
            getChildAt(index).isHapticFeedbackEnabled = isEnabled
        }
    }

    private fun updateInputs() {
        val currentRuntimeLayout = currentRuntimeLayout
        if (currentRuntimeLayout == null) {
            isGone = true
            return
        }

        isVisible = true
        val inputAlpha = currentRuntimeLayout.softInputOpacity / 100f

        val enableHapticFeedback = currentRuntimeLayout.isHapticFeedbackEnabled
        systemInputHandler?.let {
            getLayoutComponentView(LayoutComponent.DPAD)?.let { dpad ->
                dpad.view.setOnTouchListener(DpadInputHandler(it, enableHapticFeedback, touchVibrator, dpad.spacing))
            }
            getLayoutComponentView(LayoutComponent.BUTTONS)?.let { buttons ->
                buttons.view.setOnTouchListener(ButtonsInputHandler(it, enableHapticFeedback, touchVibrator, buttons.spacing))
            }
            getLayoutComponentView(LayoutComponent.BUTTON_L)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.L, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_R)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.R, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_SELECT)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.SELECT, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_START)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.START, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_HINGE)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.HINGE, enableHapticFeedback, touchVibrator))
        }
        frontendInputHandler?.let {
            getLayoutComponentView(LayoutComponent.BUTTON_RESET)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.RESET, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_PAUSE)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.PAUSE, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.FAST_FORWARD, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_MICROPHONE_TOGGLE)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.MICROPHONE, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.TOGGLE_SOFT_INPUT, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_SWAP_SCREENS)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.SWAP_SCREENS, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_QUICK_SAVE)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.QUICK_SAVE, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_QUICK_LOAD)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.QUICK_LOAD, enableHapticFeedback, touchVibrator))
            getLayoutComponentView(LayoutComponent.BUTTON_REWIND)?.view?.setOnTouchListener(SingleButtonInputHandler(it, Input.REWIND, enableHapticFeedback, touchVibrator))
        }

        getLayoutComponentViews().forEach {
            if (!it.component.isScreen()) {
                it.view.apply {
                    alpha = inputAlpha
                }
            }
        }

        updateScreenInputs()
    }

    private fun updateScreenInputs() {
        val (touchScreenComponent, nonTouchScreenComponent) = if (areScreensSwapped) {
            LayoutComponent.TOP_SCREEN to LayoutComponent.BOTTOM_SCREEN
        } else {
            LayoutComponent.BOTTOM_SCREEN to LayoutComponent.TOP_SCREEN
        }
        systemInputHandler?.let {
            getLayoutComponentView(touchScreenComponent)?.view?.setOnTouchListener(TouchscreenInputHandler(it, ::getTouchScreenArea))
        }
        getLayoutComponentView(nonTouchScreenComponent)?.view?.setOnTouchListener(null)
    }

    /**
     * Where the renderer draws the bottom screen's middle SCREEN_WIDTH columns in [touchScreenView], in its coordinates. Worked out with the same
     * getScreensDrawing() and the same rects the screen gives the renderer, at the time of each touch, since the frame width can change any time
     */
    private fun getTouchScreenArea(touchScreenView: View): Rect? {
        val (topScreen, bottomScreen) = if (areScreensSwapped) {
            LayoutComponent.BOTTOM_SCREEN to LayoutComponent.TOP_SCREEN
        } else {
            LayoutComponent.TOP_SCREEN to LayoutComponent.BOTTOM_SCREEN
        }
        val bottomScreenRect = getLayoutComponentView(bottomScreen)?.getRect() ?: return null
        val drawing = getScreensDrawing(
            getLayoutComponentView(topScreen)?.getRect(),
            bottomScreenRect,
            frameWidthProvider(),
            currentRuntimeLayout?.isWideLayout == true,
            areScreensSwapped,
            width.toFloat(),
        )
        val area = drawing.touchScreenRect ?: return null
        return Rect(area.x - touchScreenView.x.toInt(), area.y - touchScreenView.y.toInt(), area.width, area.height)
    }

    private fun updateVisibility() {
        val currentConnectedControllersState = connectedControllersState
        var hiddenComponents = when(currentRuntimeLayout?.softInputBehaviour) {
            SoftInputBehaviour.ALWAYS_VISIBLE -> emptyList()
            SoftInputBehaviour.HIDE_SYSTEM_BUTTONS_WHEN_CONTROLLERS_CONNECTED, null -> {
                when(currentConnectedControllersState) {
                    ConnectedControllersState.NoControllers -> emptyList()
                    is ConnectedControllersState.ControllersConnected -> listOf(
                        LayoutComponent.BUTTONS,
                        LayoutComponent.DPAD,
                        LayoutComponent.BUTTON_L,
                        LayoutComponent.BUTTON_R,
                        LayoutComponent.BUTTON_START,
                        LayoutComponent.BUTTON_SELECT
                    )
                }
            }
            SoftInputBehaviour.HIDE_ALL_BUTTONS_ASSIGNED_TO_CONNECTED_CONTROLLERS -> when(currentConnectedControllersState) {
                ConnectedControllersState.NoControllers -> emptyList()
                is ConnectedControllersState.ControllersConnected -> {
                    LayoutComponent.entries.filter {
                        // The component can be hidden if all matching inputs are assigned to connected controllers
                        it.matchingInputs.all {
                            currentConnectedControllersState.assignedInputs.contains(it)
                        }
                    }
                }
            }
            SoftInputBehaviour.ALWAYS_INVISIBLE -> LayoutComponent.entries.toList()
        }

        if (!isSoftInputVisible) {
            // Hide everything except the soft input toggle button if it was not already hidden by the soft input behaviour logic
            hiddenComponents = if (hiddenComponents.contains(LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT)) {
                LayoutComponent.entries.toList()
            } else {
                LayoutComponent.entries.toList().filter { it != LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT}
            }
        }

        val visibleComponents = LayoutComponent.entries.filter { !hiddenComponents.contains(it) }

        hiddenComponents.forEach { component ->
            if (!component.isScreen()) {
                getLayoutComponentView(component)?.view?.isVisible = false
            }
        }
        visibleComponents.forEach { component ->
            if (!component.isScreen()) {
                getLayoutComponentView(component)?.view?.isVisible = true
            }
        }
    }

    /**
     * Passes input on to [listener], except while touches are being carried over to a new layout (see [finishCarryingTouches]). Then releases
     * are held back, and presses only cancel a held back release, so the new views press nothing new (a toggle isn't toggled again, say)
     */
    private inner class TouchCarryingInputListener(private val listener: IInputListener) : IInputListener {
        private val heldBackReleases = mutableSetOf<Input>()

        override fun onKeyPress(key: Input) {
            if (isCarryingTouches) {
                heldBackReleases.remove(key)
            } else {
                listener.onKeyPress(key)
            }
        }

        override fun onKeyReleased(key: Input) {
            if (isCarryingTouches) {
                heldBackReleases.add(key)
            } else {
                listener.onKeyReleased(key)
            }
        }

        override fun onTouch(point: Point) {
            // The touch screen's position is sent again with the next move
            if (!isCarryingTouches) {
                listener.onTouch(point)
            }
        }

        fun sendHeldBackReleases() {
            val releases = heldBackReleases.toList()
            heldBackReleases.clear()
            releases.forEach { listener.onKeyReleased(it) }
        }
    }
}
