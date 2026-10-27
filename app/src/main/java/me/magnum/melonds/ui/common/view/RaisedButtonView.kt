package me.magnum.melonds.ui.common.view

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.view.View
import me.magnum.melonds.domain.model.Input
import kotlin.math.min

/**
 * Draws one or more buttons in a raised style: each face sits on a darker lip (the same shape offset down) and sinks onto it while pressed. All geometry is given in
 * a square design space of [designSize] units, which is scaled to fit the view.
 */
@SuppressLint("ViewConstructor")
class RaisedButtonView(
    context: Context,
    private val designSize: Float,
    private val lip: Float,
    private val travel: Float,
    private val parts: List<Part>,
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
    )

    private val facePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = FACE_COLOR }
    private val pressedFacePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = PRESSED_FACE_COLOR }
    private val lipPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = LIP_COLOR }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private val pressedParts = BooleanArray(parts.size)
    private val faces = Array(parts.size) { Path() }
    private val lips = Array(parts.size) { Path() }
    private val glyphs = Array(parts.size) { Path() }
    private val transform = Matrix()
    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        scale = min(w, h) / designSize
        offsetX = (w - designSize * scale) / 2f
        offsetY = (h - designSize * scale) / 2f
        transform.setScale(scale, scale)
        transform.postTranslate(offsetX, offsetY)

        for (i in parts.indices) {
            val part = parts[i]
            part.shape.transform(transform, faces[i])
            part.shape.transform(transform, lips[i])
            lips[i].offset(0f, lip * scale)
            glyphs[i].reset()
            part.glyph?.transform(transform, glyphs[i])
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
        for (i in parts.indices) {
            canvas.drawPath(lips[i], lipPaint)
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
        val pressed = pressedParts[index]
        val save = canvas.save()
        if (pressed) {
            canvas.translate(0f, travel * scale)
        }

        canvas.drawPath(faces[index], if (pressed) pressedFacePaint else facePaint)
        if (part.glyph != null) {
            canvas.drawPath(glyphs[index], labelPaint)
        }
        if (part.label != null) {
            labelPaint.textSize = part.labelSize * scale
            val baseline = offsetY + part.labelY * scale - (labelPaint.ascent() + labelPaint.descent()) / 2f
            canvas.drawText(part.label, offsetX + part.labelX * scale, baseline, labelPaint)
        }
        canvas.restoreToCount(save)
    }

    private companion object {
        val FACE_COLOR = Color.parseColor("#8B8E97")
        val PRESSED_FACE_COLOR = Color.parseColor("#6E717A")
        val LIP_COLOR = Color.parseColor("#2C2E34")
    }
}
