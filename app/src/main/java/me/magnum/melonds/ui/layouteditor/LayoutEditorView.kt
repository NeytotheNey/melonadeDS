package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.util.AttributeSet
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import me.magnum.melonds.domain.model.*
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.PositionedLayoutComponent
import me.magnum.melonds.domain.model.layout.ScreenFold
import me.magnum.melonds.domain.model.layout.UILayout
import me.magnum.melonds.domain.model.layout.WidescreenAspect
import me.magnum.melonds.domain.model.ui.Orientation
import me.magnum.melonds.ui.common.LayoutComponentView
import me.magnum.melonds.ui.common.LayoutView
import me.magnum.melonds.ui.common.view.RaisedButtons
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget
import me.magnum.melonds.impl.dpToPixels
import kotlin.math.*

private const val UNSELECTED_ALPHA = 0.5f
// The handle touch areas take up at most this much of the view's smaller side, which leaves the middle of each side free for dragging the view
private const val HANDLE_TOUCH_FRACTION = 0.25f
// Small views only get the bottom-right handle, so it can take up more of them
private const val SMALL_VIEW_HANDLE_TOUCH_FRACTION = 0.4f

class LayoutEditorView(context: Context, attrs: AttributeSet?) : LayoutView(context, attrs) {
    enum class Anchor {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT;

        val isLeft get() = this == TOP_LEFT || this == BOTTOM_LEFT
        val isTop get() = this == TOP_LEFT || this == TOP_RIGHT

        val opposite get() = when (this) {
            TOP_LEFT -> BOTTOM_RIGHT
            TOP_RIGHT -> BOTTOM_LEFT
            BOTTOM_LEFT -> TOP_RIGHT
            BOTTOM_RIGHT -> TOP_LEFT
        }

        /** The x of this corner of [rect] */
        fun xOf(rect: Rect) = if (isLeft) rect.x else rect.right

        /** The y of this corner of [rect] */
        fun yOf(rect: Rect) = if (isTop) rect.y else rect.bottom

        /**
         * Returns the rect of the given size whose corner on this side is at ([x], [y]).
         */
        fun place(x: Int, y: Int, width: Int, height: Int) = Rect(if (isLeft) x else x - width, if (isTop) y else y - height, width, height)
    }

    /**
     * How the selected screen's size is locked when it's resized.
     */
    enum class ScreenAspectRatio(val ratio: Float?) {
        RATIO_4_3(consoleAspectRatio),
        RATIO_16_9(16 / 9f),
        // The wide variant's own ratio, which only its top screen offers. See selectedScreenWideAspect. Its chip also sets the variant's ratio
        WIDE(null),
        UNRESTRICTED(null)
    }

    /**
     * A resize handle being dragged. The corner opposite to [corner] stays in place.
     */
    private class ResizeGesture(val corner: Anchor, val startRect: Rect, val downX: Float, val downY: Float, val modifiedBeforeDown: Boolean) {
        /** Whether the finger has moved past the touch slop. Nothing is resized before that */
        var isDragging = false
    }

    /**
     * The selected view's rect and spacing before the spacing slider started moving. Every new spacing is worked out from here instead of from the
     * previous step, so rounding doesn't add up. [lastRect] is the rect the slider last gave the view. If the view has been moved or resized since, the
     * baseline is taken again.
     */
    private class SpacingBaseline(val view: LayoutComponentView, val rect: Rect, val spacing: Float, var lastRect: Rect)

    private var onViewSelectedListener: ((LayoutComponentView) -> Unit)? = null
    private var onViewDeselectedListener: ((LayoutComponentView) -> Unit)? = null
    private var otherClickListener: OnClickListener? = null
    private val defaultComponentWidth = context.dpToPixels(100f).toInt()
    private val minComponentSize = context.dpToPixels(30f).toInt()
    private var selectedView: LayoutComponentView? = null
    // See reselectAfterNextLayout()
    private var componentToReselect: LayoutComponent? = null
    // The item that was selected when the layout was last shown again, until something is selected or deselected. See reselectIfDeselectedByLayout()
    private var componentDeselectedByLayout: LayoutComponent? = null
    private var selectedViewAnchor = Anchor.TOP_LEFT
    private var modifiedByUser = false
    private var resizeGesture: ResizeGesture? = null
    private var spacingBaseline: SpacingBaseline? = null
    private val updateAnchorRunnable = Runnable { updateSelectedViewAnchor() }

    /**
     * How the selected screen's size is locked. Controls always keep their own ratio.
     */
    var selectedScreenAspect = ScreenAspectRatio.RATIO_4_3

    /**
     * The wide variant's ratio while its top screen is selected, otherwise null. [ScreenAspectRatio.WIDE] locks to it.
     */
    var selectedScreenWideAspect: WidescreenAspect? = null

    /**
     * The width/height ratio kept when resizing the selected screen, or null to resize it freely.
     */
    val selectedScreenAspectRatio: Float?
        get() = if (selectedScreenAspect == ScreenAspectRatio.WIDE) selectedScreenWideAspect?.aspectRatio else selectedScreenAspect.ratio

    private val maxHandleTouchSize = context.dpToPixels(48f)
    private val minHandleTouchSize = context.dpToPixels(20f)
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop
    private val overlayDrawer = LayoutEditorOverlayDrawer(context)

    private val snapGrid = SnapGrid(context) { invalidate() }
    private var screenFolds = emptyList<ScreenFold>()

    init {
        super.setOnClickListener {
            if (selectedView != null) {
                deselectSelectedView()
            } else {
                otherClickListener?.onClick(it)
            }
        }
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        snapGrid.attach()
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        snapGrid.detach()
        removeCallbacks(updateAnchorRunnable)
    }

    /**
     * Sets the device's folds, in window coordinates. While snapping is on, their edges are drawn and count as snap lines.
     */
    fun setScreenFolds(folds: List<ScreenFold>) {
        screenFolds = folds
        invalidate()
    }

    override fun instantiateLayout(layoutConfiguration: UILayout, layoutTarget: LayoutTarget) {
        super.instantiateLayout(layoutConfiguration, layoutTarget)
        modifiedByUser = false

        componentToReselect?.let { component ->
            componentToReselect = null
            selectComponent(component)
        }
    }

    /**
     * Selects [component], if the layout shown has it. Returns whether it's selected.
     */
    fun selectComponent(component: LayoutComponent): Boolean {
        val view = views[component] ?: return false
        if (selectedView === view) return true

        deselectSelectedView()
        view.view.alpha = 1f
        view.setHighlighted(true)
        selectView(view)
        return true
    }

    /**
     * Selects [component] again once the layout is next shown, for a change made from its own controls. Null cancels that
     */
    fun reselectAfterNextLayout(component: LayoutComponent?) {
        componentToReselect = component
    }

    override fun destroyLayout() {
        // The selected view is about to be destroyed. Deselect it so that the item controls and handles don't stay on a stale view
        val deselectedComponent = selectedView?.component ?: componentDeselectedByLayout
        deselectSelectedView()
        componentDeselectedByLayout = deselectedComponent
        super.destroyLayout()
    }

    /**
     * Selects [component] again if it was only deselected because the layout was shown again, not because something else was selected or it was
     * deselected by hand since.
     */
    fun reselectIfDeselectedByLayout(component: LayoutComponent) {
        if (selectedView == null && componentDeselectedByLayout == component) {
            selectComponent(component)
        }
    }

    fun setOnViewSelectedListener(listener: (LayoutComponentView) -> Unit) {
        onViewSelectedListener = listener
    }

    fun setOnViewDeselectedListener(listener: (LayoutComponentView) -> Unit) {
        onViewDeselectedListener = listener
    }

    fun addLayoutComponent(component: LayoutComponent) {
        val componentBuilder = viewBuilderFactory.getLayoutComponentViewBuilder(component)
        val componentHeight = defaultComponentWidth / componentBuilder.getAspectRatio()
        val componentView = addPositionedLayoutComponent(PositionedLayoutComponent(Rect(0, 0, defaultComponentWidth, componentHeight.toInt()), component))
        views[component] = componentView
        modifiedByUser = true
    }

    fun isModifiedByUser(): Boolean {
        return modifiedByUser
    }

    fun buildCurrentLayout(): List<PositionedLayoutComponent> {
        // From the layout params, so a change made just before is kept even if the views haven't been laid out again yet
        return views.values.map {
            PositionedLayoutComponent(getParamsRect(it), it.component, it.baseAlpha, it.onTop, it.spacing)
        }
    }

    fun handleKeyDown(event: KeyEvent): Boolean {
        if (selectedView == null) return false

        // moveSelectedView also updates the anchor once the view has moved
        when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> moveSelectedView(0, -1)
            KeyEvent.KEYCODE_DPAD_DOWN -> moveSelectedView(0, 1)
            KeyEvent.KEYCODE_DPAD_LEFT -> moveSelectedView(-1, 0)
            KeyEvent.KEYCODE_DPAD_RIGHT -> moveSelectedView(1, 0)
            else -> return false
        }

        return true
    }

    override fun setOnClickListener(l: OnClickListener?) {
        otherClickListener = l
    }

    override fun onLayoutComponentViewAdded(layoutComponentView: LayoutComponentView) {
        super.onLayoutComponentViewAdded(layoutComponentView)
        setupDragHandler(layoutComponentView)
        layoutComponentView.view.alpha = UNSELECTED_ALPHA
        layoutComponentView.setHighlighted(false)
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        // The resize handles are drawn by this view, so redraw them whenever the selected view may have moved or changed size
        if (selectedView != null) {
            invalidate()
        }
    }

    override fun dispatchDraw(canvas: Canvas) {
        // The grid goes behind the components, the handles in front of them
        if (snapGrid.isEnabled) {
            overlayDrawer.drawGrid(canvas, width, height, snapGrid.cellSize, getFoldGaps(vertical = true), getFoldGaps(vertical = false))
        }
        super.dispatchDraw(canvas)
        val view = selectedView?.view ?: return

        val left = view.x
        val top = view.y
        val right = left + view.width
        val bottom = top + view.height
        overlayDrawer.drawHandles(canvas, left, top, right, bottom, cornerOnly = isSmallForHandles(view.width, view.height))
        if (resizeGesture?.isDragging == true) {
            overlayDrawer.drawSizeLabel(canvas, view.width, view.height, left, top, right, bottom)
        }
    }

    override fun onInterceptTouchEvent(ev: MotionEvent): Boolean {
        // Touches on the resize handles take priority over dragging the selected view itself
        if (ev.actionMasked == MotionEvent.ACTION_DOWN) {
            val view = selectedView
            val corner = view?.let { findHandleAt(it, ev.x, ev.y) }
            if (view != null && corner != null && !isInSystemGestureArea(this, ev)) {
                resizeGesture = ResizeGesture(corner, view.getRect(), ev.x, ev.y, modifiedByUser)
                invalidate()
                return true
            }
        }
        return super.onInterceptTouchEvent(ev)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val gesture = resizeGesture ?: return super.onTouchEvent(event)
        val view = selectedView
        when (event.actionMasked) {
            MotionEvent.ACTION_MOVE -> {
                val offsetX = event.x - gesture.downX
                val offsetY = event.y - gesture.downY
                if (!gesture.isDragging && isPastTouchSlop(offsetX, offsetY)) {
                    gesture.isDragging = true
                    invalidate()
                }
                if (view != null && gesture.isDragging) {
                    resizeFromHandle(view, gesture, offsetX, offsetY)
                }
            }
            MotionEvent.ACTION_UP -> {
                resizeGesture = null
                updateSelectedViewAnchorAfterLayout()
                invalidate()
            }
            MotionEvent.ACTION_CANCEL -> {
                // The gesture was taken away (usually by the system back gesture). Put the view back the way it was
                if (view != null) {
                    applyRect(view, gesture.startRect)
                }
                modifiedByUser = gesture.modifiedBeforeDown
                resizeGesture = null
                invalidate()
            }
        }
        return true
    }

    /**
     * Whether a finger has moved far enough from where it went down to start a drag or a resize.
     */
    private fun isPastTouchSlop(offsetX: Float, offsetY: Float): Boolean {
        return hypot(offsetX, offsetY) >= touchSlop
    }

    /**
     * Small views only get the bottom-right handle, so that their middle can still be grabbed to move them.
     */
    private fun isSmallForHandles(viewWidth: Int, viewHeight: Int): Boolean {
        return min(viewWidth, viewHeight) * HANDLE_TOUCH_FRACTION < minHandleTouchSize
    }

    /**
     * Returns the corner whose handle is at the given position, if any. The handle touch areas sit inside the view's corners and leave at least the
     * middle half of each side free, so the view can still be dragged to move it.
     */
    private fun findHandleAt(view: LayoutComponentView, x: Float, y: Float): Anchor? {
        val rect = view.getRect()
        if (x < rect.x || x > rect.right || y < rect.y || y > rect.bottom) {
            return null
        }

        val smallerSide = min(rect.width, rect.height)
        val (corners, touchSize) = if (isSmallForHandles(rect.width, rect.height)) {
            listOf(Anchor.BOTTOM_RIGHT) to smallerSide * SMALL_VIEW_HANDLE_TOUCH_FRACTION
        } else {
            Anchor.entries to min(maxHandleTouchSize, smallerSide * HANDLE_TOUCH_FRACTION)
        }
        return corners.firstOrNull { abs(x - it.xOf(rect)) <= touchSize && abs(y - it.yOf(rect)) <= touchSize }
    }

    private fun resizeFromHandle(view: LayoutComponentView, gesture: ResizeGesture, offsetX: Float, offsetY: Float) {
        val start = gesture.startRect
        val corner = gesture.corner
        // The opposite corner stays fixed, so the view can grow until it reaches the editor edges on the dragged corner's side
        val fixedCorner = corner.opposite
        val fixedX = fixedCorner.xOf(start)
        val fixedY = fixedCorner.yOf(start)
        var desiredWidth = start.width + if (corner.isLeft) -offsetX else offsetX
        var desiredHeight = start.height + if (corner.isTop) -offsetY else offsetY
        val maxWidth = if (corner.isLeft) fixedX else width - fixedX
        val maxHeight = if (corner.isTop) fixedY else height - fixedY

        // With a locked aspect ratio, follow whichever side the finger wants to make bigger
        val ratio = getLockedAspectRatio(view)
        if (ratio != null) {
            desiredWidth = max(desiredWidth, desiredHeight * ratio)
            desiredHeight = desiredWidth / ratio
        }
        if (snapGrid.isEnabled) {
            val (snappedWidth, snappedHeight) = snapResize(view, corner, fixedX, fixedY, desiredWidth, desiredHeight, ratio)
            desiredWidth = snappedWidth
            desiredHeight = snappedHeight
        }
        val (newWidth, newHeight) = constrainSize(view, desiredWidth, desiredHeight, maxWidth, maxHeight)
        applyRect(view, fixedCorner.place(fixedX, fixedY, newWidth, newHeight))
    }

    /**
     * Snaps the dragged corner of a resize to the nearest lines, while the opposite corner stays at [fixedX], [fixedY]. With a locked aspect ratio
     * only the dragged edge that's closer to a line snaps, and the other side follows the ratio.
     */
    private fun snapResize(view: LayoutComponentView, corner: Anchor, fixedX: Int, fixedY: Int, desiredWidth: Float, desiredHeight: Float, ratio: Float?): Pair<Float, Float> {
        val (xAxis, yAxis) = getSnapAxes(view)
        // The desired size can be negative when the finger goes past the fixed corner, so the side always comes from the corner being dragged
        val edgeX = if (corner.isLeft) fixedX - desiredWidth else fixedX + desiredWidth
        val edgeY = if (corner.isTop) fixedY - desiredHeight else fixedY + desiredHeight
        // Same minimum as constrainSize, so a snapped size isn't bumped off its line again
        val minWidth = getMinWidth(ratio)
        val minHeight = ratio?.let { minWidth / it } ?: minComponentSize.toFloat()
        val snappedX = LayoutEditorSnapping.snapResizedEdge(edgeX.roundToInt(), fixedX, ceil(minWidth).toInt(), !corner.isLeft, xAxis)
        val snappedY = LayoutEditorSnapping.snapResizedEdge(edgeY.roundToInt(), fixedY, ceil(minHeight).toInt(), !corner.isTop, yAxis)
        val snappedWidth = (if (corner.isLeft) fixedX - snappedX.position else snappedX.position - fixedX).toFloat()
        val snappedHeight = (if (corner.isTop) fixedY - snappedY.position else snappedY.position - fixedY).toFloat()
        return when {
            ratio == null -> snappedWidth to snappedHeight
            snappedX.distance <= snappedY.distance -> snappedWidth to snappedWidth / ratio
            else -> snappedHeight * ratio to snappedHeight
        }
    }

    /**
     * Snaps an item that was moved, or resized to an explicit size: controls put their centre on the grid, screens put their nearest edge on a
     * line.
     */
    private fun snapPosition(view: LayoutComponentView, rect: Rect, editorWidth: Int = width, editorHeight: Int = height): Rect {
        val (xAxis, yAxis) = getSnapAxes(view, editorWidth, editorHeight)
        return if (view.component.isScreen()) {
            rect.copy(x = LayoutEditorSnapping.snapEdges(rect.x, rect.width, xAxis), y = LayoutEditorSnapping.snapEdges(rect.y, rect.height, yAxis))
        } else {
            rect.copy(x = LayoutEditorSnapping.snapCenter(rect.x, rect.width, xAxis), y = LayoutEditorSnapping.snapCenter(rect.y, rect.height, yAxis))
        }
    }

    /**
     * Returns the snap lines for the given item along the x and y axes. A screen can also line up with the other screen.
     */
    private fun getSnapAxes(view: LayoutComponentView, editorWidth: Int = width, editorHeight: Int = height): Pair<SnapAxis, SnapAxis> {
        val otherScreen = getOtherScreen(view.component)?.getRect()
        val xAxis = SnapAxis(editorWidth, snapGrid.cellSize, getFoldGaps(vertical = true), otherScreen?.let { SnapSpan(it.x, it.width) })
        val yAxis = SnapAxis(editorHeight, snapGrid.cellSize, getFoldGaps(vertical = false), otherScreen?.let { SnapSpan(it.y, it.height) })
        return xAxis to yAxis
    }

    /**
     * Returns the view of the other screen when [component] is a screen, or null.
     */
    private fun getOtherScreen(component: LayoutComponent): LayoutComponentView? {
        return when (component) {
            LayoutComponent.TOP_SCREEN -> views[LayoutComponent.BOTTOM_SCREEN]
            LayoutComponent.BOTTOM_SCREEN -> views[LayoutComponent.TOP_SCREEN]
            else -> null
        }
    }

    /**
     * Returns every fold that runs in the given direction, in editor coordinates. Vertical folds give spans along x, horizontal ones along y.
     */
    private fun getFoldGaps(vertical: Boolean): List<SnapSpan> {
        if (screenFolds.isEmpty()) return emptyList()

        // The fold bounds are in window coordinates
        val location = IntArray(2)
        getLocationInWindow(location)
        val limit = if (vertical) width else height
        return screenFolds.mapNotNull {
            // A landscape fold splits the screen into a top and a bottom half, so its line is horizontal
            val isVerticalFold = it.orientation == Orientation.PORTRAIT
            when {
                isVerticalFold != vertical -> null
                vertical -> SnapSpan(it.foldBounds.x - location[0], it.foldBounds.width)
                else -> SnapSpan(it.foldBounds.y - location[1], it.foldBounds.height)
            }
        }.filter { it.end >= 0 && it.start <= limit }
    }

    private fun getLockedAspectRatio(view: LayoutComponentView): Float? {
        return if (view.component.isScreen()) selectedScreenAspectRatio else view.aspectRatio
    }

    /**
     * Clamps a size to the minimum component size and to the given maximum. With a locked aspect ratio the height is derived from the width.
     */
    private fun constrainSize(view: LayoutComponentView, desiredWidth: Float, desiredHeight: Float, maxWidth: Int, maxHeight: Int): Size {
        return constrainSize(getLockedAspectRatio(view), desiredWidth, desiredHeight, maxWidth, maxHeight)
    }

    private fun constrainSize(ratio: Float?, desiredWidth: Float, desiredHeight: Float, maxWidth: Int, maxHeight: Int): Size {
        return if (ratio == null) {
            val newWidth = desiredWidth.roundToInt().coerceAtMost(maxWidth).coerceAtLeast(minComponentSize)
            val newHeight = desiredHeight.roundToInt().coerceAtMost(maxHeight).coerceAtLeast(minComponentSize)
            Size(newWidth, newHeight)
        } else {
            val minWidth = getMinWidth(ratio)
            val maxRatioWidth = min(maxWidth.toFloat(), maxHeight * ratio)
            val newWidth = desiredWidth.coerceAtMost(maxRatioWidth).coerceAtLeast(minWidth)
            Size(newWidth.roundToInt(), (newWidth / ratio).roundToInt())
        }
    }

    /**
     * Returns the smallest width an item can have. With a locked [ratio], it's the smallest that keeps both sides at least the minimum component size.
     */
    private fun getMinWidth(ratio: Float?): Float {
        return if (ratio == null) minComponentSize.toFloat() else max(minComponentSize.toFloat(), minComponentSize * ratio)
    }

    /**
     * Returns the ranges of widths and heights the selected view can have inside the editor, taking its locked aspect ratio into account.
     */
    fun getSelectedViewSizeRanges(): Pair<IntRange, IntRange> {
        val view = selectedView ?: return IntRange.EMPTY to IntRange.EMPTY
        val (minWidth, minHeight) = constrainSize(view, 0f, 0f, width, height)
        val (maxWidth, maxHeight) = constrainSize(view, width.toFloat(), height.toFloat(), width, height)
        return minWidth..max(minWidth, maxWidth) to minHeight..max(minHeight, maxHeight)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupDragHandler(layoutComponentView: LayoutComponentView) {
        layoutComponentView.view.setOnTouchListener(object : OnTouchListener {
            private var dragging = false

            // Where the finger went down, in editor coordinates, and where the view was then. A drag moves the view from there by the finger's
            // total offset, so a snapped view can leave its line again
            private var downX = -1f
            private var downY = -1f
            private var startRect: Rect? = null
            private var modifiedBeforeDown = false

            override fun onTouch(view: View?, motionEvent: MotionEvent?): Boolean {
                if (view == null || motionEvent == null)
                    return false

                // Let touches that start in the system gesture area (e.g. the back gesture) go through to the system instead of grabbing the view
                if (motionEvent.actionMasked == MotionEvent.ACTION_DOWN && isInSystemGestureArea(view, motionEvent)) {
                    return false
                }

                if (selectedView != null) {
                    deselectSelectedView()
                }

                return when (motionEvent.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        dragging = false
                        downX = view.x + motionEvent.x
                        downY = view.y + motionEvent.y
                        startRect = layoutComponentView.getRect()
                        modifiedBeforeDown = modifiedByUser
                        view.alpha = 1f
                        layoutComponentView.setHighlighted(true)
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        // The event is relative to the view, which moves during the drag, so work in editor coordinates instead
                        val offsetX = view.x + motionEvent.x - downX
                        val offsetY = view.y + motionEvent.y - downY
                        val start = startRect
                        if (!dragging) {
                            if (isPastTouchSlop(offsetX, offsetY)) {
                                dragging = true
                            }
                        } else if (start != null) {
                            dragView(layoutComponentView, start, offsetX, offsetY)
                        }
                        true
                    }
                    MotionEvent.ACTION_UP -> {
                        if (!dragging) {
                            selectView(layoutComponentView)
                        } else {
                            view.alpha = UNSELECTED_ALPHA
                            layoutComponentView.setHighlighted(false)
                            dragging = false
                        }
                        true
                    }
                    MotionEvent.ACTION_CANCEL -> {
                        // The gesture was taken away (usually by the system back gesture). Undo the drag and don't select the view
                        if (dragging) {
                            startRect?.let { applyRect(layoutComponentView, it) }
                            modifiedByUser = modifiedBeforeDown
                        }
                        view.alpha = UNSELECTED_ALPHA
                        layoutComponentView.setHighlighted(false)
                        dragging = false
                        true
                    }
                    else -> false
                }
            }
        })
    }

    private fun isInSystemGestureArea(view: View, motionEvent: MotionEvent): Boolean {
        val gestureInsets = ViewCompat.getRootWindowInsets(this)?.getInsets(WindowInsetsCompat.Type.systemGestures()) ?: return false
        if (gestureInsets.left == 0 && gestureInsets.right == 0) {
            return false
        }

        // Convert the touch position to window coordinates, which is what the insets are relative to
        val viewLocation = IntArray(2)
        view.getLocationInWindow(viewLocation)
        val touchX = viewLocation[0] + motionEvent.x
        return touchX < gestureInsets.left || touchX > rootView.width - gestureInsets.right
    }

    private fun selectView(view: LayoutComponentView) {
        selectedView = view
        componentDeselectedByLayout = null
        spacingBaseline = null
        updateSelectedViewAnchor()
        invalidate()
        onViewSelectedListener?.invoke(view)
    }

    /**
     * Anchors the selected view to the editor corner closest to it. Resizing from the controls keeps that corner of the view in place.
     */
    private fun updateSelectedViewAnchor() {
        val view = selectedView ?: return
        selectedViewAnchor = getNearestAnchor(view.getRect(), width, height)
    }

    /**
     * Returns the corner of an editor of the given size that's closest to [rect].
     */
    private fun getNearestAnchor(rect: Rect, editorWidth: Int, editorHeight: Int): Anchor {
        val distanceLeft = rect.x.toDouble()
        val distanceRight = (editorWidth - rect.right).toDouble()
        val distanceTop = rect.y.toDouble()
        val distanceBottom = (editorHeight - rect.bottom).toDouble()

        val anchorDistances = mapOf(
            Anchor.TOP_LEFT to distanceLeft.pow(2) + distanceTop.pow(2),
            Anchor.TOP_RIGHT to distanceRight.pow(2) + distanceTop.pow(2),
            Anchor.BOTTOM_LEFT to distanceLeft.pow(2) + distanceBottom.pow(2),
            Anchor.BOTTOM_RIGHT to distanceRight.pow(2) + distanceBottom.pow(2),
        )
        return anchorDistances.minBy { it.value }.key
    }

    /**
     * Updates the anchor once the selected view has been moved. Its bounds only change on the next layout pass.
     */
    private fun updateSelectedViewAnchorAfterLayout() {
        removeCallbacks(updateAnchorRunnable)
        post(updateAnchorRunnable)
    }

    fun deselectSelectedView() {
        selectedView?.let {
            it.view.alpha = UNSELECTED_ALPHA
            it.setHighlighted(false)
            onViewDeselectedListener?.invoke(it)
        }
        selectedView = null
        componentDeselectedByLayout = null
        resizeGesture = null
        spacingBaseline = null
        invalidate()
    }

    fun deleteSelectedView() {
        val currentlySelectedView = selectedView ?: return
        removeView(currentlySelectedView.view)
        views.remove(currentlySelectedView.component)
        deselectSelectedView()
        modifiedByUser = true
    }

    /**
     * Moves a view that's being dragged by touch to [startRect] moved by the finger's total offset. With snapping on, that unsnapped position is
     * what gets snapped, so the view doesn't get stuck on a line once it's on it.
     */
    private fun dragView(view: LayoutComponentView, startRect: Rect, offsetX: Float, offsetY: Float) {
        val rect = clampInside(startRect.copy(x = (startRect.x + offsetX).toInt(), y = (startRect.y + offsetY).toInt()))
        applyRect(view, if (snapGrid.isEnabled) snapPosition(view, rect) else rect)
    }

    /**
     * Moves a view by the given offset, without snapping. Used by the nudge arrows and the D-pad, which are for fine adjustments.
     */
    private fun moveView(view: LayoutComponentView, offsetX: Int, offsetY: Int) {
        val rect = view.getRect()
        applyRect(view, clampInside(rect.copy(x = rect.x + offsetX, y = rect.y + offsetY)))
    }

    fun moveSelectedView(offsetX: Int, offsetY: Int) {
        val view = selectedView ?: return
        moveView(view, offsetX, offsetY)
        updateSelectedViewAnchorAfterLayout()
    }

    /**
     * Moves a rect back inside the editor. One that's bigger than the editor sticks out on the left or top.
     */
    private fun clampInside(rect: Rect, editorWidth: Int = width, editorHeight: Int = height): Rect {
        return rect.copy(
            x = rect.x.coerceAtLeast(0).coerceAtMost(editorWidth - rect.width),
            y = rect.y.coerceAtLeast(0).coerceAtMost(editorHeight - rect.height),
        )
    }

    fun setSelectedViewAlpha(alpha: Float) {
        selectedView?.let {
            it.baseAlpha = alpha
            modifiedByUser = true
        }
    }

    /**
     * Sets the spacing of the selected D-pad or face buttons and redraws them straight away. The view grows or shrinks around its centre, so the buttons
     * keep their size on screen and only the gaps between them change. It's moved back inside the editor if it would stick out, and if it doesn't fit at
     * all it's capped at the editor's size, which is the only case where the buttons get smaller.
     */
    fun setSelectedViewSpacing(spacing: Float) {
        val view = selectedView ?: return
        // The layout params are up to date even before the next layout pass, which the slider doesn't wait for
        val currentRect = getParamsRect(view)
        val baseline = spacingBaseline?.takeIf { it.view === view && it.lastRect == currentRect } ?: SpacingBaseline(view, currentRect, view.spacing, currentRect)
        spacingBaseline = baseline

        val growth = RaisedButtons.spacedDesignSize(spacing) / RaisedButtons.spacedDesignSize(baseline.spacing)
        var newWidth = baseline.rect.width * growth
        var newHeight = baseline.rect.height * growth
        val fitScale = min(1f, min(width / newWidth, height / newHeight))
        newWidth = max(newWidth * fitScale, minComponentSize.toFloat())
        newHeight = max(newHeight * fitScale, minComponentSize.toFloat())
        val roundedWidth = newWidth.roundToInt()
        val roundedHeight = newHeight.roundToInt()
        val centerX = baseline.rect.x + baseline.rect.width / 2f
        val centerY = baseline.rect.y + baseline.rect.height / 2f
        val newRect = clampInside(Rect((centerX - roundedWidth / 2f).roundToInt(), (centerY - roundedHeight / 2f).roundToInt(), roundedWidth, roundedHeight))

        view.spacing = spacing
        viewBuilderFactory.getLayoutComponentViewBuilder(view.component).applySpacing(view.view, spacing)
        applyRect(view, newRect)
        baseline.lastRect = newRect
        modifiedByUser = true
        updateSelectedViewAnchorAfterLayout()
    }

    fun setSelectedScreenOnTop(onTop: Boolean) {
        selectedView?.let {
            it.onTop = onTop
            // Only one screen can be above the other
            if (onTop) {
                getOtherScreen(it.component)?.onTop = false
            }
            rearrangeScreens()
            modifiedByUser = true
        }
    }

    private fun rearrangeScreens() {
        val topScreen = views[LayoutComponent.TOP_SCREEN]
        val bottomScreen = views[LayoutComponent.BOTTOM_SCREEN]
        if (topScreen != null && bottomScreen != null) {
            removeView(topScreen.view)
            removeView(bottomScreen.view)
            // Each screen goes in at index 0, so the one added last is drawn underneath. Same stacking as DSRenderer: the bottom screen is only drawn
            // over the top one when it's marked as on top
            if (bottomScreen.onTop) {
                addView(bottomScreen.view, 0)
                addView(topScreen.view, 0)
            } else {
                addView(topScreen.view, 0)
                addView(bottomScreen.view, 0)
            }
        }
    }

    fun centerSelectedViewHorizontally() {
        val view = selectedView ?: return
        val rect = view.getRect()
        applyRect(view, rect.copy(x = LayoutEditorSnapping.centeredStart(rect.width, width)))
        updateSelectedViewAnchorAfterLayout()
    }

    fun centerSelectedViewVertically() {
        val view = selectedView ?: return
        val rect = view.getRect()
        applyRect(view, rect.copy(y = LayoutEditorSnapping.centeredStart(rect.height, height)))
        updateSelectedViewAnchorAfterLayout()
    }

    /**
     * Resizes the selected view, keeping its anchor corner in place and the view inside the editor. With a locked aspect ratio the height is
     * derived from the width. The size isn't snapped, since it was asked for explicitly, but with snapping on the position is snapped again.
     */
    fun resizeSelectedView(newWidth: Float, newHeight: Float) {
        val view = selectedView ?: return
        resizeView(view, newWidth, newHeight, getLockedAspectRatio(view), selectedViewAnchor, width, height)
    }

    /**
     * Resizes [view] to a size entered in the value dialog, whether it's selected or not. Its nearest corner stays in place. [screenAspectRatio] is
     * the ratio a screen was locked to when the value was entered (controls keep their own). The size and position are worked out for an editor of
     * [editorWidth] by [editorHeight], the size the layout shown was picked for, which the editor may not be back to yet.
     */
    fun resizeView(view: LayoutComponentView, newWidth: Float, newHeight: Float, screenAspectRatio: Float?, editorWidth: Int, editorHeight: Int) {
        val ratio = if (view.component.isScreen()) screenAspectRatio else view.aspectRatio
        resizeView(view, newWidth, newHeight, ratio, getNearestAnchor(view.getRect(), editorWidth, editorHeight), editorWidth, editorHeight)
    }

    private fun resizeView(view: LayoutComponentView, newWidth: Float, newHeight: Float, ratio: Float?, anchor: Anchor, editorWidth: Int, editorHeight: Int) {
        val (constrainedWidth, constrainedHeight) = constrainSize(ratio, newWidth, newHeight, editorWidth, editorHeight)
        val rect = view.getRect()
        val newRect = clampInside(anchor.place(anchor.xOf(rect), anchor.yOf(rect), constrainedWidth, constrainedHeight), editorWidth, editorHeight)
        applyRect(view, if (snapGrid.isEnabled) snapPosition(view, newRect, editorWidth, editorHeight) else newRect)
    }

    /**
     * Grows or shrinks the selected view by the given width. The height changes in proportion, and both sides stop together when either of them
     * reaches the minimum size or the editor bounds.
     */
    fun resizeSelectedViewBy(widthOffset: Int) {
        val view = selectedView ?: return
        val currentWidth = view.getWidth()
        val currentHeight = view.getHeight()
        if (currentWidth <= 0 || currentHeight <= 0 || widthOffset == 0) return

        if (getLockedAspectRatio(view) != null) {
            resizeSelectedView((currentWidth + widthOffset).toFloat(), 0f)
            return
        }

        val desiredScale = (currentWidth + widthOffset) / currentWidth.toFloat()
        val maxScale = min(width / currentWidth.toFloat(), height / currentHeight.toFloat())
        val minScale = max(minComponentSize / currentWidth.toFloat(), minComponentSize / currentHeight.toFloat())
        val scale = desiredScale.coerceAtMost(maxScale).coerceAtLeast(minScale)
        // No room left in the requested direction
        if ((widthOffset > 0 && scale <= 1f) || (widthOffset < 0 && scale >= 1f)) return

        resizeSelectedView(currentWidth * scale, currentHeight * scale)
    }

    private fun applyRect(view: LayoutComponentView, rect: Rect) {
        // Compare with the layout params instead of the view's bounds, since those only update on the next layout pass
        if (getParamsRect(view) == rect) {
            return
        }
        view.setPositionAndSize(Point(rect.x, rect.y), rect.width, rect.height)
        modifiedByUser = true
    }

    /**
     * Returns the view's rect as set in its layout params, which already hold a new position and size before the next layout pass.
     */
    private fun getParamsRect(view: LayoutComponentView): Rect {
        val params = view.view.layoutParams as LayoutParams
        return Rect(params.leftMargin, params.topMargin, params.width, params.height)
    }
}
