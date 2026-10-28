package me.magnum.melonds.ui.common.view

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.view.View
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.ui.common.view.ControllerAppearance.Shading
import kotlin.math.min

/**
 * Draws one or more buttons in the given [appearance]. Raised buttons sit on a darker lip (the same shape offset down) and sink onto it while pressed; flat
 * buttons shrink a little instead. All geometry is given in a square design space of [designSize] units, which is scaled to fit the view.
 */
@SuppressLint("ViewConstructor")
class RaisedButtonView(
    context: Context,
    private val designSize: Float,
    private val lip: Float,
    private val travel: Float,
    private val parts: List<Part>,
    private val appearance: ControllerAppearance,
) : View(context), PressedInputsView {

    class Part(
        val shape: Path,
        val input: Input? = null,
        val pressable: Boolean = true,
        val label: String? = null,
        val labelX: Float = 0f,
        val labelY: Float = 0f,
        val labelSize: Float = 0f,
        val glyph: Path? = null,
        val icon: Drawable? = null,
        val offIcon: Drawable? = null,
    )

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private val colors = Array(parts.size) { appearance.colorsFor(parts[it].input) }
    private val pressedParts = BooleanArray(parts.size)
    private val faces = Array(parts.size) { Path() }
    private val lips = Array(parts.size) { Path() }
    private val glyphs = Array(parts.size) { Path() }
    private val centersX = FloatArray(parts.size)
    private val centersY = FloatArray(parts.size)
    private val transform = Matrix()
    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f
    private var isToggledOn = true

    init {
        for (i in parts.indices) {
            parts[i].icon?.setTint(colors[i].label)
            parts[i].offIcon?.setTint(colors[i].label)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        scale = min(w, h) / designSize
        offsetX = (w - designSize * scale) / 2f
        offsetY = (h - designSize * scale) / 2f
        if (appearance.shading != Shading.RAISED) {
            // Flat buttons have no lip, so they move into the middle of the space it would take up
            offsetY += lip * scale / 2f
        }
        transform.setScale(scale, scale)
        transform.postTranslate(offsetX, offsetY)

        val bounds = RectF()
        for (i in parts.indices) {
            val part = parts[i]
            part.shape.transform(transform, faces[i])
            part.shape.transform(transform, lips[i])
            lips[i].offset(0f, lip * scale)
            glyphs[i].reset()
            part.glyph?.transform(transform, glyphs[i])
            faces[i].computeBounds(bounds, true)
            centersX[i] = bounds.centerX()
            centersY[i] = bounds.centerY()
            val iconHalfSize = min(bounds.width(), bounds.height()) * ICON_SIZE / 2f
            val iconLeft = (bounds.centerX() - iconHalfSize).toInt()
            val iconTop = (bounds.centerY() - iconHalfSize).toInt()
            val iconSize = (iconHalfSize * 2f).toInt()
            part.icon?.setBounds(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize)
            part.offIcon?.setBounds(iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize)
        }

        // Rings are clipped to the shape, so only the inner half of the stroke is visible. Their width follows the lip, which is proportional to the button size
        val density = resources.displayMetrics.density
        ringPaint.strokeWidth = when (appearance.shading) {
            Shading.RAISED -> 0f
            Shading.OUTLINE -> (lip * scale * 0.8f).coerceAtLeast(3f * density)
            Shading.SOFT_TINT -> (lip * scale).coerceAtLeast(3f * density)
        }
    }

    override fun setPressed(pressed: Boolean) {
        val changed = pressed != isPressed
        super.setPressed(pressed)
        if (changed) {
            for (i in parts.indices) {
                pressedParts[i] = pressed && parts[i].pressable
            }
            invalidate()
        }
    }

    fun setToggleState(enabled: Boolean) {
        if (enabled != isToggledOn) {
            isToggledOn = enabled
            invalidate()
        }
    }

    override fun setPressedInputs(inputs: List<Input>) {
        var changed = false
        for (i in parts.indices) {
            val part = parts[i]
            val pressed = part.pressable && part.input != null && part.input in inputs
            if (pressedParts[i] != pressed) {
                pressedParts[i] = pressed
                changed = true
            }
        }

        if (changed) {
            invalidate()
        }
    }

    override fun onDraw(canvas: Canvas) {
        if (appearance.shading == Shading.RAISED) {
            for (i in parts.indices) {
                fillPaint.color = colors[i].edge
                canvas.drawPath(lips[i], fillPaint)
            }
        }
        // Pressed faces go first so that they slide under their unpressed neighbours
        for (i in parts.indices) {
            if (pressedParts[i]) {
                drawFace(canvas, i)
            }
        }
        for (i in parts.indices) {
            if (!pressedParts[i]) {
                drawFace(canvas, i)
            }
        }
    }

    private fun drawFace(canvas: Canvas, index: Int) {
        val part = parts[index]
        val partColors = colors[index]
        val pressed = pressedParts[index]
        val face = faces[index]
        val save = canvas.save()
        if (pressed) {
            when (appearance.shading) {
                Shading.RAISED -> canvas.translate(0f, travel * scale)
                Shading.OUTLINE -> canvas.scale(OUTLINE_PRESSED_SCALE, OUTLINE_PRESSED_SCALE, centersX[index], centersY[index])
                Shading.SOFT_TINT -> canvas.scale(SOFT_TINT_PRESSED_SCALE, SOFT_TINT_PRESSED_SCALE, centersX[index], centersY[index])
            }
        }

        fillPaint.color = if (pressed) partColors.pressedFace else partColors.face
        canvas.drawPath(face, fillPaint)
        ringPaint.color = partColors.edge
        if (appearance.shading == Shading.OUTLINE || (appearance.shading == Shading.SOFT_TINT && pressed)) {
            val clipSave = canvas.save()
            canvas.clipPath(face)
            canvas.drawPath(face, ringPaint)
            canvas.restoreToCount(clipSave)
        }

        labelPaint.color = partColors.label
        if (part.glyph != null) {
            canvas.drawPath(glyphs[index], labelPaint)
        }
        val icon = if (isToggledOn) part.icon else part.offIcon ?: part.icon
        icon?.draw(canvas)
        if (part.label != null) {
            labelPaint.textSize = part.labelSize * scale
            val baseline = offsetY + part.labelY * scale - (labelPaint.ascent() + labelPaint.descent()) / 2f
            canvas.drawText(part.label, offsetX + part.labelX * scale, baseline, labelPaint)
        }
        canvas.restoreToCount(save)
    }

    private companion object {
        const val OUTLINE_PRESSED_SCALE = 0.9f
        const val SOFT_TINT_PRESSED_SCALE = 0.94f
        const val ICON_SIZE = 0.55f
    }
}
