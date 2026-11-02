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
 * buttons shrink a little instead. Pixel buttons are raised buttons drawn as pixel art by [PixelButtonPainter]. All geometry is given in a square design
 * space of [designSize] units, which is scaled to fit the view.
 */
@SuppressLint("ViewConstructor")
class RaisedButtonView(
    context: Context,
    private var designSize: Float,
    private val lip: Float,
    private val travel: Float,
    parts: List<Part>,
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
        // D-pad parts share their colours in pixel styles, including the centre, which has no input
        val isDpad: Boolean = false,
        // Hand-drawn versions of the glyph or icons, used instead of them by pixel styles
        val pixelIcon: PixelIcon? = null,
        val pixelOffIcon: PixelIcon? = null,
    ) {
        /** The icon shown in the given toggle state: [offIcon] while off, if there is one */
        fun iconFor(isToggledOn: Boolean) = if (isToggledOn) icon else offIcon ?: icon

        /** The hand-drawn icon shown in the given toggle state, chosen like [iconFor] */
        fun pixelIconFor(isToggledOn: Boolean) = if (isToggledOn) pixelIcon else pixelOffIcon ?: pixelIcon
    }

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private var parts = emptyList<Part>()
    private var colors = emptyArray<ControllerButtonColors>()
    private var pressedParts = BooleanArray(0)
    private var faces = emptyArray<Path>()
    private var lips = emptyArray<Path>()
    private var glyphs = emptyArray<Path>()
    private var centersX = FloatArray(0)
    private var centersY = FloatArray(0)
    private val transform = Matrix()
    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f
    private var isToggledOn = true
    private val pixelPainter = appearance.pixelArt?.let { PixelButtonPainter(it, resources.displayMetrics.density) }

    init {
        setParts(parts)
    }

    /**
     * Replaces the buttons drawn by this view and the size of the design space they're given in, e.g. to show a new spacing while a layout is being edited.
     * The view itself stays the same, so its size, touch listener and selection are kept. Nothing is pressed afterwards.
     */
    fun setParts(newParts: List<Part>, newDesignSize: Float = designSize) {
        parts = newParts
        designSize = newDesignSize
        colors = Array(parts.size) { appearance.colorsFor(parts[it].input) }
        pressedParts = BooleanArray(parts.size)
        faces = Array(parts.size) { Path() }
        lips = Array(parts.size) { Path() }
        glyphs = Array(parts.size) { Path() }
        centersX = FloatArray(parts.size)
        centersY = FloatArray(parts.size)
        if (width > 0 && height > 0) {
            updateGeometry(width, height)
        }
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        updateGeometry(w, h)
    }

    /**
     * Scales the parts from the design space to the view's size.
     */
    private fun updateGeometry(w: Int, h: Int) {
        scale = min(w, h) / designSize
        offsetX = (w - designSize * scale) / 2f
        offsetY = (h - designSize * scale) / 2f
        if (appearance.shading == Shading.OUTLINE || appearance.shading == Shading.SOFT_TINT) {
            // Flat buttons have no lip, so they move into the middle of the space it would take up
            offsetY += lip * scale / 2f
        }
        if (pixelPainter != null) {
            fitPixelLip(pixelPainter, w, h)
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
            Shading.PIXEL -> 0f
        }
        pixelPainter?.update(parts, faces, glyphs, transform, isToggledOn)
    }

    /**
     * Pixel lips have the same depth on every button, which is often deeper than the room the design leaves for the lip. When the faces plus the lip are
     * taller than the view, the faces shrink until they fit, staying centred across. If the lip still runs off the bottom because the design leaves more
     * room above the faces than below, they move up into that room.
     */
    private fun fitPixelLip(painter: PixelButtonPainter, w: Int, h: Int) {
        painter.setViewSize(w, h)
        val bounds = RectF()
        val partBounds = RectF()
        for (i in parts.indices) {
            parts[i].shape.computeBounds(partBounds, true)
            if (i == 0) bounds.set(partBounds) else bounds.union(partBounds)
        }
        if (bounds.height() <= 0f) return

        val fitScale = ((h - painter.lipHeight) / bounds.height()).coerceAtLeast(0f)
        if (fitScale < scale) {
            scale = fitScale
            offsetX = (w - designSize * scale) / 2f
            offsetY = (h - designSize * scale) / 2f
        }
        val top = offsetY + bounds.top * scale
        val bottom = offsetY + bounds.bottom * scale
        offsetY -= (bottom + painter.lipHeight - h).coerceIn(0f, top.coerceAtLeast(0f))
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
            if (pixelPainter != null && width > 0 && height > 0) {
                // The icons are part of the cached pixel faces
                updateGeometry(width, height)
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
        if (pixelPainter != null) {
            for (i in parts.indices) {
                pixelPainter.drawLip(canvas, i)
            }
        } else if (appearance.shading == Shading.RAISED) {
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
        if (pixelPainter != null) {
            pixelPainter.drawFace(canvas, index, pressedParts[index])
            return
        }
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
                // Drawn by the pixel painter above
                Shading.PIXEL -> Unit
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
        val icon = part.iconFor(isToggledOn)
        // Tinted here rather than in setParts, since pixel styles rasterize the icon as a mask and never need the tint
        icon?.setTint(partColors.label)
        icon?.draw(canvas)
        if (part.label != null) {
            labelPaint.textSize = part.labelSize * scale
            val baseline = offsetY + part.labelY * scale - (labelPaint.ascent() + labelPaint.descent()) / 2f
            canvas.drawText(part.label, offsetX + part.labelX * scale, baseline, labelPaint)
        }
        canvas.restoreToCount(save)
    }

    companion object {
        private const val OUTLINE_PRESSED_SCALE = 0.9f
        private const val SOFT_TINT_PRESSED_SCALE = 0.94f
        // How big icons get as a share of the face's smaller side, here and in the pixel painter
        internal const val ICON_SIZE = 0.55f
    }
}
