package me.magnum.melonds.ui.layouteditor

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.TypedValue
import androidx.core.content.ContextCompat
import androidx.core.graphics.ColorUtils
import me.magnum.melonds.R
import me.magnum.melonds.impl.dpToPixels
import kotlin.math.max

/**
 * Draws what the layout editor shows besides its items: the snap grid and fold lines behind them, and the selected item's resize handles and size
 * label in front of them.
 */
internal class LayoutEditorOverlayDrawer(private val context: Context) {
    private val handleSize = context.dpToPixels(10f)
    private val cornerHandleSize = context.dpToPixels(18f)
    private val rect = RectF()
    private val handleFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private val handleStrokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0x99000000.toInt()
        strokeWidth = context.dpToPixels(1f)
    }
    private val resizeIcon = ContextCompat.getDrawable(context, R.drawable.ic_open_in_full)?.mutate()?.apply { setTint(Color.BLACK) }
    private val sizeLabelTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.textLayoutEditor)
        textSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 14f, context.resources.displayMetrics)
        textAlign = Paint.Align.CENTER
    }
    private val sizeLabelBackgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = ContextCompat.getColor(context, R.color.layoutEditorPanelBackground) }
    private val sizeLabelPadding = context.dpToPixels(8f)
    // The editor background is always black, whatever the theme, so the grid is always light
    private val gridPaint = Paint().apply {
        color = ColorUtils.setAlphaComponent(Color.WHITE, 0x50)
        strokeWidth = max(1f, context.dpToPixels(1f))
    }
    private val foldPaint = Paint().apply {
        color = ColorUtils.setAlphaComponent(Color.WHITE, 0xB0)
        strokeWidth = max(1f, context.dpToPixels(2f))
    }

    /**
     * Draws the grid of an editor of [width] by [height] with cells of [cellSize], and the edges of the given vertical and horizontal folds.
     */
    fun drawGrid(canvas: Canvas, width: Int, height: Int, cellSize: Int, verticalFolds: List<SnapSpan>, horizontalFolds: List<SnapSpan>) {
        val viewWidth = width.toFloat()
        val viewHeight = height.toFloat()
        // The same lines the snapping uses: from the middle of the editor out, plus the editor edges
        LayoutEditorSnapping.gridLines(width, cellSize).forEach {
            val x = insetLine(it, width, gridPaint)
            canvas.drawLine(x, 0f, x, viewHeight, gridPaint)
        }
        LayoutEditorSnapping.gridLines(height, cellSize).forEach {
            val y = insetLine(it, height, gridPaint)
            canvas.drawLine(0f, y, viewWidth, y, gridPaint)
        }
        verticalFolds.flatMap { listOf(it.start, it.end) }.filter { it in 0..width }.distinct().forEach {
            val x = insetLine(it, width, foldPaint)
            canvas.drawLine(x, 0f, x, viewHeight, foldPaint)
        }
        horizontalFolds.flatMap { listOf(it.start, it.end) }.filter { it in 0..height }.distinct().forEach {
            val y = insetLine(it, height, foldPaint)
            canvas.drawLine(0f, y, viewWidth, y, foldPaint)
        }
    }

    /**
     * Draws the resize handles of an item with the given bounds. With [cornerOnly], only the bottom-right one.
     */
    fun drawHandles(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float, cornerOnly: Boolean) {
        if (!cornerOnly) {
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
    }

    /**
     * Draws an item's size, [itemWidth] by [itemHeight], as a label in the middle of its bounds.
     */
    fun drawSizeLabel(canvas: Canvas, itemWidth: Int, itemHeight: Int, left: Float, top: Float, right: Float, bottom: Float) {
        val label = context.getString(R.string.layout_editor_size_value, itemWidth, itemHeight)
        val centerX = (left + right) / 2f
        val centerY = (top + bottom) / 2f
        val textWidth = sizeLabelTextPaint.measureText(label)
        val fontMetrics = sizeLabelTextPaint.fontMetrics
        val textHeight = fontMetrics.descent - fontMetrics.ascent
        rect.set(
            centerX - textWidth / 2f - sizeLabelPadding,
            centerY - textHeight / 2f - sizeLabelPadding / 2f,
            centerX + textWidth / 2f + sizeLabelPadding,
            centerY + textHeight / 2f + sizeLabelPadding / 2f,
        )
        canvas.drawRoundRect(rect, sizeLabelPadding, sizeLabelPadding, sizeLabelBackgroundPaint)
        canvas.drawText(label, centerX, centerY - (fontMetrics.ascent + fontMetrics.descent) / 2f, sizeLabelTextPaint)
    }

    /**
     * Moves lines at the editor edges inwards by half the stroke, so they aren't half cut off.
     */
    private fun insetLine(position: Int, limit: Int, paint: Paint): Float {
        val halfStroke = paint.strokeWidth / 2f
        return position.toFloat().coerceIn(halfStroke, max(halfStroke, limit - halfStroke))
    }

    private fun drawHandle(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        rect.set(left, top, right, bottom)
        canvas.drawRect(rect, handleFillPaint)
        canvas.drawRect(rect, handleStrokePaint)
    }
}
