package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.Context
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
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.*
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.PositionedLayoutComponent
import me.magnum.melonds.domain.model.layout.UILayout
import me.magnum.melonds.ui.common.LayoutComponentView
import me.magnum.melonds.ui.common.LayoutView
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget
import me.magnum.melonds.impl.dpToPixels
import kotlin.math.*

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

    private var onViewSelectedListener: ((LayoutComponentView) -> Unit)? = null
    private var onViewDeselectedListener: ((LayoutComponentView) -> Unit)? = null
    private var otherClickListener: OnClickListener? = null
    private val defaultComponentWidth by lazy { context.dpToPixels(100f).toInt() }
    val minComponentSize by lazy { context.dpToPixels(30f).toInt() }
    private var selectedView: LayoutComponentView? = null
    private var selectedViewAnchor = Anchor.TOP_LEFT
    private var modifiedByUser = false
    private var resizeGesture: ResizeGesture? = null
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

    init {
        super.setOnClickListener {
            if (selectedView != null) {
                deselectCurrentView()
            } else {
                otherClickListener?.onClick(it)
            }
        }
    }

    override fun instantiateLayout(layoutConfiguration: UILayout, layoutTarget: LayoutTarget) {
        super.instantiateLayout(layoutConfiguration, layoutTarget)
        modifiedByUser = false
    }

    override fun destroyLayout() {
        // The selected view is about to be destroyed. Deselect it so that the item controls and handles don't stay on a stale view
        deselectCurrentView()
        super.destroyLayout()
    }

    fun setOnViewSelectedListener(listener: (LayoutComponentView) -> Unit) {
        onViewSelectedListener = listener
    }

    fun setOnViewDeselectedListener(listener: (LayoutComponentView) -> Unit) {
        onViewDeselectedListener = listener
    }

    fun getSelectedView(): LayoutComponentView? {
        return selectedView
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
            PositionedLayoutComponent(it.getRect(), it.component, it.baseAlpha, it.onTop)
        }
    }

    fun handleKeyDown(event: KeyEvent): Boolean {
        val currentlySelectedView = selectedView ?: return false

        when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> dragView(currentlySelectedView, 0f, -1f)
            KeyEvent.KEYCODE_DPAD_DOWN -> dragView(currentlySelectedView, 0f, 1f)
            KeyEvent.KEYCODE_DPAD_LEFT -> dragView(currentlySelectedView, -1f, 0f)
            KeyEvent.KEYCODE_DPAD_RIGHT -> dragView(currentlySelectedView, 1f, 0f)
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
        layoutComponentView.view.alpha = 0.5f
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
        return min(viewWidth, viewHeight) / 4f < minHandleTouchSize
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
            listOf(Anchor.BOTTOM_RIGHT) to smallerSide * 0.4f
        } else {
            Anchor.entries to min(maxHandleTouchSize, smallerSide / 4f)
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
        val desiredWidth = start.width + if (corner.isLeft) -offsetX else offsetX
        val desiredHeight = start.height + if (corner.isTop) -offsetY else offsetY
        val maxWidth = if (corner.isLeft) fixedX else width - fixedX
        val maxHeight = if (corner.isTop) fixedY else height - fixedY

        // With a locked aspect ratio, follow whichever side the finger wants to make bigger
        val ratio = getLockedAspectRatio(view)
        val (newWidth, newHeight) = if (ratio != null) {
            constrainSize(view, max(desiredWidth, desiredHeight * ratio), 0f, maxWidth, maxHeight)
        } else {
            constrainSize(view, desiredWidth, desiredHeight, maxWidth, maxHeight)
        }
        val x = if (corner.isLeft) fixedX - newWidth else fixedX
        val y = if (corner.isTop) fixedY - newHeight else fixedY
        applyRect(view, Rect(x, y, newWidth, newHeight))
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
     * Returns the range of widths the selected view can have inside the editor, taking its locked aspect ratio into account.
     */
    fun getSelectedViewWidthRange(): IntRange {
        val view = selectedView ?: return IntRange.EMPTY
        val (minWidth, _) = constrainSize(view, 0f, 0f, width, height)
        val (maxWidth, _) = constrainSize(view, width.toFloat(), height.toFloat(), width, height)
        return minWidth..max(minWidth, maxWidth)
    }

    /**
     * Returns the range of heights the selected view can have inside the editor, taking its locked aspect ratio into account.
     */
    fun getSelectedViewHeightRange(): IntRange {
        val view = selectedView ?: return IntRange.EMPTY
        val (_, minHeight) = constrainSize(view, 0f, 0f, width, height)
        val (_, maxHeight) = constrainSize(view, width.toFloat(), height.toFloat(), width, height)
        return minHeight..max(minHeight, maxHeight)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupDragHandler(layoutComponentView: LayoutComponentView) {
        layoutComponentView.view.setOnTouchListener(object : OnTouchListener {
            private var dragging = false

            private var downOffsetX = -1f
            private var downOffsetY = -1f
            private var downPosition: Point? = null
            private var modifiedBeforeDown = false

            override fun onTouch(view: View?, motionEvent: MotionEvent?): Boolean {
                if (view == null || motionEvent == null)
                    return false

                // Let touches that start in the system gesture area (e.g. the back gesture) go through to the system instead of grabbing the view
                if (motionEvent.action == MotionEvent.ACTION_DOWN && isInSystemGestureArea(view, motionEvent)) {
                    return false
                }

                if (selectedView != null) {
                    deselectCurrentView()
                }

                return when (motionEvent.action) {
                    MotionEvent.ACTION_DOWN -> {
                        dragging = false
                        downOffsetX = motionEvent.x
                        downOffsetY = motionEvent.y
                        downPosition = layoutComponentView.getPosition()
                        modifiedBeforeDown = modifiedByUser
                        view.alpha = 1f
                        layoutComponentView.setHighlighted(true)
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        if (!dragging) {
                            val distance = sqrt((motionEvent.x - downOffsetX).pow(2f) + (motionEvent.y - downOffsetY).pow(2f))
                            if (distance >= 25) {
                                dragging = true
                            }
                        } else {
                            dragView(layoutComponentView, motionEvent.x - downOffsetX, motionEvent.y - downOffsetY)
                        }
                        true
                    }
                    MotionEvent.ACTION_UP -> {
                        if (!dragging) {
                            selectView(layoutComponentView)
                        } else {
                            view.alpha = 0.5f
                            layoutComponentView.setHighlighted(false)
                            dragging = false
                        }
                        true
                    }
                    MotionEvent.ACTION_CANCEL -> {
                        // The gesture was taken away (usually by the system back gesture). Undo the drag and don't select the view
                        if (dragging) {
                            downPosition?.let { layoutComponentView.setPosition(it) }
                            modifiedByUser = modifiedBeforeDown
                        }
                        view.alpha = 0.5f
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
        deselectCurrentView()
    }

    private fun deselectCurrentView() {
        selectedView?.let {
            it.view.alpha = 0.5f
            it.setHighlighted(false)
            onViewDeselectedListener?.invoke(it)
        }
        selectedView = null
        resizeGesture = null
        invalidate()
    }

    fun deleteSelectedView() {
        val currentlySelectedView = selectedView ?: return
        removeView(currentlySelectedView.view)
        views.remove(currentlySelectedView.component)
        deselectCurrentView()
        modifiedByUser = true
    }

    private fun dragView(view: LayoutComponentView, offsetX: Float, offsetY: Float) {
        val currentPosition = view.getPosition()
        val finalX = min(max(currentPosition.x + offsetX, 0f), width - view.getWidth().toFloat())
        val finalY = min(max(currentPosition.y + offsetY, 0f), height - view.getHeight().toFloat())
        view.setPosition(Point(finalX.toInt(), finalY.toInt()))
        modifiedByUser = true
    }

    fun moveSelectedView(offsetX: Int, offsetY: Int) {
        val view = selectedView ?: return
        dragView(view, offsetX.toFloat(), offsetY.toFloat())
        updateSelectedViewAnchorAfterLayout()
    }

    fun setSelectedViewAlpha(alpha: Float) {
        selectedView?.let {
            it.baseAlpha = alpha
            modifiedByUser = true
        }
    }

    fun setSelectedScreenOnTop(onTop: Boolean) {
        selectedView?.let {
            it.onTop = onTop
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
            if (topScreen.onTop) {
                addView(bottomScreen.view, 0)
                addView(topScreen.view, 0)
            } else if (bottomScreen.onTop) {
                addView(topScreen.view, 0)
                addView(bottomScreen.view, 0)
            } else {
                addView(bottomScreen.view, 0)
                addView(topScreen.view, 0)
            }
        }
    }

    fun centerSelectedViewHorizontally() {
        val view = selectedView ?: return
        val centerX = (width - view.getWidth()) / 2
        val position = view.getPosition()
        view.setPosition(Point(centerX, position.y))
        modifiedByUser = true
        updateSelectedViewAnchorAfterLayout()
    }

    fun centerSelectedViewVertically() {
        val view = selectedView ?: return
        val centerY = (height - view.getHeight()) / 2
        val position = view.getPosition()
        view.setPosition(Point(position.x, centerY))
        modifiedByUser = true
        updateSelectedViewAnchorAfterLayout()
    }

    /**
     * Resizes the selected view, keeping its anchor corner in place and the view inside the editor. With a locked aspect ratio the height is
     * derived from the width.
     */
    fun resizeSelectedView(newWidth: Float, newHeight: Float) {
        val view = selectedView ?: return
        val (constrainedWidth, constrainedHeight) = constrainSize(view, newWidth, newHeight, width, height)
        val rect = view.getRect()

        val x = if (selectedViewAnchor.isLeft) rect.x else rect.right - constrainedWidth
        val y = if (selectedViewAnchor.isTop) rect.y else rect.bottom - constrainedHeight
        applyRect(view, Rect(
            x.coerceIn(0, max(0, width - constrainedWidth)),
            y.coerceIn(0, max(0, height - constrainedHeight)),
            constrainedWidth,
            constrainedHeight,
        ))
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
        val params = view.view.layoutParams as LayoutParams
        if (params.leftMargin == rect.x && params.topMargin == rect.y && params.width == rect.width && params.height == rect.height) {
            return
        }
        view.setPositionAndSize(Point(rect.x, rect.y), rect.width, rect.height)
        modifiedByUser = true
    }
}
