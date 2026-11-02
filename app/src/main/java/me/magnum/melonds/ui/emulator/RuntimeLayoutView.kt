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
import me.magnum.melonds.domain.model.layout.UILayout
import me.magnum.melonds.domain.model.layout.getScreensDrawing
import me.magnum.melonds.ui.common.LayoutComponentView
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

    // The last event of the touches that are down now, kept so they can be carried over to a new layout
    private var currentTouches: MotionEvent? = null
    // True from a layout being rebuilt while touches are down until the new views are laid out and have been given those touches
    private var isCarryingTouches = false
    // The pointers that were down when the layout was rebuilt
    private val carriedPointerIds = mutableSetOf<Int>()

    // How far a shown status bar reaches down into this view
    private var topSystemBarInset = 0
    // The top of each control as the layout placed it, so the push is always worked out from there and going back to 0 puts them back exactly
    private val layoutTops = mutableMapOf<LayoutComponent, Int>()

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

    /**
     * Pushes the controls (not the screens) that would sit under the status bar down to just below it, so taps on them don't go to the system.
     */
    fun setTopSystemBarInset(inset: Int) {
        if (inset == topSystemBarInset) {
            return
        }
        topSystemBarInset = inset
        applyTopSystemBarInset()
    }

    override fun instantiateLayout(layoutConfiguration: UILayout, layoutTarget: LayoutTarget) {
        super.instantiateLayout(layoutConfiguration, layoutTarget)
        applyTopSystemBarInset()
    }

    override fun destroyLayout() {
        super.destroyLayout()
        layoutTops.clear()
    }

    override fun onLayoutComponentViewAdded(layoutComponentView: LayoutComponentView) {
        super.onLayoutComponentViewAdded(layoutComponentView)
        // The view was just made with the layout's position, before anything moved it
        (layoutComponentView.view.layoutParams as? LayoutParams)?.let {
            layoutTops[layoutComponentView.component] = it.topMargin
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (h != oldh) {
            // How far a control can be pushed depends on the height
            applyTopSystemBarInset()
        }
    }

    private fun applyTopSystemBarInset() {
        views.values.forEach { componentView ->
            if (componentView.component.isScreen()) {
                return@forEach
            }
            val layoutTop = layoutTops[componentView.component] ?: return@forEach
            val params = componentView.view.layoutParams as? LayoutParams ?: return@forEach

            // With no inset the layout's position is kept as is, even a control placed a little above the top
            var top = if (topSystemBarInset > 0) maxOf(layoutTop, topSystemBarInset) else layoutTop
            if (height > 0) {
                // Not pushed off the bottom of the view, and never moved up from where the layout put it
                top = top.coerceAtMost(height - params.height).coerceAtLeast(layoutTop)
            }
            if (params.topMargin != top) {
                params.topMargin = top
                componentView.view.layoutParams = params
            }
        }
    }

    fun instantiateLayout(runtimeLayout: RuntimeInputLayoutConfiguration, layoutTarget: LayoutTarget) {
        // Removing the old views cancels their touches, so the touches are carried over to keep held buttons held
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
            // No view took it, so the rest of this gesture won't come here, and it mustn't be kept or a later layout change would replay it
            // and press whatever is there then
            currentTouches?.recycle()
            currentTouches = null
        }
        return isHandled
    }

    /**
     * Replays the touches still down onto the new views and then sends the releases the old views made, except for inputs held again.
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
        // The carried pointers go first, so the ones that went down after the rebuild reach the new views as new touches
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
        val (nonTouchScreenComponent, touchScreenComponent) = getDrawnScreens()
        systemInputHandler?.let {
            getLayoutComponentView(touchScreenComponent)?.view?.setOnTouchListener(TouchscreenInputHandler(it, ::getTouchScreenArea))
        }
        getLayoutComponentView(nonTouchScreenComponent)?.view?.setOnTouchListener(null)
    }

    /**
     * Gives [renderer] the screens' areas, opacity and stacking as this layout shows them.
     *
     * @return the bottom screen's area for the gesture exclusion rects, empty if the layout has none.
     */
    fun applyScreenAreasTo(renderer: DSRenderer): List<android.graphics.Rect> {
        val (topScreen, bottomScreen) = getDrawnScreens()
        val topView = getLayoutComponentView(topScreen)
        val bottomView = getLayoutComponentView(bottomScreen)
        renderer.updateScreenAreas(
            topScreenRect = topView?.getRect(),
            bottomScreenRect = bottomView?.getRect(),
            topAlpha = topView?.baseAlpha ?: 1f,
            bottomAlpha = bottomView?.baseAlpha ?: 1f,
            topOnTop = topView?.onTop ?: false,
            bottomOnTop = bottomView?.onTop ?: false,
            isWideLayout = currentRuntimeLayout?.isWideLayout == true,
            areScreensSwapped = areScreensSwapped,
        )

        return bottomView?.getRect()?.let {
            listOf(android.graphics.Rect(it.x, it.y, it.right, it.bottom))
        }.orEmpty()
    }

    /**
     * The layout components the DS top and bottom screens are drawn in, in that order.
     */
    private fun getDrawnScreens(): Pair<LayoutComponent, LayoutComponent> {
        return if (areScreensSwapped) {
            LayoutComponent.BOTTOM_SCREEN to LayoutComponent.TOP_SCREEN
        } else {
            LayoutComponent.TOP_SCREEN to LayoutComponent.BOTTOM_SCREEN
        }
    }

    /**
     * Where the renderer draws the bottom screen's middle SCREEN_WIDTH columns in [touchScreenView]; must match what the renderer is given.
     */
    private fun getTouchScreenArea(touchScreenView: View): Rect? {
        val (topScreen, bottomScreen) = getDrawnScreens()
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
     * Passes input on to [listener], holding back releases while touches are carried over (see [finishCarryingTouches]) so nothing new is pressed.
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
