package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.util.TypedValue
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import androidx.core.content.ContextCompat
import androidx.core.graphics.ColorUtils
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.*
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.PositionedLayoutComponent
import me.magnum.melonds.domain.model.layout.ScreenFold
import me.magnum.melonds.domain.model.layout.UILayout
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
private const val MIN_GRID_CELL_PX = 4

class LayoutEditorView(context: Context, attrs: AttributeSet?) : LayoutView(context, attrs) {
    enum class Anchor {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT;

        val isLeft get() = this == TOP_LEFT || this == BOTTOM_LEFT
        val isTop get() = this == TOP_LEFT || this == TOP_RIGHT
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
    val minComponentSize = context.dpToPixels(30f).toInt()
    private var selectedView: LayoutComponentView? = null
    // See reselectAfterNextLayout()
    private var componentToReselect: LayoutComponent? = null
    private var selectedViewAnchor = Anchor.TOP_LEFT
    private var modifiedByUser = false
    private var resizeGesture: ResizeGesture? = null
    private var spacingBaseline: SpacingBaseline? = null
    private val updateAnchorRunnable = Runnable { updateSelectedViewAnchor() }

    /**
     * The width/height ratio kept when resizing the selected screen, or null to resize it freely. Controls always keep their own ratio.
     */
    var selectedScreenAspectRatio: Float? = null

    private val handleSize = context.dpToPixels(10f)
    private val cornerHandleSize = context.dpToPixels(18f)
    private val maxHandleTouchSize = context.dpToPixels(48f)
    private val minHandleTouchSize = context.dpToPixels(20f)
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop
    private val handleRect = RectF()
    private val handleFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private val handleStrokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0x99000000.toInt()
        strokeWidth = context.dpToPixels(1f)
    }
    private val resizeIcon = ContextCompat.getDrawable(context, R.drawable.ic_open_in_full)?.mutate()?.apply { setTint(Color.BLACK) }
    private val sizeLabelTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.textLayoutEditor)
        textSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 14f, resources.displayMetrics)
        textAlign = Paint.Align.CENTER
    }
    private val sizeLabelBackgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = ContextCompat.getColor(context, R.color.layoutEditorPanelBackground) }
    private val sizeLabelPadding = context.dpToPixels(8f)

    private val gridSettings = LayoutEditorGridSettings(context)
    private var gridSettingsListener: SharedPreferences.OnSharedPreferenceChangeListener? = null
    private var isSnapEnabled = false
    private var gridCellSize = 1
    private var screenFolds = emptyList<ScreenFold>()
    // The editor background is always black, whatever the theme, so the grid is always light
    private val gridPaint = Paint().apply {
        color = ColorUtils.setAlphaComponent(Color.WHITE, 0x50)
        strokeWidth = max(1f, context.dpToPixels(1f))
    }
    private val foldPaint = Paint().apply {
        color = ColorUtils.setAlphaComponent(Color.WHITE, 0xB0)
        strokeWidth = max(1f, context.dpToPixels(2f))
    }

    init {
        super.setOnClickListener {
            if (selectedView != null) {
                deselectSelectedView()
            } else {
                otherClickListener?.onClick(it)
            }
        }
        loadGridSettings()
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        // The setting can also be changed from the editor on the other display, so follow every change
        gridSettingsListener = gridSettings.addListener { loadGridSettings() }
        loadGridSettings()
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        gridSettingsListener?.let { gridSettings.removeListener(it) }
        gridSettingsListener = null
        removeCallbacks(updateAnchorRunnable)
    }

    private fun loadGridSettings() {
        isSnapEnabled = gridSettings.isSnapEnabled
        gridCellSize = max(MIN_GRID_CELL_PX, context.dpToPixels(gridSettings.gridSize.dp.toFloat()).roundToInt())
        invalidate()
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
            views[component]?.let {
                it.view.alpha = 1f
                it.setHighlighted(true)
                selectView(it)
            }
        }
    }

    /**
     * Selects [component] again once the layout is next shown, for a change made from its own controls. Null cancels that
     */
    fun reselectAfterNextLayout(component: LayoutComponent?) {
        componentToReselect = component
    }

    override fun destroyLayout() {
        // The selected view is about to be destroyed. Deselect it so that the item controls and handles don't stay on a stale view
        deselectSelectedView()
        super.destroyLayout()
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
        return views.values.map {
            PositionedLayoutComponent(it.getRect(), it.component, it.baseAlpha, it.onTop, it.spacing)
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
        if (isSnapEnabled) {
            drawGrid(canvas)
        }
        super.dispatchDraw(canvas)
        val view = selectedView?.view ?: return

        val left = view.x
        val top = view.y
        val right = left + view.width
        val bottom = top + view.height
        if (!isSmallForHandles(view.width, view.height)) {
            drawHandle(canvas, left, top, left + handleSize, top + handleSize)
            drawHandle(canvas, right - handleSize, top, right, top + handleSize)
            drawHandle(canvas, left, bottom - handleSize, left + handleSize, bottom)
        }
        drawHandle(canvas, right - cornerHandleSize, bottom - cornerHandleSize, right, bottom)
        resizeIcon?.let {
            val inset = context.dpToPixels(2f)
            it.setBounds((right - cornerHandleSize + inset).toInt(), (bottom - cornerHandleSize + inset).toInt(), (right - inset).toInt(), (bottom - inset).toInt())
            it.draw(canvas)
        }

        if (resizeGesture?.isDragging == true) {
            val label = resources.getString(R.string.layout_editor_size_value, view.width, view.height)
            val centerX = (left + right) / 2f
            val centerY = (top + bottom) / 2f
            val textWidth = sizeLabelTextPaint.measureText(label)
            val fontMetrics = sizeLabelTextPaint.fontMetrics
            val textHeight = fontMetrics.descent - fontMetrics.ascent
            handleRect.set(
                centerX - textWidth / 2f - sizeLabelPadding,
                centerY - textHeight / 2f - sizeLabelPadding / 2f,
                centerX + textWidth / 2f + sizeLabelPadding,
                centerY + textHeight / 2f + sizeLabelPadding / 2f,
            )
            canvas.drawRoundRect(handleRect, sizeLabelPadding, sizeLabelPadding, sizeLabelBackgroundPaint)
            canvas.drawText(label, centerX, centerY - (fontMetrics.ascent + fontMetrics.descent) / 2f, sizeLabelTextPaint)
        }
    }

    private fun drawGrid(canvas: Canvas) {
        val viewWidth = width.toFloat()
        val viewHeight = height.toFloat()
        // The same lines the snapping uses: from the middle of the editor out, plus the editor edges
        LayoutEditorSnapping.gridLines(width, gridCellSize).forEach {
            val x = insetLine(it, width, gridPaint)
            canvas.drawLine(x, 0f, x, viewHeight, gridPaint)
        }
        LayoutEditorSnapping.gridLines(height, gridCellSize).forEach {
            val y = insetLine(it, height, gridPaint)
            canvas.drawLine(0f, y, viewWidth, y, gridPaint)
        }
        getFoldGaps(vertical = true).flatMap { listOf(it.start, it.end) }.filter { it in 0..width }.distinct().forEach {
            val x = insetLine(it, width, foldPaint)
            canvas.drawLine(x, 0f, x, viewHeight, foldPaint)
        }
        getFoldGaps(vertical = false).flatMap { listOf(it.start, it.end) }.filter { it in 0..height }.distinct().forEach {
            val y = insetLine(it, height, foldPaint)
            canvas.drawLine(0f, y, viewWidth, y, foldPaint)
        }
    }

    /**
     * Moves lines at the editor edges inwards by half the stroke, so they aren't half cut off.
     */
    private fun insetLine(position: Int, limit: Int, paint: Paint): Float {
        val halfStroke = paint.strokeWidth / 2f
        return position.toFloat().coerceIn(halfStroke, max(halfStroke, limit - halfStroke))
    }

    private fun drawHandle(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        handleRect.set(left, top, right, bottom)
        canvas.drawRect(handleRect, handleFillPaint)
        canvas.drawRect(handleRect, handleStrokePaint)
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
                if (!gesture.isDragging && hypot(offsetX, offsetY) >= touchSlop) {
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
        return corners.firstOrNull {
            val cornerX = if (it.isLeft) rect.x else rect.right
            val cornerY = if (it.isTop) rect.y else rect.bottom
            abs(x - cornerX) <= touchSize && abs(y - cornerY) <= touchSize
        }
    }

    private fun resizeFromHandle(view: LayoutComponentView, gesture: ResizeGesture, offsetX: Float, offsetY: Float) {
        val start = gesture.startRect
        val corner = gesture.corner
        // The opposite corner stays fixed, so the view can grow until it reaches the editor edges on the dragged corner's side
        val fixedX = if (corner.isLeft) start.right else start.x
        val fixedY = if (corner.isTop) start.bottom else start.y
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
        if (isSnapEnabled) {
            val (snappedWidth, snappedHeight) = snapResize(view, corner, fixedX, fixedY, desiredWidth, desiredHeight, ratio)
            desiredWidth = snappedWidth
            desiredHeight = snappedHeight
        }
        val (newWidth, newHeight) = constrainSize(view, desiredWidth, desiredHeight, maxWidth, maxHeight)
        val x = if (corner.isLeft) fixedX - newWidth else fixedX
        val y = if (corner.isTop) fixedY - newHeight else fixedY
        applyRect(view, Rect(x, y, newWidth, newHeight))
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
        val minWidth = ratio?.let { max(minComponentSize.toFloat(), minComponentSize * it) } ?: minComponentSize.toFloat()
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
    private fun snapPosition(view: LayoutComponentView, rect: Rect): Rect {
        val (xAxis, yAxis) = getSnapAxes(view)
        return if (view.component.isScreen()) {
            rect.copy(x = LayoutEditorSnapping.snapEdges(rect.x, rect.width, xAxis), y = LayoutEditorSnapping.snapEdges(rect.y, rect.height, yAxis))
        } else {
            rect.copy(x = LayoutEditorSnapping.snapCenter(rect.x, rect.width, xAxis), y = LayoutEditorSnapping.snapCenter(rect.y, rect.height, yAxis))
        }
    }

    /**
     * Returns the snap lines for the given item along the x and y axes. A screen can also line up with the other screen.
     */
    private fun getSnapAxes(view: LayoutComponentView): Pair<SnapAxis, SnapAxis> {
        val otherScreen = when (view.component) {
            LayoutComponent.TOP_SCREEN -> views[LayoutComponent.BOTTOM_SCREEN]
            LayoutComponent.BOTTOM_SCREEN -> views[LayoutComponent.TOP_SCREEN]
            else -> null
        }?.getRect()
        val xAxis = SnapAxis(width, gridCellSize, getFoldGaps(vertical = true), otherScreen?.let { SnapSpan(it.x, it.width) })
        val yAxis = SnapAxis(height, gridCellSize, getFoldGaps(vertical = false), otherScreen?.let { SnapSpan(it.y, it.height) })
        return xAxis to yAxis
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
    private fun constrainSize(view: LayoutComponentView, desiredWidth: Float, desiredHeight: Float, maxWidth: Int, maxHeight: Int): Pair<Int, Int> {
        val ratio = getLockedAspectRatio(view)
        return if (ratio == null) {
            val newWidth = desiredWidth.roundToInt().coerceAtMost(maxWidth).coerceAtLeast(minComponentSize)
            val newHeight = desiredHeight.roundToInt().coerceAtMost(maxHeight).coerceAtLeast(minComponentSize)
            newWidth to newHeight
        } else {
            val minWidth = max(minComponentSize.toFloat(), minComponentSize * ratio)
            val maxRatioWidth = min(maxWidth.toFloat(), maxHeight * ratio)
            val newWidth = desiredWidth.coerceAtMost(maxRatioWidth).coerceAtLeast(minWidth)
            newWidth.roundToInt() to (newWidth / ratio).roundToInt()
        }
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
                            if (hypot(offsetX, offsetY) >= touchSlop) {
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
        val rect = view.getRect()
        val distanceLeft = rect.x.toDouble()
        val distanceRight = (width - rect.right).toDouble()
        val distanceTop = rect.y.toDouble()
        val distanceBottom = (height - rect.bottom).toDouble()

        val anchorDistances = mapOf(
            Anchor.TOP_LEFT to distanceLeft.pow(2) + distanceTop.pow(2),
            Anchor.TOP_RIGHT to distanceRight.pow(2) + distanceTop.pow(2),
            Anchor.BOTTOM_LEFT to distanceLeft.pow(2) + distanceBottom.pow(2),
            Anchor.BOTTOM_RIGHT to distanceRight.pow(2) + distanceBottom.pow(2),
        )
        selectedViewAnchor = anchorDistances.minBy { it.value }.key
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
        applyRect(view, if (isSnapEnabled) snapPosition(view, rect) else rect)
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
    private fun clampInside(rect: Rect): Rect {
        return rect.copy(
            x = rect.x.coerceAtLeast(0).coerceAtMost(width - rect.width),
            y = rect.y.coerceAtLeast(0).coerceAtMost(height - rect.height),
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
                val otherScreen = when (it.component) {
                    LayoutComponent.TOP_SCREEN -> views[LayoutComponent.BOTTOM_SCREEN]
                    LayoutComponent.BOTTOM_SCREEN -> views[LayoutComponent.TOP_SCREEN]
                    else -> null
                }
                otherScreen?.onTop = false
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
        applyRect(view, rect.copy(x = (width - rect.width) / 2))
        updateSelectedViewAnchorAfterLayout()
    }

    fun centerSelectedViewVertically() {
        val view = selectedView ?: return
        val rect = view.getRect()
        applyRect(view, rect.copy(y = (height - rect.height) / 2))
        updateSelectedViewAnchorAfterLayout()
    }

    /**
     * Resizes the selected view, keeping its anchor corner in place and the view inside the editor. With a locked aspect ratio the height is
     * derived from the width. The size isn't snapped, since it was asked for explicitly, but with snapping on the position is snapped again.
     */
    fun resizeSelectedView(newWidth: Float, newHeight: Float) {
        val view = selectedView ?: return
        val (constrainedWidth, constrainedHeight) = constrainSize(view, newWidth, newHeight, width, height)
        val rect = view.getRect()

        val x = if (selectedViewAnchor.isLeft) rect.x else rect.right - constrainedWidth
        val y = if (selectedViewAnchor.isTop) rect.y else rect.bottom - constrainedHeight
        val newRect = clampInside(Rect(x, y, constrainedWidth, constrainedHeight))
        applyRect(view, if (isSnapEnabled) snapPosition(view, newRect) else newRect)
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
