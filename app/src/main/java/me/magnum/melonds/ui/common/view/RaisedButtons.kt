package me.magnum.melonds.ui.common.view

import android.content.Context
import android.graphics.Matrix
import android.graphics.Path
import android.graphics.RectF
import androidx.annotation.DrawableRes
import androidx.core.content.ContextCompat
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.input.ControllerStyle

/**
 * Builders for the raised on-screen buttons. Button positions match the hit areas used by the input handlers: the D-pad and ABXY are drawn on a 512 grid,
 * which grows with their spacing (see [spacedDesignSize]), and the hit areas scale with the view either way.
 */
object RaisedButtons {
    private const val OFF_ICON_ALPHA = 90

    /**
     * How many design units the D-pad and ABXY buttons move out by at spacing 1. Their design box grows by twice that, so the buttons keep their size.
     */
    const val SPACING_EXTRA = 128f

    /**
     * The size of the design box of the D-pad and ABXY with the given spacing, from 512 at 0 to 768 at 1. The view must grow by the same ratio for the
     * buttons to keep their size on screen.
     */
    fun spacedDesignSize(spacing: Float): Float {
        return 512f + 2f * SPACING_EXTRA * spacing
    }

    fun dpad(context: Context, style: ControllerStyle): RaisedButtonView {
        return RaisedButtonView(context, spacedDesignSize(0f), 20f, 16f, dpadParts(0f), ControllerAppearance.of(style))
    }

    /**
     * The parts of [dpad] with the given [spacing], which goes from 0 to 1, to be drawn in a box of [spacedDesignSize]. Raising it moves the arms away from
     * the centre square by up to [SPACING_EXTRA], while every part keeps its size.
     */
    fun dpadParts(spacing: Float): List<RaisedButtonView.Part> {
        // The centre square is 10 units above the middle so that the cross plus its lip is centred in the box
        val extra = SPACING_EXTRA * spacing
        val cx = spacedDesignSize(spacing) / 2f
        val cy = cx - 10f
        val halfWidth = 80f
        val armStart = halfWidth + 6f + extra
        val reach = 232f + extra
        // Glyphs sit halfway along their arm
        val glyphOffset = (armStart + reach) / 2f
        val corner = 16f
        return listOf(
            RaisedButtonView.Part(roundRect(cx - halfWidth, cy - halfWidth, cx + halfWidth, cy + halfWidth, corner), pressable = false),
            RaisedButtonView.Part(roundRect(cx - halfWidth, cy - reach, cx + halfWidth, cy - armStart, corner), Input.UP, glyph = triangle(cx, cy - glyphOffset, 0f, -1f)),
            RaisedButtonView.Part(roundRect(cx - halfWidth, cy + armStart, cx + halfWidth, cy + reach, corner), Input.DOWN, glyph = triangle(cx, cy + glyphOffset, 0f, 1f)),
            RaisedButtonView.Part(roundRect(cx - reach, cy - halfWidth, cx - armStart, cy + halfWidth, corner), Input.LEFT, glyph = triangle(cx - glyphOffset, cy, -1f, 0f)),
            RaisedButtonView.Part(roundRect(cx + armStart, cy - halfWidth, cx + reach, cy + halfWidth, corner), Input.RIGHT, glyph = triangle(cx + glyphOffset, cy, 1f, 0f)),
        )
    }

    fun abxy(context: Context, style: ControllerStyle): RaisedButtonView {
        return RaisedButtonView(context, spacedDesignSize(0f), 20f, 16f, abxyParts(0f), ControllerAppearance.of(style))
    }

    /**
     * The parts of [abxy] with the given [spacing], which goes from 0 to 1, to be drawn in a box of [spacedDesignSize]. Raising it moves the buttons away
     * from the middle by up to [SPACING_EXTRA], while they keep their size.
     */
    fun abxyParts(spacing: Float): List<RaisedButtonView.Part> {
        // The middle sits 2 units above the centre of the box, which leaves room for the lip under the bottom button
        val cx = spacedDesignSize(spacing) / 2f
        val cy = cx - 2f
        val offset = 160f + SPACING_EXTRA * spacing
        return listOf(
            circleButton(cx, cy - offset, 78f, Input.X, "X", 64f),
            circleButton(cx - offset, cy, 78f, Input.Y, "Y", 64f),
            circleButton(cx + offset, cy, 78f, Input.A, "A", 64f),
            circleButton(cx, cy + offset, 78f, Input.B, "B", 64f),
        )
    }

    fun shoulderL(context: Context, style: ControllerStyle): RaisedButtonView {
        return RaisedButtonView(context, 128f, 10f, 8f, listOf(RaisedButtonView.Part(shoulderShape(false), label = "L", labelX = 40f, labelY = 59f, labelSize = 60f)), ControllerAppearance.of(style))
    }

    fun shoulderR(context: Context, style: ControllerStyle): RaisedButtonView {
        return RaisedButtonView(context, 128f, 10f, 8f, listOf(RaisedButtonView.Part(shoulderShape(true), label = "R", labelX = 88f, labelY = 59f, labelSize = 60f)), ControllerAppearance.of(style))
    }

    fun start(context: Context, style: ControllerStyle): RaisedButtonView {
        return RaisedButtonView(context, 64f, 7f, 5.6f, listOf(circleButton(32f, 28.5f, 28f, null, "START", 12f)), ControllerAppearance.of(style))
    }

    fun select(context: Context, style: ControllerStyle): RaisedButtonView {
        return RaisedButtonView(context, 64f, 7f, 5.6f, listOf(circleButton(32f, 28.5f, 28f, null, "SELECT", 12f)), ControllerAppearance.of(style))
    }

    /**
     * Rounded square with an icon in the middle. When [offIconRes] is given it's shown while the button is toggled off, otherwise [dimWhenOff] shows a faded
     * [iconRes] instead.
     */
    fun iconButton(context: Context, style: ControllerStyle, @DrawableRes iconRes: Int?, @DrawableRes offIconRes: Int? = null, dimWhenOff: Boolean = false): RaisedButtonView {
        val icon = iconRes?.let { ContextCompat.getDrawable(context, it)?.mutate() }
        val offIcon = when {
            offIconRes != null -> ContextCompat.getDrawable(context, offIconRes)?.mutate()
            dimWhenOff && iconRes != null -> ContextCompat.getDrawable(context, iconRes)?.mutate()?.apply { alpha = OFF_ICON_ALPHA }
            else -> null
        }
        // Same proportions as Start/Select: the 57 unit face plus its 7 unit lip fill the 64 unit square
        val shape = roundRect(3.5f, 0f, 60.5f, 57f, 11.4f)
        return RaisedButtonView(context, 64f, 7f, 5.6f, listOf(RaisedButtonView.Part(shape, icon = icon, offIcon = offIcon)), ControllerAppearance.of(style))
    }

    private fun circleButton(cx: Float, cy: Float, radius: Float, input: Input?, label: String, labelSize: Float): RaisedButtonView.Part {
        val shape = Path().apply { addCircle(cx, cy, radius, Path.Direction.CW) }
        return RaisedButtonView.Part(shape, input, label = label, labelX = cx, labelY = cy, labelSize = labelSize)
    }

    private fun roundRect(left: Float, top: Float, right: Float, bottom: Float, radius: Float): Path {
        return Path().apply { addRoundRect(RectF(left, top, right, bottom), radius, radius, Path.Direction.CW) }
    }

    private fun triangle(cx: Float, cy: Float, dx: Float, dy: Float): Path {
        val halfLength = 20f
        val halfWidth = 26f
        return Path().apply {
            moveTo(cx + dx * halfLength, cy + dy * halfLength)
            lineTo(cx - dx * halfLength - dy * halfWidth, cy - dy * halfLength + dx * halfWidth)
            lineTo(cx - dx * halfLength + dy * halfWidth, cy - dy * halfLength - dx * halfWidth)
            close()
        }
    }

    // L-shaped tab in a 128 grid: a full-width bar on top with a stem going down on the outer side. The face ends at 118 to leave room for the lip.
    private fun shoulderShape(mirrored: Boolean): Path {
        val bar = Path().apply {
            addRoundRect(RectF(0f, 0f, 128f, 70f), floatArrayOf(12f, 12f, 12f, 12f, 8f, 8f, 8f, 8f), Path.Direction.CW)
        }
        val stem = Path().apply {
            addRoundRect(RectF(0f, 0f, 80f, 118f), floatArrayOf(12f, 12f, 0f, 0f, 8f, 8f, 12f, 12f), Path.Direction.CW)
        }
        bar.op(stem, Path.Op.UNION)
        if (mirrored) {
            bar.transform(Matrix().apply { setScale(-1f, 1f, 64f, 0f) })
        }
        return bar
    }
}
